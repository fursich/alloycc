#include "alloycc.h"

//
// Parser
//

// Scope for local or global vars (incl. string literal),
// typedefs, or enum constants
typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next;
  char *name;
  int depth;

  Var *var;
  Type *type_def;
  Type *enum_ty;
  int enum_val;
};

// variable attributes (e.g. typedef, extern, etc)
typedef struct {
  bool is_typedef;
  bool is_static;
  bool is_extern;
  int align;
} VarAttr;

// Scope for struct, union or enum tags
typedef struct TagScope TagScope;
struct TagScope {
  TagScope *next;
  char *name;
  int depth;
  Type *ty;
};

// represents a variable initializer
// this struct has a tree structure since initializers can be nested:
// (e.g. int x[2][2] = {{1, 2}, {3, 4}})
typedef struct Initializer Initializer;
struct Initializer {
  Type *ty;
  Token *tok;

  // for leaf nodes, len == 0 and expr is the initializer expression
  // otherwise children has chile nodes of length `len`
  int len;
  Node *expr;

  // children might contain null pointers, in which case, corresponding
  // members have to be initialized by zeroes.
  Initializer **children;
};

// For local variable initializer
typedef struct InitDesg InitDesg;
struct InitDesg {
  InitDesg *next;
  int idx;
  Member *member;
  Var *var;
};

Var *locals;
Var *globals;

// C has two block scopes: one for variables/typedefs and
// the other for struct/union/enum tags.
static VarScope *var_scope;
static TagScope *tag_scope;

static int scope_depth;

// Points to the function object the parser is currently parsing.
static Var *current_fn;
// Points to a node representing a switch if we are parsing
// a switch statement. Otherwise, NULL.
static Node *current_switch;

static bool is_typename(Token *tok);
static Type *typespec(Token **rest, Token *tok, VarAttr *attr);
static Type *typename(Token **rest, Token *tok);
static void register_enum_list(Token **rest, Token *tok, Type *ty);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *type_suffix(Token **rest, Token *tok, Type *ty);
static Type *declarator(Token **rest, Token *tok, Type *base);
static Node *declaration(Token **rest, Token *tok);
static Initializer *initializer(Token **rest, Token *tok, Type *ty);
static void gvar_initializer(Token **rest, Token *tok, Var *var);
static Node *lvar_initializer(Token **rest, Token *tok, Var *var);

static Function *funcdef(Token **rest, Token *tok);
static Type *func_params(Token **rest, Token *tok, Type *ty);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);

static Node *block_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);

static Node *if_stmt(Token **rest, Token *tok);
static Node *while_stmt(Token **rest, Token *tok);
static Node *do_stmt(Token **rest, Token *tok);
static Node *for_stmt(Token **rest, Token *tok);

static Node *goto_stmt(Token **rest, Token *tok);
static Node *switch_stmt(Token **rest, Token *tok);
static Node *case_labeled_stmt(Token **rest, Token *tok);
static Node *default_labeled_stmt(Token **rest, Token *tok);
static Node *labeled_stmt(Token **rest, Token *tok);

static Node *return_stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);

static Node *expr(Token **rest, Token *tok);
static long eval(Node *node);
static long eval2(Node *node, Var **var);
static long const_expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *conditional(Token **rest, Token *tok);
static Node *logor(Token **rest, Token *tok);
static Node *logand(Token **rest, Token *tok);
static Node *bitor(Token **rest, Token *tok);
static Node *bitxor(Token **rest, Token *tok);
static Node *bitand(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *shift(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok, Node *fn);

static void enter_scope() {
  scope_depth++;
}

static void leave_scope() {
  scope_depth--;
  // remove deeper-scoped vars from the end of the chain
  while (var_scope && var_scope->depth > scope_depth)
    var_scope = var_scope->next;

  while (tag_scope && tag_scope->depth > scope_depth)
    tag_scope = tag_scope->next;
}

static VarScope *push_scope(char *name) {
  VarScope *sc = calloc(1, sizeof(VarScope));
  sc->name = name;
  sc->depth = scope_depth;
  sc->next = var_scope;
  var_scope = sc;
  return sc;
}

static TagScope *push_tag_scope(char *name, Type *ty) {
  TagScope *sc = calloc(1, sizeof(TagScope));
  sc->name = name;
  sc->ty = ty;
  sc->depth = scope_depth;
  sc->next = tag_scope;
  tag_scope = sc;
  return sc;
}

static Program *new_program() {
  Program *prog = calloc(1, sizeof(Program));
  return prog;
}

static Function *new_function(Type *ty, VarAttr *attr) {
  Function *func = calloc(1, sizeof(Function));
  func->name = get_identifier(ty->ident);
  func->is_static = attr->is_static;
  func->is_variadic= ty->is_variadic;
  // TODO: consider return type: func->return_ty = ty;
  return func;
}

static Node *new_node(NodeKind kind, Token *tok) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->token = tok;
  return node;
}

static Var *new_var(char *name, Type *ty) {
  Var *var = calloc(1, sizeof(Var));
  var->name = name;
  var->ty = ty;
  var->align = ty->align;
  return var;
}

static Member *new_member(char *name, Type *ty) {
  Member *mem = calloc(1, sizeof(Member));
  mem->name = name;
  mem->ty= ty;
  return mem;
}

static Initializer *new_init(Type *ty, int len, Node *expr, Token *tok) {
  Initializer *init = calloc(1, sizeof(Initializer));
  init->ty = ty;
  init->tok = tok;
  init->len = len;
  init->expr = expr;
  if (len)
    init->children = calloc(len, sizeof(Initializer *));

  return init;
}

