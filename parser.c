#include "9cc.h"

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
} VarAttr;

// Scope for struct, union or enum tags
typedef struct TagScope TagScope;
struct TagScope {
  TagScope *next;
  char *name;
  int depth;
  Type *ty;
};

Var *locals;
Var *globals;

// C has two block scopes: one for variables/typedefs and
// the other for struct/union/enum tags.
static VarScope *var_scope;
static TagScope *tag_scope;

static int scope_depth;
static Var *current_fn;

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
  var->ty= ty;
  return var;
}

static Member *new_member(char *name, Type *ty) {
  Member *mem = calloc(1, sizeof(Member));
  mem->name = name;
  mem->ty= ty;
  return mem;
}

static Var *new_lvar(char *name, Type *ty) {
  Var *var = new_var(name, ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  push_scope(name)->var = var;
  return var;
}

static Var *new_gvar(char *name, Type *ty, bool emit) {
  Var *var = new_var(name, ty);
  var->is_local = false;
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
  Var *var = new_gvar(new_gvar_name(), ty, true);
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

static Node *new_node_num(int val, Token *tok) {
  Node *node = new_node(ND_NUM, tok);
  node->val = val;
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

static bool is_typename(Token *tok);
static Type *typespec(Token **rest, Token *tok, VarAttr *attr);
static void register_enum_list(Token **rest, Token *tok, Type *ty);
static Type *enum_specifier(Token **rest, Token *tok);
static Type *declarator(Token **rest, Token *tok, Type *base);
static Node *declaration(Token **rest, Token *tok);

static Function *funcdef(Token **rest, Token *tok);
static Type *func_params(Token **rest, Token *tok);
static Type *struct_decl(Token **rest, Token *tok);
static Type *union_decl(Token **rest, Token *tok);

static Node *block_stmt(Token **rest, Token *tok);
static Node *stmt(Token **rest, Token *tok);

static Node *if_stmt(Token **rest, Token *tok);
static Node *while_stmt(Token **rest, Token *tok);
static Node *for_stmt(Token **rest, Token *tok);
static Node *return_stmt(Token **rest, Token *tok);
static Node *expr_stmt(Token **rest, Token *tok);

static Node *expr(Token **rest, Token *tok);
static Node *assign(Token **rest, Token *tok);
static Node *equality(Token **rest, Token *tok);
static Node *relational(Token **rest, Token *tok);
static Node *add(Token **rest, Token *tok);
static Node *mul(Token **rest, Token *tok);
static Node *cast(Token **rest, Token *tok);
static Node *unary(Token **rest, Token *tok);
static Node *postfix(Token **rest, Token *tok);
static Node *primary(Token **rest, Token *tok);
static Node *funcall(Token **rest, Token *tok);
static Node *arg_list(Token **rest, Token *tok, Type *param_ty);

// program = (funcdef | global-var)*
Program *parse(Token *tok) {

  Function head = {0};
  Function *cur = &head;
  globals = NULL;

  while (tok->kind != TK_EOF) {
    Token *start = tok;
    VarAttr attr = {0};
    Type *basety = typespec(&tok, tok, &attr);
    Type *ty = declarator(&tok, tok, basety);

    // typedef
    // "typedef" basety foo[3], *bar, ..
    if (attr.is_typedef) {
      for(;;) {
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
      current_fn = new_gvar(get_identifier(ty->ident), ty, false);
      if (!consume(&tok, tok, ";"))
        cur = cur->next = funcdef(&tok, start);
      continue;
    }

    // global variable = typespec declarator ("," declarator)* ";"
    for (;;) {
      new_gvar(get_identifier(ty->ident), ty, true);
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
    VOID  = 1 << 0,
    BOOL  = 1 << 2,
    CHAR  = 1 << 4,
    SHORT = 1 << 6,
    INT   = 1 << 8,
    LONG  = 1 << 10,
    OTHER = 1 << 12,
  };

  Type *ty = ty_int;
  int counter = 0;

  while (is_typename(tok)) {
    // handle storage class specifiers
    if (equal(tok, "typedef") || equal(tok, "static")) {
      if (!attr)
        error_tok(tok, "storage class specifier is not allowed in this context");

      if (consume(&tok, tok, "typedef"))
        attr->is_typedef = true;
      else if(consume(&tok, tok, "static"))
        attr->is_static = true;
      else
        error_tok(tok, "internal error");

      if (attr->is_typedef + attr->is_static > 1)
        error_tok(tok, "typedef and static may not be used together");
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
      ty = ty_char;
      break;
    case SHORT:
    case SHORT + INT:
      ty = ty_short;
      break;
    case INT:
      ty = ty_int;
      break;
    case LONG:
    case LONG + INT:
    case LONG + LONG:
    case LONG + LONG + INT:
      ty = ty_long;
      break;
    default:
      error_tok(tok, "invalid type");
    }

    tok = tok->next;
  }

  *rest = tok;
  return ty;
}

// type-suffix = "(" func-params ")"
//             | "[" num "]" type-suffix
//             | ε
static Type *type_suffix(Token **rest, Token *tok, Type *ty) {
  if (consume(&tok, tok, "(")) {
    ty = func_returning(ty);
    ty->params = func_params(&tok, tok);
    tok =  skip(tok, ")");

    *rest = tok;
    return ty;
  }
  if (consume(&tok, tok, "[")) {
    int sz = expect_number(&tok, tok);
    tok =  skip(tok, "]");
    ty = type_suffix(&tok, tok, ty);  // first, define rightmost sub-array's size
    ty = array_of(ty, sz); // this array composes of sz length of subarrays above
    *rest = tok;
    return ty;
  }

  *rest = tok;
  return ty;
}

// declarator = "*"* ("(" declarator ")" | ident) type-suffix
static Type *declarator(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*"))
    ty = pointer_to(ty);

  if (consume(&tok, tok, "(")) {
    Type *placeholder = calloc(1, sizeof(Type));
    Type *new_ty = declarator(&tok, tok, placeholder);
    tok =  skip(tok, ")");
    *placeholder = *type_suffix(&tok, tok, ty);
    *rest = tok;
    return new_ty;
  }

  Token *ident = tok;
  expect_ident(&tok, tok);

  ty = type_suffix(rest, tok, ty);
  ty->ident = ident;

  return ty;
}

// abstract-declarator = "*" ("(" abstract-declarator ")")? type-suffix
static Type *abstract_declarator(Token **rest, Token *tok, Type *ty) {
  while (consume(&tok, tok, "*"))
    ty = pointer_to(ty);

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

// enum-list      = ident ("=" num)? ("'" ident ("=" num)?)*
static void register_enum_list(Token **rest, Token *tok, Type *ty) {
  int i = 0;
  int val = 0;

  while (!equal(tok, "}")) {
    if (i++ > 0)
      tok = skip(tok, ",");

    char *tag_name = expect_ident(&tok, tok);

    if (equal(tok, "="))
      val = expect_number(&tok, tok->next);

    VarScope *sc = push_scope(tag_name);
    sc->enum_ty = ty;
    sc->enum_val = val++;
  }

  *rest = tok;
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

  tok =  skip(tok, "{");
  Type *ty = enum_type();
  register_enum_list(&tok, tok, ty);
  tok =  skip(tok, "}");

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

  while(!consume(&tok, tok, ";")) {
    if (cur != &head)
      tok =  skip(tok, ",");

    Token *start = tok;
    Type *ty = declarator(&tok, tok, basety);
    if (ty->kind == TY_VOID)
      error_tok(start, "variable declared void");

    if (attr.is_typedef) {
      push_scope(get_identifier(ty->ident))->type_def = ty;
      continue;
    }

    Var *var = new_lvar(get_identifier(ty->ident), ty);

    Node *node = new_node_var(var, tok);
    if (consume(&tok, tok, "="))
      node = new_node_binary(ND_ASSIGN, node, assign(&tok, tok), start);

    cur = cur->next = new_node_unary(ND_EXPR_STMT, node, start);
  }

  Node *blk = new_node(ND_BLOCK, start_decl);
  blk->body = head.next;

  *rest = tok;
  return blk;
}

// whether given token reprents a type
static bool is_typename(Token *tok) {
  static char *kw[] = {
    "static", "void", "_Bool", "char", "short", "int", "long",
    "struct", "union", "typedef", "enum",
  };

  for (int i = 0; i < sizeof(kw) / sizeof(*kw); i++)
    if (equal(tok, kw[i]))
      return true;

  return lookup_typedef(tok);
}

// struct-union-members = (typespec declarator ("," declarator)* ";")*
static Member *struct_union_members(Token **rest, Token *tok) {
  Member head = {0};
  Member *cur = &head;

  while (!equal(tok, "}")) {
    Type *basety = typespec(&tok, tok, NULL);
    int cnt = 0;

    while (!consume(&tok, tok, ";")) {
      if (cnt++)
        tok =  skip(tok, ",");

      Type *ty = declarator(&tok, tok, basety);
      Member *mem = new_member(get_identifier(ty->ident), ty);
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
    TagScope *sc = lookup_tag(tag_name);
    if (!sc)
      error_tok(start, "unknown struct type");
    *rest = tok;
    return sc->ty;
  }

  tok =  skip(tok, "{");
  Type *ty = new_type(TY_STRUCT, 0, 0);
  ty->members = struct_union_members(&tok, tok);
  tok =  skip(tok, "}");

  // Register the struct type if name is given
  if (tag_name)
    push_tag_scope(tag_name, ty);

  *rest = tok;
  return ty;
}

// struct-decl = struct-union-decl
static Type *struct_decl(Token **rest, Token *tok) {
  Type *ty = struct_union_decl(rest, tok);

  int offset = 0;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    offset = align_to(offset, mem->ty->align);
    mem->offset = offset;
    offset += size_of(mem->ty);

    if (ty->align < mem->ty->align)
      ty->align = mem->ty->align;
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
    if (ty->align < mem->ty->align)
      ty->align = mem->ty->align;
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
  Function *func = new_function(ty, &attr);

  enter_scope();
  for (Type *t = ty->params; t; t = t->next) {
    Var *var = new_lvar(get_identifier(t->ident), t); // TODO: check if this registratoin order make sense (first defined comes first, latter could overshadow the earlier)
  }

  func->params = locals;

  func->node = block_stmt(rest, tok);
  func->locals = locals;

  leave_scope();
  return func;
}

// func-params = param, ("," param)*
// param = typespec declarator
static Type *func_params(Token **rest, Token *tok) {
  Type head = {0};
  Type *cur = &head;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok =  skip(tok, ",");

    Token *start = tok;
    Type *basety = typespec(&tok, tok, NULL);
    Type *ty = declarator(&tok, tok, basety);
    cur = cur->next = copy_ty(ty);
  }

  *rest = tok;
  return head.next;
}

// block_stmt = stmt*
// TODO: allow blocks to have their own local vars
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

// stmt = if_stmt | return_stmt | { block_stmt } | expr_stmt
static Node *stmt(Token **rest, Token *tok) {
  Node *node;

  if (equal(tok, "if")) {
    node = if_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "while")) {
    node = while_stmt(rest, tok);
    return node;
  }

  if (equal(tok, "for")) {
    node = for_stmt(rest, tok);
    return node;
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

// return_stmt = "return" expr
static Node *return_stmt(Token **rest, Token *tok) {
  Node *node = new_node(ND_RETURN, tok);

  tok =  skip(tok, "return");
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

// assign = equality (assign_op assign)?
// assign_op = "=" | "+=" | "-=" | "*=" | "/="
static Node *assign(Token **rest, Token *tok) {
  Node *node = equality(&tok, tok);

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

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational(Token **rest, Token *tok) {
  Node *node = add(&tok, tok);
  Token *start = tok;

  for(;;) {
    if (consume(&tok, tok, "<")) {
      node = new_node_binary(ND_LT, node, add(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, "<=")) {
      node = new_node_binary(ND_LE, node, add(&tok, tok), start);
      continue;
    }
    if (consume(&tok, tok, ">")) {
      node = new_node_binary(ND_LT, add(&tok, tok), node, start);
      continue;
    }
    if (consume(&tok, tok, ">=")) {
      node = new_node_binary(ND_LE, add(&tok, tok), node, start);
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

// mul = cast ("*" cast | "/" cast)*
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

    *rest = tok;
    return node;
  }
}

// cast = "(" typename ")" cast | unary
static Node *cast(Token **rest, Token *tok) {
  if (equal(tok, "(") && is_typename(tok->next)) {
    Node *node = new_node(ND_CAST, tok);
    node->ty = typename(&tok, tok->next);
    tok = skip(tok, ")");
    node->lhs = cast(rest, tok);
    generate_type(node->lhs);
    return node;
  }

  return unary(rest, tok);
}

// unary = ("+" | "-" | "*" | "&" | "!" )? cast
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

// postfix = primary ("[" expr "]" | "." ident | "->" ident | "++" | "--")*
static Node *postfix(Token **rest, Token *tok) {

  Token *start = tok;
  Node *node = primary(&tok, tok);

  for (;;) {
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
//           | ident ("(" arg-list ")")?
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
    if (equal(tok->next, "("))
      return funcall(rest, tok);

    // variable or enum constant
    Token *start = tok;
    char *name = expect_ident(rest, tok);

    VarScope *sc = lookup_var(name);
    if (!sc || (!sc->var && !sc->enum_ty))
      error_tok(start, "undefined variable");

    Node *node;
    if (sc->var)
      node = new_node_var(sc->var, start);
    else
      node = new_node_num(sc->enum_val, start);

    return node;
  }

  if (consume(&tok, tok, "sizeof")) {
    if (equal(tok, "(") && is_typename(tok->next)) {
      Type *ty = typename(&tok, tok->next);
      *rest = skip(tok, ")");
      return new_node_num(size_of(ty), tok);
    }

    Node *node = unary(&tok, tok);
    generate_type(node);
    *rest = tok;
    return new_node_num(size_of(node->ty), start);
  }

  if (tok->kind == TK_STR) {
    Var *var = new_string_literal(tok->contents, tok->cont_len);
    expect_string(&tok, tok);
    *rest = tok;
    return new_node_var(var, start);
  }

  return new_node_num(expect_number(rest, tok), start);
}

// funcall = ident "(" arg-list ")"
static Node *funcall(Token **rest, Token *tok) {
  Token *start = tok;
  char *name = expect_ident(&tok, tok);

  VarScope *sc = lookup_var(name);
  Type *ty;
  if (sc) {
    if (!sc->var || sc->var->ty->kind != TY_FUNC)
      error_tok(start, "not a function");
    ty = sc->var->ty;
  } else {
    warn_tok(start, "implicit declaration of a function");
    ty = func_returning(ty_int);
  }

  tok = skip(tok, "(");
  Node *funcall = new_node(ND_FUNCALL, start);
  funcall->funcname = name;
  funcall->func_ty = ty;
  funcall->ty = ty->return_ty;
  funcall->args = arg_list(&tok, tok, ty->params);
  tok =  skip(tok, ")");

  *rest = tok;
  return funcall;
}

// arg-list = (assign (, assign)*)?
static Node *arg_list(Token **rest, Token *tok, Type *param_ty) {
  Node head = {0};
  Node *cur = &head;

  while (!equal(tok, ")")) {
    if (cur != &head)
      tok =  skip(tok, ",");
    Node *arg = assign(&tok, tok);
    generate_type(arg);

    if (param_ty) {
      arg = new_node_cast(arg, param_ty);
      param_ty = param_ty->next;
    }

    cur = cur->next = arg;
  }

  *rest = tok;
  return head.next;
}
