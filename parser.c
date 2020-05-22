#include "9cc.h"

//
// Parser
//

// Scope for local or global vars (incl. string literal)
typedef struct VarScope VarScope;
struct VarScope {
  VarScope *next;
  char *name;
  int depth;
  Var *var;
};

// Scope for struct tags
typedef struct TagScope TagScope;
struct TagScope {
  TagScope *next;
  char *name;
  int depth;
  Type *ty;
};

Var *locals;
Var *globals;

static VarScope *var_scope;
static TagScope *tag_scope;
static int scope_depth;

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

static VarScope *push_scope(char *name, Var *var) {
  VarScope *sc = calloc(1, sizeof(VarScope));
  sc->name = name;
  sc->var = var;
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

static Function *new_function(Type *ty) {
  Function *func = calloc(1, sizeof(Function));
  func->name = ty->identifier;
  // TODO: consider return type: func->return_ty = ty;
  return func;
}

static Node *new_node(NodeKind kind, Token *token) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  node->token = token;
  return node;
}

static Var *new_var(Type *ty) {
  Var *var = calloc(1, sizeof(Var));
  var->name = ty->identifier;
  var->ty= ty;
  return var;
}

static Member *new_member(Type *ty) {
  Member *mem = calloc(1, sizeof(Member));
  mem->name = ty->identifier;
  mem->ty= ty;
  return mem;
}