static Var *new_lvar(char *name, Type *ty) {
  Var *var = new_var(name, ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  push_scope(name)->var = var;
  return var;
}

static Var *new_gvar(char *name, Type *ty, bool is_static, bool emit) {
  Var *var = new_var(name, ty);
  var->is_local = false;
  var->is_static = is_static;
  if (emit) {
    var->next = globals;
    globals = var;
  }
  push_scope(name)->var = var;
  return var;
}

// finds a variable or a typedef by name
static VarScope *lookup_var(char *name) {
  for (VarScope *sc = var_scope; sc; sc = sc->next) {
    if (!strcmp(sc->name, name))
      return sc;
  }
  return NULL;
}

static Type *lookup_typedef(Token *tok) {
  if (tok->kind == TK_IDENT) {
    VarScope *sc = lookup_var(get_identifier(tok));
    if (sc)
      return sc->type_def;
  }
  return NULL;
}

static TagScope *lookup_tag(char *name) {
  for (TagScope *sc = tag_scope; sc; sc = sc->next) {
    if (!strcmp(sc->name, name))
      return sc;
  }
  return NULL;
}

static char *new_gvar_name() {
  static int counter = 0;
  char *buf = malloc(20);

  sprintf(buf, ".L.data.%d", counter++);
  return buf;
}

static Var *new_string_literal(char *s, int len) {
  Type *ty = array_of(ty_char, len);
  Var *var = new_gvar(new_gvar_name(), ty, true, true);
  var->init_data = s;
  return var;
}

static Node *new_node_unary(NodeKind kind, Node *lhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  return node;
}

static Node *new_node_binary(NodeKind kind, Node *lhs, Node *rhs, Token *tok) {
  Node *node = new_node(kind, tok);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_node_var(Var *var, Token *tok) {
  Node *node = new_node(ND_VAR, tok);
  node->var = var;
  return node;
}

static Node *new_node_num(long val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  return node;
}

static Node *new_node_num_ulong(long val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
  node->ty = ty_ulong;
  return node;
}

static Node *new_node_add(Node *lhs, Node *rhs, Token *tok) {
  generate_type(lhs);
  generate_type(rhs);

  // number + number
  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_node_binary(ND_ADD, lhs, rhs, tok);

  // ptr + ptr (illegal)
  if (is_pointer_like(lhs->ty) && is_pointer_like(rhs->ty))
    error_tok(tok, "invalid operands");

  // number + ptr (canonicalize)
  if (is_integer(lhs->ty) && is_pointer_like(rhs->ty)) {
    Node *t = lhs;
    lhs = rhs;
    rhs = t;
  }

  // ptr + number (multiplied by base size of ptr)
  if (is_pointer_like(lhs->ty) && is_integer(rhs->ty)) {
    rhs = new_node_binary(ND_MUL, rhs, new_node_num(size_of(lhs->ty->base), tok), tok);
    return new_node_binary(ND_ADD, lhs, rhs, tok);
  }

  error_tok(tok, "invalid operands");
}

static Node *new_node_sub(Node *lhs, Node *rhs, Token *tok) {
  generate_type(lhs);
  generate_type(rhs);

  // number - number
  if (is_integer(lhs->ty) && is_integer(rhs->ty))
    return new_node_binary(ND_SUB, lhs, rhs, tok);

  // ptr - number (multiplied by base size of ptr)
  if (is_pointer_like(lhs->ty) && is_integer(rhs->ty)) {
    rhs = new_node_binary(ND_MUL, rhs, new_node_num(size_of(lhs->ty->base), tok), tok);
    return new_node_binary(ND_SUB, lhs, rhs, tok);
  }

  // ptr - ptr: returns how many elements are between the two
  if (is_pointer_like(lhs->ty) && is_pointer_like(rhs->ty)) {
    Node *node = new_node_binary(ND_SUB, lhs, rhs, tok);
    return new_node_binary(ND_DIV, node, new_node_num(size_of(lhs->ty->base), tok), tok);
  }

  // number - ptr (illegal)
  error_tok(tok, "invalid operands");
}

Node *new_node_cast(Node *expr, Type *ty) {
  generate_type(expr);

  Node *node = new_node_unary(ND_CAST, expr, expr->token);
  node->ty = copy_ty(ty);
  return node;
}

// program = (funcdef | global-var)*
Program *parse(Token *tok) {
  // add built-if functon types
  new_gvar("__builtin_va_start", func_returning(ty_void), true, false);

  // read source code until EOF
  Function head = {0};
  Function *cur = &head;
  globals = NULL;

  while (tok->kind != TK_EOF) {
    Token *start = tok;
    VarAttr attr = {0};
    Type *basety = typespec(&tok, tok, &attr);
    if (consume(&tok, tok, ";"))
      continue;
    Type *ty = declarator(&tok, tok, basety);

    // typedef
    // "typedef" basety foo[3], *bar, ..
    if (attr.is_typedef) {
      for(;;) {
        if (!ty->ident)
          error_tok(ty->name_pos, "typedef name omitted");

        push_scope(get_identifier(ty->ident))->type_def =ty;

        if (consume(&tok, tok, ";"))
          break;
        tok =  skip(tok, ",");
        ty = declarator(&tok, tok, basety);
      }
      continue;
    }

    // function
    if (ty->kind == TY_FUNC) {
      current_fn = new_gvar(get_identifier(ty->ident), ty, attr.is_static, false);
      if (!consume(&tok, tok, ";"))
        cur = cur->next = funcdef(&tok, start);
      continue;
    }

    // global variable = typespec declarator ("," declarator)* ";"
    for (;;) {
      if (!ty->ident)
        error_tok(ty->name_pos, "variable name omitted");
      Var *var = new_gvar(get_identifier(ty->ident), ty, attr.is_static, !attr.is_extern);
      if (attr.align)
        var->align = attr.align;

      if (consume(&tok, tok, "="))
        gvar_initializer(&tok, tok, var);

      if (consume(&tok, tok, ";"))
        break;
      tok =  skip(tok, ",");
      ty = declarator(&tok, tok, basety);
    }
  }

  Program *prog = new_program();
  prog->globals = globals;
  prog->fns = head.next;

  return prog;
}

// typespec = typename typename*
// typename = "void" | "_Bool" | "char" | "int" | "short" | "long" |
//            "struct" struct_dec | "union" union-decll
static Type *typespec(Token **rest, Token *tok, VarAttr *attr) {

  enum {
    VOID     = 1 << 0,
    BOOL     = 1 << 2,
    CHAR     = 1 << 4,
    SHORT    = 1 << 6,
    INT      = 1 << 8,
    LONG     = 1 << 10,
    OTHER    = 1 << 12,
    SIGNED   = 1 << 13,
    UNSIGNED = 1 << 14,
  };

  Type *ty = ty_int;
  int counter = 0;
  bool is_const = false;

  while (is_typename(tok)) {
    // handle storage class specifiers
    if (equal(tok, "typedef") || equal(tok, "static") || equal(tok, "extern")) {
      if (!attr)
        error_tok(tok, "storage class specifier is not allowed in this context");

      if (consume(&tok, tok, "typedef"))
        attr->is_typedef = true;
      else if(consume(&tok, tok, "static"))
        attr->is_static = true;
      else if(consume(&tok, tok, "extern"))
        attr->is_extern = true;
      else
        error_tok(tok, "internal error");

      if (attr->is_typedef + attr->is_static + attr->is_extern > 1)
        error_tok(tok, "typedef and static may not be used together");
      continue;
    }

    if (consume(&tok, tok, "const")) {
      is_const = true;
      continue;
    }

    if (consume(&tok, tok, "volatile")) {
      continue;
    }

    if (equal(tok, "_Alignas")) {
      if (!attr)
        error_tok(tok, "_Alignas is not allowed in this context");

      tok = skip(tok->next, "(");

      if (is_typename(tok))
        attr->align = typename(&tok, tok)->align;
      else
        attr->align = const_expr(&tok, tok);
      tok = skip(tok, ")");
      continue;
    }

    // Handle user-defined tyees
    Type *ty2 = lookup_typedef(tok);
    if (equal(tok, "struct") || equal(tok, "union") || equal(tok, "enum") || ty2) {
      if (counter)
        break;

      if (equal(tok, "struct")) {
        ty = struct_decl(&tok, tok->next);
      } else if (equal(tok, "union")) {
        ty = union_decl(&tok, tok->next);
      } else if (equal(tok, "enum")) {
        ty = enum_specifier(&tok, tok->next);
      } else {
        ty = ty2;
        expect_ident(&tok, tok);
      }

      counter += OTHER;
      continue;
    }

    // Handle built-in types.
    if (equal(tok, "void"))
      counter += VOID;
    else if (equal(tok, "_Bool"))
      counter += BOOL;
    else if (equal(tok, "char"))
      counter += CHAR;
    else if (equal(tok, "short"))
      counter += SHORT;
    else if (equal(tok, "int"))
      counter += INT;
    else if (equal(tok, "long"))
      counter += LONG;
    else if (equal(tok, "signed"))
      counter |= SIGNED;
    else if (equal(tok, "unsigned"))
      counter |= UNSIGNED;
    else
      error_tok(tok, "internal error");

    switch (counter) {
    case VOID:
      ty = ty_void;
      break;
    case BOOL:
      ty = ty_bool;
      break;
    case CHAR:
    case SIGNED + CHAR:
      ty = ty_char;
      break;
    case UNSIGNED + CHAR:
      ty = ty_uchar;
      break;
    case SHORT:
    case SHORT + INT:
    case SIGNED + SHORT:
    case SIGNED + SHORT + INT:
      ty = ty_short;
      break;
    case UNSIGNED + SHORT:
    case UNSIGNED + SHORT + INT:
      ty = ty_ushort;
      break;
    case INT:
    case SIGNED:
    case SIGNED + INT:
      ty = ty_int;
      break;
    case UNSIGNED:
    case UNSIGNED + INT:
      ty = ty_uint;
      break;
    case LONG:
    case LONG + INT:
    case LONG + LONG:
    case LONG + LONG + INT:
    case SIGNED + LONG:
    case SIGNED + LONG + INT:
    case SIGNED + LONG + LONG:
    case SIGNED + LONG + LONG + INT:
      ty = ty_long;
      break;
    case UNSIGNED + LONG:
    case UNSIGNED + LONG + INT:
    case UNSIGNED + LONG + LONG:
    case UNSIGNED + LONG + LONG + INT:
      ty = ty_ulong;
      break;
    default:
      error_tok(tok, "invalid type");
    }

    tok = tok->next;
  }

  if (is_const) {
    ty = copy_ty(ty);
    ty->is_const = true;
  }

  *rest = tok;
  return ty;
}

// array-dimensions = "[" const-expr? "]" type-suffix
static Type *array_dimensions(Token **rest, Token *tok, Type *ty) {
  tok = skip(tok, "[");
  if (equal(tok, "]")) {
    ty = type_suffix(rest, tok->next, ty);
    ty = array_of(ty, 0);
    ty->is_incomplete = true;
    return ty;
  }

  int sz = const_expr(&tok, tok);
  tok =  skip(tok, "]");
  ty = type_suffix(rest, tok, ty);  // first, define rightmost sub-array's size
  return array_of(ty, sz);          // this array composes of sz length of subarrays above
}

// type-suffix = "(" func-params ")"
//             | array-dimensions
//             | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
  if (consume(&tok, tok, "(")) {
    ty = func_params(&tok, tok, ty);
    tok =  skip(tok, ")");

    *rest = tok;
    return ty;
  }

  if (equal(tok, "["))
    return array_dimensions(rest, tok, ty);

  *rest = tok;
  return ty;
}

// pointers = ("*" "const"*)*
static Type *pointers(Token **rest, Token *tok, Type *ty) {

  while (consume(&tok, tok, "*")) {
    ty = pointer_to(ty);
    while (equal(tok, "const") || equal(tok, "volatile")) {
      if (equal(tok, "const"))
        ty->is_const = true;
      tok = tok->next;
    }
  }

  *rest = tok;
  return ty;
}

// declarator = pointers ("(" declarator ")" | ident?) type-suffix
static Type *declarator(Token **rest, Token *tok, Type *ty) {
  ty = pointers(&tok, tok, ty);

  if (consume(&tok, tok, "(")) {
    Type *placeholder = calloc(1, sizeof(Type));
    Type *new_ty = declarator(&tok, tok, placeholder);
    tok =  skip(tok, ")");
    *placeholder = *type_suffix(&tok, tok, ty);
    *rest = tok;
    return new_ty;
  }

  Token *name_pos = tok;
  Token *ident = NULL;

  if (tok->kind == TK_IDENT) {
    ident = tok;
    tok = tok->next;
  }

  ty = type_suffix(rest, tok, ty);
  ty->ident = ident;
  ty->name_pos = name_pos;

  return ty;
}

// abstract-declarator = pinters ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  ty = pointers(&tok, tok, ty);

  if (consume(&tok, tok, "(")) {
    Type *placeholder = calloc(1, sizeof(Type));
    Type *new_ty = abstract_declarator(&tok, tok, placeholder);
    tok =  skip(tok, ")");
    *placeholder = *type_suffix(rest, tok, ty);
    return new_ty;
  }

  return type_suffix(rest, tok, ty);
}

