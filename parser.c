#include "9cc.h"

//
// Parser
//

Var *locals;
Var *globals;

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

static ScopedContext *new_context(Var *lcl) {
  ScopedContext *ctx = calloc(1, sizeof(ScopedContext));
  ctx->locals = lcl;
  return ctx;
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

// XXX: No scopes implemehted, so it will simply overwrite old definition
// if duplicated definition occurs
// (no checks using lookup_var()
static Var *new_lvar(Type *ty) {
  Var *var = new_var(ty);
  var->is_local = true;
  var->next = locals;
  locals = var;
  return var;
}

static Var *new_gvar(Type *ty) {
  Var *var = new_var(ty);
  var->is_local = false;
  var->next = globals;
  globals = var;
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

static Var *lookup_var(char *name) {
  for (Var *var = locals; var; var = var->next) {
    if (!strcmp(var->name, name))
      return var;
  }

  for (Var *var = globals; var; var = var->next) {
    if (!strcmp(var->name, name))
      return var;
  }
  return NULL;
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

// typespec = "int"
static Type *typespec() {
  expect("int");
  return ty_int;
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

// funcdef = { block_stmt }
// TODO: consider poiter-type
static Function *funcdef(Type *ty) {
  locals = NULL;

  Function *func = new_function(ty);

  for (Type *t = ty->params; t; t = t->next) {
    Var *var = new_lvar(t); // TODO: check if this registratoin order make sense (first defined comes first, latter could overshadow the earlier)
  }

  func->params = locals;

  func->node = block_stmt();
  func->context = new_context(locals);

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

  expect("{");
  while (!consume("}")) {
    if (equal("int"))
      cur = cur->next = declaration();
    else
      cur = cur->next = stmt();

    generate_type(cur);
  }

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

// expr = assign
static Node *expr() {
  return assign();
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

// postfix = primary ("[" expr "]")*
static Node *postfix() {
  Node *node = primary();

  while (consume("["))  {
    // x[y] is syntax sugar for *(x + y)
    Token *start = token;
    Node *idx = expr();
    expect("]");
    node = new_node_unary(ND_DEREF, new_node_add(node, idx, start), start);
  }

  return node;
}

// primary = "(" expr ")" | "sizeof" unary | func_or_var | num
static Node *primary() {
  Token *start = token;

  if (consume("(")) {
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

// arg_list = (expr (, expr)*)?
static Node *arg_list() {
  Node head = {0};
  Node *cur = &head;

  while (!equal(")")) {
    if (cur != &head)
      expect(",");
    cur = cur->next = expr();
  }

  return head.next;
}