static Var *new_lvar(Type *ty) {
  Var *var = new_var(ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  push_scope(var->name, var);
  return var;
}

static Var *new_gvar(Type *ty) {
  Var *var = new_var(ty);
  var->is_local = false;
  var->next = globals;
  globals = var;
  push_scope(var->name, var);
  return var;
}

static Var *lookup_var(char *name) {
  for (VarScope *sc = var_scope; sc; sc = sc->next) {
    if (!strcmp(sc->name, name))
      return sc->var;
  }
  return NULL;
}

static Type *lookup_tag(char *name) {
  for (TagScope *sc = tag_scope; sc; sc = sc->next) {
    if (!strcmp(sc->name, name))
      return sc->ty;
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
  ty->identifier = new_gvar_name();
  Var *var = new_gvar(ty);
  var->init_data = s;
  return var;
}

static Node *new_node_unary(NodeKind kind, Node *lhs, Token *token) {
  Node *node = new_node(kind, token);
  node->lhs = lhs;
  return node;
}

static Node *new_node_binary(NodeKind kind, Node *lhs, Node *rhs, Token *token) {
  Node *node = new_node(kind, token);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_node_var(Var *var, Token *token) {
  Node *node = new_node(ND_VAR, token);
  node->var = var;
  return node;
}

static Node *new_node_num(int val, Token *token) {
  Node *node = new_node(ND_NUM, token);
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
    rhs = new_node_binary(ND_MUL, rhs, new_node_num(lhs->ty->base->size, tok), tok);
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
    rhs = new_node_binary(ND_MUL, rhs, new_node_num(lhs->ty->base->size, tok), tok);
    return new_node_binary(ND_SUB, lhs, rhs, tok);
  }

  // ptr - ptr: returns how many elements are between the two
  if (is_pointer_like(lhs->ty) && is_pointer_like(rhs->ty)) {
    Node *node = new_node_binary(ND_SUB, lhs, rhs, tok);
    return new_node_binary(ND_DIV, node, new_node_num(lhs->ty->base->size, tok), tok);
  }

  // number - ptr (illegal)
  error_tok(tok, "invalid operands");
}

static Type *typespec(void);
static Type *declarator(Type *base);
static Node *declaration(void);

static Function *funcdef(Type *ty);
static Type *func_params(void);
static Type *struct_decl(void);

static Node *block_stmt(void);
static Node *stmt(void);

static Node *if_stmt(void);
static Node *while_stmt(void);
static Node *for_stmt(void);
static Node *return_stmt(void);
static Node *expr_stmt(void);

static Node *expr(void);
static Node *assign(void);
static Node *equality(void);
static Node *relational(void);
static Node *add(void);
static Node *mul(void);
static Node *unary(void);
static Node *postfix(void);
static Node *primary(void);
static Node *func_or_var(void);
static Node *arg_list(void);

// program = (funcdef | global-var)*
Program *parse() {
  Function head = {0};
  Function *cur = &head;
  globals = NULL;

  while (!at_eof()) {
    Type *basety = typespec();
    Type *ty = declarator(basety);

    // function
    if (ty->kind == TY_FUNC) {
      cur = cur->next = funcdef(ty);
      continue;
    }

    // global variable = typespec declarator ("," declarator)* ";"
    for (;;) {
      new_gvar(ty);
      if (consume(";"))
        break;
      expect(",");
      ty = declarator(basety);
    }
  }

  Program *prog = new_program();
  prog->globals = globals;
  prog->fns = head.next;

  return prog;
}

// typespec = "int" | "char" | "struct" struct_decl
static Type *typespec() {
  if (consume("char"))
    return ty_char;

  if (consume("int"))
    return ty_int;

  if (consume("struct"))
    return struct_decl();

  error_tok(token, "typename expected");
}

// type-suffix = "(" func-params ")"
//             | "[" num "]" type-suffix
//             | ε
static Type *type_suffix(Type *ty) {
  if (consume("(")) {
    ty = func_returning(ty);
    ty->params = func_params();
    expect(")");
    return ty;
  }
  if (consume("[")) {
    int sz = expect_number();
    expect("]");
    ty = type_suffix(ty);  // first, define rightmost sub-array's size
    ty = array_of(ty, sz); // this array composes of sz length of subarrays above
    return ty;
  }

  return ty;
}

// declarator = "*"* ident type-suffix
// TODO: to consider "(" declarator ")" | type-suffix+
static Type *declarator(Type *ty) {
  while (consume("*"))
    ty = pointer_to(ty);

  char *name = expect_ident();
  ty = type_suffix(ty);

  ty->identifier = name;
  return ty;
}

// declaration = typespec (declarator ( = expr)? ( "," declarator ( = expr)? )* )? ";"
static Node *declaration() {
  Node head = {};
  Node *cur = &head;
  Token *start_decl = token;

  Type *basety = typespec();

  while(!consume(";")) {
    if (cur != &head)
      expect(",");

    Token *start = token;
    Type *ty = declarator(basety);
    Var *var = new_lvar(ty);

    Node *node = new_node_var(var, token);
    if (consume("="))
      node = new_node_binary(ND_ASSIGN, node, assign(), start);

    cur = cur->next = new_node_unary(ND_EXPR_STMT, node, start);
  }

  Node *blk = new_node(ND_BLOCK, start_decl);
  blk->body = head.next;

  return blk;
}

// whether given token reprents a type
static bool is_typename() {
  return equal("char") || equal("int") || equal("struct");
}

// struct-members = (typespec declarator ("," declarator)* ";")*
static Member *struct_members() {
  Member head = {0};
  Member *cur = &head;

  while (!equal("}")) {
    Type *basety = typespec();
    int cnt = 0;

    while (!consume(";")) {
      if (cnt++)
        expect(",");

      Type *ty = declarator(basety);
      Member *mem = new_member(ty);
      cur = cur->next = mem;
    }
  }

  return head.next;
}

// struct-decl = ident? "{" struct-members "}"
static Type *struct_decl() {
  // Read a struct tag
  char *tag_name = NULL;
  Token *start = token;

  if (token->kind == TK_IDENT)
    tag_name = expect_ident();

  if (tag_name && !equal("{")) {
    Type *ty = lookup_tag(tag_name);
    if (!ty)
      error_tok(start, "unknown struct type");
    return ty;
  }

  expect("{");
  Type *ty = new_type(TY_STRUCT, 0, 0);
  ty->members = struct_members();

  int offset = 0;
  for (Member *mem = ty->members; mem; mem = mem->next) {
    offset = align_to(offset, mem->ty->align);
    mem->offset = offset;
    offset += mem->ty->size;

    if (ty->align < mem->ty->align)
      ty->align = mem->ty->align;
  }
  ty->size = align_to(offset, ty->align);
  expect("}");

  // Register the struct type if name is given
  if (tag_name)
    push_tag_scope(tag_name, ty);

  return ty;
}

// funcdef = { block_stmt }
// TODO: consider poiter-type
static Function *funcdef(Type *ty) {
  locals = NULL;

  Function *func = new_function(ty);

  enter_scope();
  for (Type *t = ty->params; t; t = t->next) {
    Var *var = new_lvar(t); // TODO: check if this registratoin order make sense (first defined comes first, latter could overshadow the earlier)
  }

  func->params = locals;

  func->node = block_stmt();
  func->locals = locals;

  leave_scope();
  return func;
}

// func-params = param, ("," param)*
// param = typespec declarator
static Type *func_params() {
  Type head = {0};
  Type *cur = &head;

  while (!equal(")")) {
    if (cur != &head)
      expect(",");

    Token *start = token;
    Type *basety = typespec();
    Type *ty = declarator(basety);
    cur = cur->next = copy_ty(ty);
  }

  return head.next;
}

// block_stmt = stmt*
// TODO: allow blocks to have their own local vars
static Node *block_stmt() {
  Node head = {};
  Node *cur = &head;
  Token *start = token;

  enter_scope();

  expect("{");
  while (!consume("}")) {
    if (is_typename())
      cur = cur->next = declaration();
    else
      cur = cur->next = stmt();

    generate_type(cur);
  }

  leave_scope();

  Node *node = new_node(ND_BLOCK, start);
  node->body = head.next;

  return node;
}

// stmt = if_stmt | return_stmt | { block_stmt } | expr_stmt
static Node *stmt() {
  Node *node;

  if (equal("if")) {
    node = if_stmt();
    return node;
  }

  if (equal("while")) {
    node = while_stmt();
    return node;
  }

  if (equal("for")) {
    node = for_stmt();
    return node;
  }

  if (equal("return")) {
    node = return_stmt();
    return node;
  }

  if (equal("{")) {
    node = block_stmt();
    return node;
  }

  node = expr_stmt();
  return node;
}

// if_stmt = "if" (cond) then_stmt ("else" els_stmt)
static Node *if_stmt() {
  Node *node;
  Token *start = token;

  expect("if");
  node = new_node(ND_IF, start);
  expect("(");
  node->cond = expr();
  expect(")");
  node->then = stmt();
  if (consume("else"))
    node->els = stmt();
  return node;
}

// while_stmt = "while" (cond) then_stmt
static Node *while_stmt() {
  Node *node;
  Token *start = token;

  expect("while");
  node = new_node(ND_FOR, start);
  expect("(");
  node->cond = expr();
  expect(")");
  node->then = stmt();

  return node;
}

// for_stmt = "for" ( init; cond; inc) then_stmt
static Node *for_stmt() {
  Node *node;
  Token *start = token;

  expect("for");
  node = new_node(ND_FOR, start);
  expect("(");
  if(!consume(";")) {
    node->init = new_node_unary(ND_EXPR_STMT, expr(), token);
    expect(";");
  }
  if(!consume(";")) {
    node->cond = expr();
    expect(";");
  }
  if(!consume(")")) {
    node->inc = new_node_unary(ND_EXPR_STMT, expr(), token);
    expect(")");
  }
  node->then = stmt();

  return node;
}

// return_stmt = "return" expr
static Node *return_stmt() {
  Node *node;
  Token *start = token;

  expect("return");
  node = new_node_unary(ND_RETURN, expr(), start);
  expect(";");
  return node;
}

// expr_stmt
static Node *expr_stmt() {
  Node *node;
  Token *start = token;

  node = new_node_unary(ND_EXPR_STMT, expr(), start);
  expect(";");
  return node;
}

// expr = assign ( "," expr )?
static Node *expr() {
  Node *node = assign();

  // supporting "generized lvalue" supported by past GCC (already deprecated)
  // implemeted for convenient use
  if (consume(","))
    node = new_node_binary(ND_COMMA, node, expr(), token);

  return node;
}

// assign = equality ("=" assign)?
static Node *assign() {
  Node *node = equality();
  Token *start = token;

  if (consume("="))
    node = new_node_binary(ND_ASSIGN, node, assign(), start);

  return node;
}

// equality = relational ('==' relational | '!=' relational)*
static Node *equality() {
  Node *node = relational();
  Token *start = token;

  for(;;) {
    if (consume("=="))
      node = new_node_binary(ND_EQ, node, relational(), start);
    else if (consume("!="))
      node = new_node_binary(ND_NE, node, relational(), start);
    else
      return node;
  }
}

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational() {
  Node *node = add();
  Token *start = token;

  for(;;) {
    if (consume("<"))
      node = new_node_binary(ND_LT, node, add(), start);
    else if (consume("<="))
      node = new_node_binary(ND_LE, node, add(), start);
    else if (consume(">"))
      node = new_node_binary(ND_LT, add(), node, start);
    else if (consume(">="))
      node = new_node_binary(ND_LE, add(), node, start);
    else
      return node;
  }
}

// add = mul ("+" mul | "-" mul)*
static Node *add() {
  Node *node = mul();
  Token *start = token;

  for(;;) {
    if (consume("+"))
      node = new_node_add(node, mul(), start);
    else if (consume("-"))
      node = new_node_sub(node, mul(), start);
    else
      return node;
  }
}

// mul = unary ("*" unary | "/" unary)*
static Node *mul() {
  Node *node = unary();
  Token *start = token;

  for(;;) {
    if (consume("*"))
      node = new_node_binary(ND_MUL, node, unary(), start);
    else if (consume("/"))
      node = new_node_binary(ND_DIV, node, unary(), start);
    else
      return node;
  }
}

// unary = ("+" | "-" | "*" | "&")? unary
//       | postfix
static Node *unary() {
  Token *start = token;

  if (consume("+"))
    return unary();
  if (consume("-"))
    return new_node_binary(ND_SUB, new_node_num(0, start), unary(), start);
  if (consume("&"))
    return new_node_unary(ND_ADDR, unary(), start);
  if (consume("*"))
    return new_node_unary(ND_DEREF, unary(), start);
  return postfix();
}

static Member *get_struct_member(Type *ty, Token *tok) {
  for (Member *mem = ty->members; mem; mem = mem->next)
    if (!strncmp(mem->name, tok->str, tok->len))
      return mem;
  error_tok(tok, "no such member");
}

static Node *struct_ref(Node *lhs, Token *tok) {
  generate_type(lhs);
  if (lhs->ty->kind != TY_STRUCT)
    error_tok(lhs->token, "not a struct");

  Node *node = new_node_unary(ND_MEMBER, lhs, tok);
  node->member = get_struct_member(lhs->ty, tok);
  return node;
}

// postfix = primary ("[" expr "]" | "." ident)*
static Node *postfix() {
  Node *node = primary();

  for (;;) {
    if (consume("["))  {
      // x[y] is syntax sugar for *(x + y)
      Token *start = token;
      Node *idx = expr();
      expect("]");
      node = new_node_unary(ND_DEREF, new_node_add(node, idx, start), start);
      continue;
    }

    if (consume(".")) {
      node = struct_ref(node, token);
      expect_ident();
      continue;
    }

    return node;
  }
}

// primary = "(" "{" stmt stmt* "}" ")"
//           | "(" expr ")"
//           | "sizeof" unary
//           | func_or_var
//           | str
//           | num
static Node *primary() {
  Token *start = token;

  if (consume("(")) {
    if (equal("{")) {
      Node *node = new_node(ND_STMT_EXPR, start);
      node->body = block_stmt()->body;
      expect(")");

      Node *cur = node->body;
      while(cur && cur->next)
        cur = cur->next;

      if (!cur || cur->kind != ND_EXPR_STMT)
        error_tok(start, "statement expression returning void is not supported");
      return node;
    }

    Node *node = expr();
    expect(")");
    return node;
  }

  if (token->kind == TK_IDENT) {
    return func_or_var();
  }

  if (consume("sizeof")) {
    Node *node = unary();
    generate_type(node);
    return new_node_num(node->ty->size, start);
  }

  if (token->kind == TK_STR) {
    Var *var = new_string_literal(token->contents, token->cont_len);
    expect_string();
    return new_node_var(var, start);
  }

  return new_node_num(expect_number(), start);
}

// func_or_var = func(arg_list) | var
static Node *func_or_var() {
  Token *start = token;
  char *name = expect_ident();

  if (consume("(")) {
    Node *node = new_node(ND_FUNCALL, start);
    node->funcname = name;
    node->args = arg_list();
    expect(")");
    return node;
  }

  Var *var = lookup_var(name);
  if (!var) {
    error_tok(start, "undefined variable");
  }
  return new_node_var(var, start);
}

// arg_list = (assign (, assign)*)?
static Node *arg_list() {
  Node head = {0};
  Node *cur = &head;

  while (!equal(")")) {
    if (cur != &head)
      expect(",");
    cur = cur->next = assign();
  }

  return head.next;
}