// type-name = typespec abstract-declarator
static Type *typename(Token **rest, Token *tok) {
  Type *ty = typespec(&tok, tok, NULL);
  return abstract_declarator(rest, tok, ty);
}

static bool is_end(Token *tok) {
  return equal(tok, "}") || (equal(tok, ",") && equal(tok->next, "}"));
}

static bool consume_end(Token **rest, Token *tok) {
  if (equal(tok, "}")) {
    *rest = tok->next;
    return true;
  }

  if (equal(tok, ",") && equal(tok->next, "}")) {
    *rest = tok->next->next;
    return true;
  }

  return false;
}

// enum-list = ident ("=" const-expr)? ("," ident ("=" const-expr)?)* ","?
static void register_enum_list(Token **rest, Token *tok, Type *ty) {
  int i = 0;
  int val = 0;
  tok = skip(tok, "{");

  while (!consume_end(rest, tok)) {
    if (i++ > 0)
      tok = skip(tok, ",");

    char *tag_name = expect_ident(&tok, tok);

    if (equal(tok, "="))
      val = const_expr(&tok, tok->next);

    VarScope *sc = push_scope(tag_name);
    sc->enum_ty = ty;
    sc->enum_val = val++;
  }
}

// enum-specifier = ident? "{" enum-list? "}"
//                | ident ("{" enum-list? "}")?
static Type *enum_specifier(Token **rest, Token *tok) {
  char *tag_name = NULL;
  Token *start = tok;

  // read a struct tag;
  if (tok->kind == TK_IDENT)
    tag_name = expect_ident(&tok, tok);

  if (tag_name && !equal(tok, "{")) {
    TagScope *sc = lookup_tag(tag_name);
    if (!sc)
      error_tok(start, "unknown enum type");
    if (sc->ty->kind != TY_ENUM)
      error_tok(start, "not an enum tag");
    *rest = tok;
    return sc->ty;
  }

  Type *ty = enum_type();
  register_enum_list(&tok, tok, ty);

  // Register the struct type if name is given
  if (tag_name)
    push_tag_scope(tag_name, ty);

  *rest = tok;
  return ty;
}

// declaration = typespec (declarator ( = expr)? ( "," declarator ( = expr)? )* )? ";"
static Node *declaration(Token **rest, Token *tok) {
  Node head = {};
  Node *cur = &head;
  Token *start_decl = tok;

  VarAttr attr = {0};
  Type *basety = typespec(&tok, tok, &attr);

  int cnt = 0;
  while(!consume(&tok, tok, ";")) {
    if (cnt++ > 0)
      tok =  skip(tok, ",");

    Token *start = tok;
    Type *ty = declarator(&tok, tok, basety);

    if (!ty->ident)
      error_tok(ty->name_pos, "variable declared void");
    if (ty->kind == TY_VOID)
      error_tok(start, "variable declared void");

    if (attr.is_typedef) {
      push_scope(get_identifier(ty->ident))->type_def = ty;
      continue;
    }

    if (attr.is_static) {
      // static local variable
      Var *var = new_gvar(new_gvar_name(), ty, true, true);
      push_scope(get_identifier(ty->ident))->var = var;

      if (equal(tok, "="))
        gvar_initializer(&tok, tok->next, var);
      continue;
    }

    Var *var = new_lvar(get_identifier(ty->ident), ty);
    if (attr.align)
      var->align = attr.align;

    if (consume(&tok, tok, "=")) {
      Node *expr = lvar_initializer(&tok, tok, var);
      cur = cur->next = new_node_unary(ND_EXPR_STMT, expr, tok);
    }
  }

  Node *blk = new_node(ND_BLOCK, start_decl);
  blk->body = head.next;

  *rest = tok;
  return blk;
}

static Token *skip_excess_elements(Token *tok) {
  while(!consume_end(&tok, tok)) {
    tok = skip(tok, ",");
    if (equal(tok, "{"))
      tok = skip_excess_elements(tok->next);
    else
      assign(&tok, tok);
  }
  return tok;
}

static Token *skip_end(Token *tok) {
  if (consume_end(&tok, tok))
    return tok;
  warn_tok(tok, "excess elements in initializer");
  return skip_excess_elements(tok);
}

// string-initializer = string-literal
static Initializer *string_initializer(Token **rest, Token *tok, Type *ty) {
  if (ty->is_incomplete) {
    ty->size = tok->cont_len;
    ty->array_len = tok->cont_len;
    ty->is_incomplete = false;
  }

  Initializer *init = new_init(ty, ty->array_len, NULL, tok);

  int len = (ty->array_len < tok->cont_len) ? ty->array_len : tok->cont_len;

  for (int i = 0; i < len; i++) {
    Node *expr = new_node_num(tok->contents[i], tok);
    init->children[i] = new_init(ty->base, 0, expr, tok);
  }
  *rest = tok->next;
  return init;
}

// array-initializer = "{" initializer ("," initializer)* ","? "}"
//                    | initializer ("," initializer)* ","?
static Initializer *array_initializer(Token **rest, Token *tok, Type *ty) {
  bool has_paren = consume(&tok, tok, "{");

  if (ty->is_incomplete) {
    int i = 0;
    for (Token *tok2 = tok; !is_end(tok2); i++) {
      if (i > 0)
        tok2 = skip(tok2, ",");
      initializer(&tok2, tok2, ty->base);
    }

    ty->size = size_of(ty->base) * i;
    ty->array_len= i;
    ty->is_incomplete = false;
  }

  Initializer *init = new_init(ty, ty->array_len, NULL, tok);

  for (int i = 0; i < ty->array_len && !is_end(tok); i++) {
    if (i > 0)
      tok = skip(tok, ",");
    init->children[i] = initializer(&tok, tok, ty->base);
  }

  if (has_paren)
    tok = skip_end(tok);
  *rest = tok;
  return init;
}

// struct-initializer = "{" initializer ("," initializer)* ","? "}"
//                    | initializer ("," initializer)* ","?
static Initializer *struct_initializer(Token **rest, Token *tok, Type *ty) {
  if (!equal(tok, "{")) {
    Token *tok2;
    Node *expr = assign(&tok2, tok);
    generate_type(expr);
    if (expr->ty->kind == TY_STRUCT) {
      Initializer *init = new_init(ty, 0, expr, tok);
      *rest = tok2;
      return init;
    }
  }

  int len = 0;
  for (Member *mem = ty->members; mem; mem = mem->next)
    len++;

  Initializer *init = new_init(ty, len, NULL, tok);
  bool has_paren = consume(&tok, tok, "{");

  int i = 0;
  for (Member *mem = ty->members; mem && !is_end(tok); mem = mem->next, i++) {
    if (i > 0)
      tok = skip(tok, ",");
    init->children[i] = initializer(&tok, tok, mem->ty);
  }

  if (has_paren)
    tok = skip_end(tok);
  *rest = tok;
  return init;
}

// initializer = string-initializer | array-initializer | struct-initializer
//             | "{" assign "}" | assign
static Initializer *initializer(Token **rest, Token *tok, Type *ty) {
  if (ty->kind == TY_ARRAY && ty->base->kind == TY_CHAR && tok->kind == TK_STR)
    return string_initializer(rest, tok, ty);

  if (ty->kind == TY_ARRAY)
    return array_initializer(rest, tok, ty);

  if (ty->kind == TY_STRUCT)
    return struct_initializer(rest, tok, ty);

  Token *start = tok;
  bool has_paren = consume(&tok, tok, "{");
  Initializer *init = new_init(ty, 0, assign(&tok, tok), start);
  if (has_paren)
    tok = skip_end(tok);

  *rest = tok;
  return init;
}

Node *init_desg_expr(InitDesg *desg, Token *tok) {
  if (desg->var)
    return new_node_var(desg->var, tok);

  if (desg->member) {
    Node *node = new_node_unary(ND_MEMBER, init_desg_expr(desg->next, tok), tok);
    node->member = desg->member;
    return node;
  }

  Node *lhs = init_desg_expr(desg->next, tok);
  Node *rhs = new_node_num(desg->idx, tok);

  return new_node_unary(ND_DEREF, new_node_add(lhs, rhs, tok), tok);
}

static Node *create_lvar_init(Initializer *init, Type *ty, InitDesg *desg, Token *tok) {
  if (ty->kind == TY_ARRAY) {
    Node *node = new_node(ND_NULL_EXPR, tok);

    int sz = size_of(ty->base); // FIXME: unsed variable?
    for (int i = 0; i < ty->array_len; i++) {
      InitDesg desg2 = {desg, i};
      Initializer *child = init ? init->children[i] : NULL;
      Node *rhs = create_lvar_init(child, ty->base, &desg2, tok);
      node = new_node_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  if (ty->kind == TY_STRUCT && (!init || init->len)) {
    Node *node = new_node(ND_NULL_EXPR, tok);

    int i = 0;
    for (Member *mem = ty->members; mem; mem = mem->next, i++) {
      InitDesg desg2 = {desg, 0, mem};
      Initializer *child = init ? init->children[i] : NULL;
      Node *rhs = create_lvar_init(child, mem->ty, &desg2, tok);
      node = new_node_binary(ND_COMMA, node, rhs, tok);
    }
    return node;
  }

  Node *lhs = init_desg_expr(desg, tok);
  Node *rhs = init ? init->expr : new_node_num(0, tok);
  Node *expr = new_node_binary(ND_ASSIGN, lhs, rhs, tok);
  expr->is_init = true;
  return expr;
}

static Node *lvar_initializer(Token **rest, Token *tok, Var *var) {
  Initializer *init = initializer(rest, tok, var->ty);
  InitDesg desg = {NULL, 0, NULL, var};
  return create_lvar_init(init, var->ty, &desg, tok);
}

// whether given token reprents a type
static bool is_typename(Token *tok) {
  static char *kw[] = {
    "void", "_Bool", "signed", "unsigned",
    "char", "short", "int", "long",
    "struct", "union", "typedef", "enum",
    "extern", "static", "_Alignas", "const", "volatile",
  };

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
    if (equal(tok, kw[i]))
      return true;

  return lookup_typedef(tok);
}

static void write_buf(char *buf, unsigned long val, int sz) {
  switch(sz) {
  case 1:
    *(unsigned char *)buf = val;
    return;
  case 2:
    *(unsigned short *)buf = val;
    return;
  case 4:
    *(unsigned int *)buf = val;
    return;
  default:
    assert(sz == 8);
    *(unsigned long *)buf = val;
    return;
  }
}

static Relocation *
write_gvar_data(Relocation *cur, Initializer *init, Type *ty, char *buf, int offset) {
  if (ty->kind == TY_ARRAY) {
    int sz = size_of(ty->base);
    for (int i = 0; i < ty->array_len; i++) {
      Initializer *child = init->children[i];
      if (child)
        cur = write_gvar_data(cur, child, ty->base, buf, offset + sz * i);
    }
    return cur;
  }

  if (ty->kind == TY_STRUCT) {
    int i = 0;
    for (Member *mem = ty->members; mem; mem = mem->next, i++) {
      Initializer *child = init->children[i];
      if (child)
        cur = write_gvar_data(cur, child, mem->ty, buf, offset + mem->offset);
    }
    return cur;
  }

  Var *var = NULL;
  long val = eval2(init->expr, &var);

  if (var) {
    Relocation *rel = calloc(1, sizeof(Relocation));
    rel->offset = offset;
    rel->label = var->name;
    rel->addend = val;
    cur->next = rel;
    return cur->next;
  }

  write_buf(buf + offset, val, size_of(ty));
  return cur;
}

// serializs Initializer objects to a flat byte array. initial values for
// gloval vars need to be evaluated at compile time to have embedded
// into .data section
static void gvar_initializer(Token **rest, Token *tok, Var *var) {
  Initializer *init = initializer(rest, tok, var->ty);

  Relocation head = {0};
  char *buf = calloc(1, size_of(var->ty));
  write_gvar_data(&head, init, var->ty, buf, 0);

  var->init_data = buf;
  var->rel = head.next;
}

// struct-union-members = (typespec declarator ("," declarator)* ";")*
static Member *struct_union_members(Token **rest, Token *tok) {
  Member head = {0};
  Member *cur = &head;

  while (!equal(tok, "}")) {
    VarAttr attr = {0};
    Type *basety = typespec(&tok, tok, &attr);
    int cnt = 0;

    while (!consume(&tok, tok, ";")) {
      if (cnt++)
        tok =  skip(tok, ",");

      Type *ty = declarator(&tok, tok, basety);
      Member *mem = new_member(get_identifier(ty->ident), ty);
      mem->align = attr.align ? attr.align : mem->ty->align;
      cur = cur->next = mem;
    }
  }

  *rest = tok;
  return head.next;
}

// struct-union-decl = ident? ("{" struct-union-members "}"
static Type *struct_union_decl(Token **rest, Token *tok) {
  // Read the tag (if any)
  char *tag_name = NULL;
  Token *start = tok;

  if (tok->kind == TK_IDENT)
    tag_name = expect_ident(&tok, tok);

  if (tag_name && !equal(tok, "{")) {
    *rest = tok;

    TagScope *sc = lookup_tag(tag_name);
    if (sc)
      return sc->ty;

    Type *ty = struct_type();
    ty->is_incomplete = true;
    push_tag_scope(tag_name, ty);
    return ty;
  }

  tok =  skip(tok, "{");
  Type *ty = struct_type();
  ty->members = struct_union_members(&tok, tok);
  *rest =  skip(tok, "}");

  if (tag_name) {
    // If this a redefinition, overwrite the previous type.
    // Otherwise register the struct type.
    TagScope *sc = lookup_tag(tag_name);
    if (sc && sc->depth == scope_depth) {
      *sc->ty = *ty;
      return sc->ty;
    }

    push_tag_scope(tag_name, ty);
  }

  return ty;
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);

  int offset = 0;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    offset = align_to(offset, mem->align);
    mem->offset = offset;
    offset += size_of(mem->ty);

    if (ty->align < mem->align)
      ty->align = mem->align;
  }
  ty->size = align_to(offset, ty->align);

  return ty;
}

// union-decl = struct-union-decl
static Type *union_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);

  // for union, all the offsets has to stay zero, meaning we
  // just need to leave these values unchanged after initialization.
  // Nonetheless alignment and size should be properly calculated.
  for (Member *mem = ty->members; mem; mem = mem->next) {
    if (ty->align < mem->align)
      ty->align = mem->align;
    if (ty->size < size_of(mem->ty))
      ty->size = size_of(mem->ty);
  }
  ty->size = align_to(ty->size, ty->align);

  return ty;
}

// funcdef = { block_stmt }
// TODO: consider poiter-type
static Function *funcdef(Token **rest, Token *tok) {
  locals = NULL;

  VarAttr attr = {0};
  Type *basety = typespec(&tok, tok, &attr);
  Type *ty = declarator(&tok, tok, basety);

  if (!ty->ident)
    error_tok(ty->name_pos, "function name omitted");

  Function *func = new_function(ty, &attr);

  enter_scope();
  for (Type *t = ty->params; t; t = t->next) {
    if (!t->ident)
      error_tok(t->name_pos, "parameter name omitted");
    Var *var = new_lvar(get_identifier(t->ident), t); // TODO: check if this registratoin order make sense (first defined comes first, latter could overshadow the earlier)
  }

  func->params = locals;

  func->node = block_stmt(rest, tok);
  func->locals = locals;

  leave_scope();
  return func;
}

// func-params = "void"
//             | "..."
//             | param ("," param)* ("," "...")?
//             | ε
// param = typespec declarator
static Type *func_params(Token **rest, Token *tok, Type *ty) {
  if (equal(tok, "void") && equal(tok->next, ")")) {
    *rest = tok->next;
    return func_returning(ty);
  }

  Type head = {0};
  Type *cur = &head;
  bool is_variadic = false;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok =  skip(tok, ",");

    if (consume(&tok, tok, "...")) {
      is_variadic = true;
      break;
    }

    Token *start = tok;
    Type *basety = typespec(&tok, tok, NULL);
    Type *ty2 = declarator(&tok, tok, basety);
    
    // "array of T" is converted to "pointer of T" only in parameter
    // context. example: *argv[] is converted to **argv by this.
    if (ty2->kind == TY_ARRAY) {
      Token *name = ty2->ident;
      ty2 = pointer_to(ty2->base);
      ty2->ident = name;
    }
    cur = cur->next = copy_ty(ty2);
  }

  ty = func_returning(ty);
  ty->params = head.next;
  ty->is_variadic = is_variadic;

  *rest = tok;
  return ty;
}

// block_stmt = stmt*
static Node *block_stmt(Token **rest, Token *tok) {
  Node head = {};
  Node *cur = &head;
  Token *start = tok;

  enter_scope();

  tok =  skip(tok, "{");
  while (!consume(&tok, tok, "}")) {
    if (is_typename(tok))
      cur = cur->next = declaration(&tok, tok);
    else
      cur = cur->next = stmt(&tok, tok);

    generate_type(cur);
  }

  leave_scope();

  Node *node = new_node(ND_BLOCK, start);
  node->body = head.next;

  *rest = tok;
  return node;
}

// stmt = if-stmt
//      | while-stmt
//      | do-stmt
//      | switch-stmt
//      | case-labeled-stmt
//      | default-labeled-stmt
//      | for-stmt
//      | "break" ";"
//      | "continue" ";"
//      | goto-stmt
//      | labeled-stmt
//      | return-stmt
//      | { block-stmt }
//      | expr-stmt
static Node *stmt(Token **rest, Token *tok) {
  Node *node;

  if (equal(tok, "if")) {
    node = if_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "switch")) {
    node = switch_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "case")) {
    node = case_labeled_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "default")) {
    node = default_labeled_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "while")) {
    node = while_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "do")) {
    node = do_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "for")) {
    node = for_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "break")) {
    *rest = skip(tok->next, ";");
    return new_node(ND_BREAK, tok);
  }

  if (equal(tok, "continue")) {
    *rest = skip(tok->next, ";");
    return new_node(ND_CONTINUE, tok);
  }

  if (equal(tok, "goto")) {
    return goto_stmt(rest, tok);
  }

  if (equal(tok, ";")) {
    Node *node = new_node(ND_BLOCK, tok);
    *rest = tok->next;
    return node;
  }

  if (tok->kind == TK_IDENT && equal(tok->next, ":")) {
    return labeled_stmt(rest, tok);
  }

  if (equal(tok, "return")) {
    node = return_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "{")) {
    node = block_stmt(rest, tok);
    return node;
  }

  node = expr_stmt(rest, tok);
  return node;
}

// if_stmt = "if" (cond) then_stmt ("else" els_stmt)
static Node *if_stmt(Token **rest, Token *tok) {
  Node *node;
  Token *start = tok;

  tok =  skip(tok, "if");
  node = new_node(ND_IF, start);
  tok =  skip(tok, "(");
  node->cond = expr(&tok, tok);
  tok =  skip(tok, ")");
  node->then = stmt(&tok, tok);
  if (consume(&tok, tok, "else"))
    node->els = stmt(&tok, tok);

  *rest = tok;
  return node;
}

// switch_stmt = "switch" "(" expr ")" stmt
static Node *switch_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_SWITCH, tok);

  tok =  skip(tok, "switch");
  tok =  skip(tok, "(");
  node->cond = expr(&tok, tok);
  tok =  skip(tok, ")");

  Node *prev_sw = current_switch;
  current_switch = node;
  node->then = stmt(rest, tok);
  current_switch = prev_sw;
  return node;
}

// case-labeled-stmt = "case" const-expr ":" stmt
static Node *case_labeled_stmt(Token **rest, Token *tok) {
  if (!current_switch)
    error_tok(tok, "stray case");

  Node *node = new_node(ND_CASE, tok);

  tok =  skip(tok, "case");
  int val = const_expr(&tok, tok);
  tok =  skip(tok, ":");
  node->lhs = stmt(rest, tok);
  node->val = val;
  node->case_next = current_switch->case_next;
  current_switch->case_next = node;
  return node;
}

// default-labeled-stmt = "default" ":" stmt
static Node *default_labeled_stmt(Token **rest, Token *tok) {
  if (!current_switch)
    error_tok(tok, "stray case");

  Node *node = new_node(ND_CASE, tok);

  tok =  skip(tok, "default");
  tok =  skip(tok, ":");
  node->lhs = stmt(rest, tok);

  current_switch->default_case = node;
  return node;
}

// while_stmt = "while" (cond) then_stmt
static Node *while_stmt(Token **rest, Token *tok) {
  Node *node;
  Token *start = tok;

  tok =  skip(tok, "while");
  node = new_node(ND_FOR, start);
  tok =  skip(tok, "(");
  node->cond = expr(&tok, tok);
  tok =  skip(tok, ")");
  node->then = stmt(&tok, tok);

  *rest = tok;
  return node;
}

// do_stmt = "do" stmt "while" "(" expr ")" ";"
static Node *do_stmt(Token **rest, Token *tok) {
  tok =  skip(tok, "do");

  Node *node = new_node(ND_DO, tok);
  node->then = stmt(&tok, tok);

  tok = skip(tok, "while");
  tok = skip(tok, "(");
  node->cond = expr(&tok, tok);
  tok = skip(tok, ")");
  *rest = skip(tok, ";");

  return node;
}

// for_stmt = "for" "(" (expr | declaration)? ";" expr? ";" expr? ")" then_stmt
static Node *for_stmt(Token **rest, Token *tok) {
  Node *node;
  Token *start = tok;

  tok =  skip(tok, "for");
  node = new_node(ND_FOR, start);
  tok =  skip(tok, "(");

  enter_scope();

  if (is_typename(tok)) {
    node->init = declaration(&tok, tok);
  } else {
    if(!consume(&tok, tok, ";")) {
      node->init = new_node_unary(ND_EXPR_STMT, expr(&tok, tok), tok);
      tok =  skip(tok, ";");
    }
  }
  if(!consume(&tok, tok, ";")) {
    node->cond = expr(&tok, tok);
    tok =  skip(tok, ";");
  }
  if(!consume(&tok, tok, ")")) {
    node->inc = new_node_unary(ND_EXPR_STMT, expr(&tok, tok), tok);
    tok =  skip(tok, ")");
  }
  node->then = stmt(&tok, tok);
  leave_scope();

  *rest = tok;
  return node;
}

// goto-stmt = "goto" ident ";"
static Node *goto_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_GOTO, tok);

  tok =  skip(tok, "goto");
  node->label_name = expect_ident(&tok, tok);
  *rest =  skip(tok, ";");
  return node;
}

// labeled-stmt = ident ":" stmt
static Node *labeled_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_LABEL, tok);

  node->label_name = expect_ident(&tok, tok);
  tok =  skip(tok, ":");
  node->lhs = stmt(rest, tok);
  return node;
}

// return_stmt = "return" expr? ";"
static Node *return_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_RETURN, tok);

  tok =  skip(tok, "return");
  if (consume(rest, tok, ";"))
    return node;

  Node *exp = expr(&tok, tok);
  *rest =  skip(tok, ";");

  generate_type(exp);
  node->lhs = new_node_cast(exp, current_fn->ty->return_ty);
  return node;
}

// expr_stmt
static Node *expr_stmt(Token **rest, Token *tok) {
  Node *node;
  Token *start = tok;

  node = new_node_unary(ND_EXPR_STMT, expr(&tok, tok), start);
  tok =  skip(tok, ";");

  *rest = tok;
  return node;
}

// expr = assign ( "," expr )?
static Node *expr(Token **rest, Token *tok) {
  Node *node = assign(&tok, tok);

  // supporting "generized lvalue" supported by past GCC (already deprecated)
  // implemeted for convenient use
  if (consume(&tok, tok, ","))
    node = new_node_binary(ND_COMMA, node, expr(&tok, tok), tok);

  *rest = tok;
  return node;
}

// evaluate a given node as a constant expression
static long eval(Node *node) {
  return eval2(node, NULL);
}

// evaluate a given node as a constant expression
// must be a number, or ptr + a (signed) number
// (latter form can be accepted for global vars initialization only)
static long eval2(Node *node, Var **var) {
  generate_type(node);
  
  switch (node->kind) {
    case ND_ADD:
      return eval2(node->lhs, var) + eval(node->rhs);
    case ND_SUB:
      return eval2(node->lhs, var) - eval(node->rhs);
    case ND_MUL:
      return eval(node->lhs) * eval(node->rhs);
    case ND_DIV:
      if (node->ty->is_unsigned)
        return (unsigned long)eval(node->lhs) / eval(node->rhs);
      return eval(node->lhs) / eval(node->rhs);
    case ND_MOD:
      return eval(node->lhs) % eval(node->rhs);

    case ND_BITAND:
      return eval(node->lhs) & eval(node->rhs);
    case ND_BITOR:
      return eval(node->lhs) | eval(node->rhs);
    case ND_BITXOR:
      return eval(node->lhs) ^ eval(node->rhs);
    case ND_LOGAND:
      return eval(node->lhs) && eval(node->rhs);
    case ND_LOGOR:
      return eval(node->lhs) || eval(node->rhs);

    case ND_SHL:
      return eval(node->lhs) << eval(node->rhs);
    case ND_SHR:
      if (node->ty->is_unsigned && size_of(node->ty) == 8)
        return (unsigned long)eval(node->lhs) >> eval(node->rhs);
      return eval(node->lhs) >> eval(node->rhs);

    case ND_EQ:
      return eval(node->lhs) == eval(node->rhs);
    case ND_NE:
      return eval(node->lhs) != eval(node->rhs);
    case ND_LT:
      if (node->ty->is_unsigned)
        return (unsigned long)eval(node->lhs) < eval(node->rhs);
      return eval(node->lhs) < eval(node->rhs);
    case ND_LE:
      if (node->ty->is_unsigned)
        return (unsigned long)eval(node->lhs) <= eval(node->rhs);
      return eval(node->lhs) <= eval(node->rhs);

    case ND_NOT:
      return !eval(node->lhs);
    case ND_BITNOT:
      return ~eval(node->lhs);

    case ND_COND:
      return eval(node->cond) ? eval(node->then) : eval(node->els);
    case ND_COMMA:
      return eval(node->rhs);

    case ND_CAST: {
      long val = eval2(node->lhs, var);
      if (!is_integer(node->ty) || size_of(node->ty) == 8)
        return val;

      switch(size_of(node->ty)) {
      case 1:
        if (node->ty->is_unsigned)
          return (unsigned char)val;
        return (char)val;
      case 2:
        if (node->ty->is_unsigned)
          return (unsigned short)val;
        return (short)val;
      default:
        assert(size_of(node->ty) == 4);
        if (node->ty->is_unsigned)
          return (unsigned int)val;
        return (int)val;
      }
    }
    case ND_NUM:
      return node->val;
    case ND_ADDR:
      if (!var || *var || node->lhs->kind != ND_VAR || node->lhs->var->is_local)
        error_tok(node->token, "invalid initializer");
      *var = node->lhs->var;
      return 0;
    case ND_VAR:
      if (!var || *var || node->var->ty->kind != TY_ARRAY)
        error_tok(node->token, "invalid initializer");
      *var = node->var;
      return 0;
  }

  error_tok(node->token, "not a constant expression");
}

static long const_expr(Token **rest, Token *tok) {
  Node *node = conditional(rest, tok);
  return eval(node);
}

// Convert 'A op= B' to 'tmp = &A, *tmp = *tmp op B'
// where tmp is a fresh pointer variable.
static Node *to_assign(Node *binary) {
  generate_type(binary->lhs);
  generate_type(binary->rhs);

  Var *var = new_lvar("", pointer_to(binary->lhs->ty));
  Token *tok = binary->token;

  Node *expr1 = new_node_binary(ND_ASSIGN, new_node_var(var, tok), new_node_unary(ND_ADDR, binary->lhs, tok), tok);
  Node *expr2 = new_node_binary(ND_ASSIGN, 
                                new_node_unary(ND_DEREF, new_node_var(var, tok), tok),
                                new_node_binary(binary->kind,
                                                new_node_unary(ND_DEREF, new_node_var(var, tok), tok),
                                                binary->rhs,
                                                tok),
                                tok);
  return new_node_binary(ND_COMMA, expr1, expr2, tok);
}

// assign = conditional (assign_op assign)?
// assign_op =  "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&="
//           | "|=" | "^=" | "<<=" | ">>="
static Node *assign(Token **rest, Token *tok) {
  Node *node = conditional(&tok, tok);

  if (consume(&tok, tok, "="))
    return new_node_binary(ND_ASSIGN, node, assign(rest, tok), tok);

  if (consume(&tok, tok, "+="))
    return to_assign(new_node_add(node, assign(rest, tok), tok));

  if (consume(&tok, tok, "-="))
    return to_assign(new_node_sub(node, assign(rest, tok), tok));

  if (consume(&tok, tok, "*="))
    return to_assign(new_node_binary(ND_MUL, node, assign(rest, tok), tok));

  if (consume(&tok, tok, "/="))
    return to_assign(new_node_binary(ND_DIV, node, assign(rest, tok), tok));

  if (consume(&tok, tok, "%="))
    return to_assign(new_node_binary(ND_MOD, node, assign(rest, tok), tok));

  if (consume(&tok, tok, "&="))
    return to_assign(new_node_binary(ND_BITAND, node, assign(rest, tok), tok));

  if (consume(&tok, tok, "|="))
    return to_assign(new_node_binary(ND_BITOR, node, assign(rest, tok), tok));

  if (consume(&tok, tok, "^="))
    return to_assign(new_node_binary(ND_BITXOR, node, assign(rest, tok), tok));

  if (consume(&tok, tok, "<<="))
    return to_assign(new_node_binary(ND_SHL, node, assign(rest, tok), tok));

  if (consume(&tok, tok, ">>="))
    return to_assign(new_node_binary(ND_SHR, node, assign(rest, tok), tok));

  *rest = tok;
  return node;
}

// conditional = logor ("?" expr ":" conditional)?
static Node *conditional(Token **rest, Token *tok) {
  Node *node = logor(&tok, tok);

  if (!equal(tok, "?")) {
    *rest = tok;
    return node;
  }

  Node *cond = new_node(ND_COND, tok);
  cond->cond = node;
  cond->then = expr(&tok, tok->next);
  tok = skip(tok, ":");
  cond->els = conditional(rest, tok);

  return cond;
}

// logor  = logand ("||" logand)*
static Node *logor(Token **rest, Token *tok) {
  Node *node = logand(&tok, tok);
  while (consume(&tok, tok, "||")) {
    Token *start = tok;
    node = new_node_binary(ND_LOGOR, node, logand(&tok, tok), start);
  }
  *rest = tok;
  return node;
}

// logand = bitor ("&&" bitor)*
static Node *logand(Token **rest, Token *tok) {
  Node *node = bitor(&tok, tok);
  while (consume(&tok, tok, "&&")) {
    Token *start = tok;
    node = new_node_binary(ND_LOGAND, node, bitor(&tok, tok), start);
  }
  *rest = tok;
  return node;
}

// bitor  = bitxor ("|" bitxor)*
static Node *bitor(Token **rest, Token *tok) {
  Node *node = bitxor(&tok, tok);
  while (consume(&tok, tok, "|")) {
    Token *start = tok;
    node = new_node_binary(ND_BITOR, node, bitxor(&tok, tok), start);
  }
  *rest = tok;
  return node;
}

// bitxor  = bitand ("^" bitand)*
static Node *bitxor(Token **rest, Token *tok) {
  Node *node = bitand(&tok, tok);
  while (consume(&tok, tok, "^")) {
    Token *start = tok;
    node = new_node_binary(ND_BITXOR, node, bitand(&tok, tok), start);
  }
  *rest = tok;
  return node;
}

// bitand  = equality ("^" equality)*
static Node *bitand(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);
  while (consume(&tok, tok, "&")) {
    Token *start = tok;
    node = new_node_binary(ND_BITAND, node, equality(&tok, tok), start);
  }
  *rest = tok;
  return node;
}

// equality = relational ('==' relational | '!=' relational)*
static Node *equality(Token **rest, Token *tok) {
  Node *node = relational(&tok, tok);
  Token *start = tok;

  for(;;) {
    if (consume(&tok, tok, "==")) {
      node = new_node_binary(ND_EQ, node, relational(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, "!=")) {
      node = new_node_binary(ND_NE, node, relational(&tok, tok), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// relational = shift ("<" shift | "<=" shift | ">" shift | ">=" shift)*
static Node *relational(Token **rest, Token *tok) {
  Node *node = shift(&tok, tok);
  Token *start = tok;

  for(;;) {
    if (consume(&tok, tok, "<")) {
      node = new_node_binary(ND_LT, node, shift(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, "<=")) {
      node = new_node_binary(ND_LE, node, shift(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, ">")) {
      node = new_node_binary(ND_LT, shift(&tok, tok), node, start);
      continue;
    }
    if (consume(&tok, tok, ">=")) {
      node = new_node_binary(ND_LE, shift(&tok, tok), node, start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// shift = add ("<<" add | ">>" add)*
static Node *shift(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);

  for(;;) {
    Token *start = tok;

    if (consume(&tok, tok, "<<")) {
      node = new_node_binary(ND_SHL, node, add(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, ">>")) {
      node = new_node_binary(ND_SHR, node, add(&tok, tok), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// add = mul ("+" mul | "-" mul)*
static Node *add(Token **rest, Token *tok) {
  Node *node = mul(&tok, tok);
  Token *start = tok;

  for(;;) {
    if (consume(&tok, tok, "+")) {
      node = new_node_add(node, mul(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, "-")) {
      node = new_node_sub(node, mul(&tok, tok), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// mul = cast ("*" cast | "/" cast | "%" cast)*
static Node *mul(Token **rest, Token *tok) {
  Node *node = cast(&tok, tok);
  Token *start = tok;

  for(;;) {
    if (consume(&tok, tok, "*")) {
      node = new_node_binary(ND_MUL, node, cast(&tok, tok), start);
      continue;
    }
    else if (consume(&tok, tok, "/")) {
      node = new_node_binary(ND_DIV, node, cast(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, "%")) {
      node = new_node_binary(ND_MOD, node, cast(&tok, tok), start);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// compound-literal = "{" initializer "}"
static Node *compound_literal(Token **rest, Token *tok, Type *ty, Token *start) {
  if (scope_depth == 0) {
    Var *var = new_gvar(new_gvar_name(), ty, true, true);
    gvar_initializer(rest, tok, var);
    return new_node_var(var, start);
  }

  Var *var = new_lvar(new_gvar_name(), ty);
  Node *lhs = lvar_initializer(rest, tok, var);
  Node *rhs = new_node_var(var, tok);
  return new_node_binary(ND_COMMA, lhs, rhs, tok);
}

// cast = "(" typename ")" "{" compound-literal "}"
//      | "(" typename ")" cast
//      | unary
static Node *cast(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    Token *start = tok;
    Type *ty = typename(&tok, tok->next);
    tok = skip(tok, ")");

    if (equal(tok, "{"))
      return compound_literal(rest, tok, ty, start);

    Node *node = new_node_unary(ND_CAST, cast(rest, tok), tok);
    generate_type(node->lhs);
    node->ty = ty;

    return node;
  }

  return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" | "~") cast
//       | ("++" | "--") unary
//       | postfix
static Node *unary(Token **rest, Token *tok) {
  Token *start = tok;

  if (equal(tok, "+"))
    return cast(rest, tok->next);
  if (equal(tok, "-"))
    return new_node_binary(ND_SUB, new_node_num(0, start), cast(rest, tok->next), start);
  if (equal(tok, "&"))
    return new_node_unary(ND_ADDR, cast(rest, tok->next), start);
  if (equal(tok, "*"))
    return new_node_unary(ND_DEREF, cast(rest, tok->next), start);
  if (equal(tok, "!"))
    return new_node_unary(ND_NOT, cast(rest, tok->next), start);
  if (equal(tok, "~"))
    return new_node_unary(ND_BITNOT, cast(rest, tok->next), start);
  if (equal(tok, "++"))
    return to_assign(new_node_add(unary(rest, tok->next), new_node_num(1, tok), tok));
  if (equal(tok, "--"))
    return to_assign(new_node_sub(unary(rest, tok->next), new_node_num(1, tok), tok));

  return postfix(rest, tok);
}

static Member *get_struct_member(Type *ty, Token *tok) {
  for (Member *mem = ty->members; mem; mem = mem->next)
    if (!strncmp(mem->name, tok->str, tok->len))
      return mem;
  error_tok(tok, "no such member");
}

static Node *struct_ref(Token **rest, Token *tok, Node *lhs) {
  generate_type(lhs);
  if (lhs->ty->kind != TY_STRUCT)
    error_tok(lhs->token, "not a struct");

  Node *node = new_node_unary(ND_MEMBER, lhs, tok);
  node->member = get_struct_member(lhs->ty, tok);
  return node;
}

// Convert A++ (A--) to 'tmp = &A, *tmp = *tmp + 1 (- 1), *tmp - 1 (+ 1)'
// where tmp is a fresh pointer variable.
static Node *new_inc_dec(Node *node, Token *tok, int addend) {
  generate_type(node);

  Var *var = new_lvar("", pointer_to(node->ty));
//  Token *tok = binary->token;

  Node *expr1 = new_node_binary(ND_ASSIGN,
                                new_node_var(var, tok),
                                new_node_unary(ND_ADDR, node, tok),
                                tok);
  Node *expr2 = new_node_binary(ND_ASSIGN, 
                                new_node_unary(ND_DEREF, new_node_var(var, tok), tok),
                                new_node_add(new_node_unary(ND_DEREF, new_node_var(var, tok), tok),
                                             new_node_num(addend, tok),
                                             tok),
                                tok);
  Node *expr3 = new_node_add(new_node_unary(ND_DEREF, new_node_var(var, tok), tok),
                             new_node_num(-addend, tok),
                              tok);
  return new_node_binary(ND_COMMA, expr1, new_node_binary(ND_COMMA, expr2, expr3, tok), tok);
}

// postfix = ident "(" func-args ")" postfix-tail*
//         | primary postfix-tail*
//
// postfix-tail = "[" expr "]"
//              | "(" func-args ")"
//              | "." ident
//              | "->" ident
//              | "++"
//              | "--"
static Node *postfix(Token **rest, Token *tok) {

  Token *start = tok;
  Node *node = primary(&tok, tok);

  for (;;) {
    if (equal(tok, "("))  {
      node = funcall(&tok, tok, node);
      continue;
    }

    if (consume(&tok, tok, "["))  {
      // x[y] is syntax sugar for *(x + y)
      Node *idx = expr(&tok, tok);
      tok =  skip(tok, "]");
      node = new_node_unary(ND_DEREF, new_node_add(node, idx, start), start);
      continue;
    }

    if (consume(&tok, tok, ".")) {
      node = struct_ref(&tok, tok, node);
      expect_ident(&tok, tok);
      continue;
    }

    if (consume(&tok, tok, "->")) {
      node = new_node_unary(ND_DEREF, node, start);
      node = struct_ref(&tok, tok, node);
      expect_ident(&tok, tok);
      continue;
    }

    if (consume(&tok, tok, "++")) {
      node = new_inc_dec(node, tok, 1);
      continue;
    }

    if (consume(&tok, tok, "--")) {
      node = new_inc_dec(node, tok, -1);
      continue;
    }

    *rest = tok;
    return node;
  }
}

// primary = "(" "{" stmt stmt* "}" ")"
//           | "(" expr ")"
//           | "sizeof" "(" typename ")"
//           | "sizeof" unary
//           | "alignof" "(" typename ")"
//           | ident
//           | str
//           | num
static Node *primary(Token **rest, Token *tok) {
  Token *start = tok;

  if (consume(&tok, tok, "(")) {
    if (equal(tok, "{")) {
      Node *node = new_node(ND_STMT_EXPR, start);
      node->body = block_stmt(&tok, tok)->body;
      tok =  skip(tok, ")");

      Node *cur = node->body;
      while(cur && cur->next)
        cur = cur->next;

      if (!cur || cur->kind != ND_EXPR_STMT)
        error_tok(start, "statement expression returning void is not supported");
      *rest = tok;
      return node;
    }

    Node *node = expr(&tok, tok);
    tok =  skip(tok, ")");
    *rest = tok;
    return node;
  }

  if (tok->kind == TK_IDENT) {
    // variable or enum constant
    Token *start = tok;
    char *name = expect_ident(rest, tok);

    VarScope *sc = lookup_var(name);

    if (sc) {
      if (sc->var)
        return new_node_var(sc->var, start);
      if (sc->enum_ty)
        return new_node_num(sc->enum_val, start);
    }

    if (equal(tok->next, "(")) {
      warn_tok(start, "implicit declaration of a function");
      Var *var = new_gvar(name, func_returning(ty_int), true, false);
      return new_node_var(var, start);
    }

    error_tok(start, "undefined variable");
  }

  if (consume(&tok, tok, "sizeof")) {
    if (equal(tok, "(") && is_typename(tok->next)) {
      Type *ty = typename(&tok, tok->next);
      *rest = skip(tok, ")");
      return new_node_num_ulong(size_of(ty), tok);
    }

    Node *node = unary(&tok, tok);
    generate_type(node);
    *rest = tok;
    return new_node_num_ulong(size_of(node->ty), start);
  }

  if (consume(&tok, tok, "alignof")) {
    tok = skip(tok, "(");
    Type *ty = typename(&tok, tok);
    *rest = skip(tok, ")");
    return new_node_num_ulong(ty->align, tok);
  }

  if (tok->kind == TK_STR) {
    Var *var = new_string_literal(tok->contents, tok->cont_len);
    expect_string(&tok, tok);
    *rest = tok;
    return new_node_var(var, start);
  }

  Type *num_ty = tok->ty;
  Node *node = new_node_num(expect_number(rest, tok), start);
  node->ty = num_ty;

  return node;
}

// funcall = "(" arg-list ")"
// arg-list = (assign ("," assign)*)?
//
// foo(a, b, c) is compiled to ({t1=a; t2=b; t3=c; foo(t1, t2, t3)})
// where t1, t2, and t3 are fresh (unnamed) local vars.
static Node *funcall(Token **rest, Token *tok, Node *fn) {
  Token *start = tok;
  generate_type(fn);

  if (fn->ty->kind != TY_FUNC &&
      !(fn->ty->kind == TY_PTR && fn->ty->base->kind == TY_FUNC))
    error_tok(start, "not a function");

  Node *node = new_node(ND_NULL_EXPR, tok);
  Var **args = NULL;
  int nargs = 0;
  Type *ty = (fn->ty->kind == TY_FUNC) ? fn->ty : fn->ty->base;
  Type *param_ty = ty->params;

  tok = skip(tok, "(");

  while (!equal(tok, ")")) {
    if (nargs)
      tok =  skip(tok, ",");

    Node *arg = assign(&tok, tok);
    generate_type(arg);

    if (param_ty) {
      arg = new_node_cast(arg, param_ty);
      param_ty = param_ty->next;
    }

    Var *var = arg->ty->base
             ? new_lvar("", pointer_to(arg->ty->base))
             : new_lvar("", arg->ty);

    args = realloc(args, sizeof(*args) * (nargs + 1));
    args[nargs] = var;
    nargs++;

    Node *expr = new_node_binary(ND_ASSIGN, new_node_var(var, tok), arg, tok);
    node = new_node_binary(ND_COMMA, node, expr, tok);
  }
  *rest = skip(tok, ")");

  Node *funcall = new_node_unary(ND_FUNCALL, fn, start);
  funcall->func_ty = ty;
  funcall->ty = ty->return_ty;
  funcall->args = args;
  funcall->nargs = nargs;

  return new_node_binary(ND_COMMA, node, funcall, start);
}

