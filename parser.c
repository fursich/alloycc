#include "9cc.h"

//
// Parser
//

Var *locals = NULL;

static Function *new_function(char *name) {
  Function *func = calloc(1, sizeof(Function));
  func->name = name;
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

static Var *new_var(char *name) {
  Var *var = calloc(1, sizeof(Var));
  var->name = name;
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
  return NULL;
}

static Node *new_node_num(int val, Token *token) {
  Node *node = new_node(ND_NUM, token);
  node->val = val;
  return node;
}

static Type *typespec(void);
static Node *declaration(void);

static Function *funcdef(void);
static Var *read_var_list(void);

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
static Node *primary(void);
static Node *func_or_var(void);
static Node *arg_list(void);

// program = funcdef*
Function *parse() {
  Function head = {0};
  Function *cur = &head;

  while (!at_eof())
    cur = cur->next = funcdef();

  return head.next;
}

static Type *typespec() {
  expect("int");
  return ty_int;
}

// declaration = typespec (declarator ( = expr)? ( "," declarator ( = expr)? )* )? ";"
static Node *declaration() {
  Node head = {};
  Node *cur = &head;
  Token *start_decl = token;

  Type *ty = typespec(); // TODO: store type info

  while(!consume(";")) {
    if (cur != &head)
      expect(",");

    Token *start = token;
    char *name = expect_ident();

    // No scopes implemehted, so it will simply overwrite old definition
    // if duplicated definition occurs
    // (no checks using lookup_var()
    Var *var = new_var(name); // TODO: use type info * consider args as well
    var->next = locals;
    locals = var;
    Node *node = new_node_var(var, start);

    if (consume("="))
      node = new_node_binary(ND_ASSIGN, node, assign(), start);

    cur = cur->next = new_node_unary(ND_EXPR_STMT, node, start);
  }

  Node *blk = new_node(ND_BLOCK, start_decl);
  blk->body = head.next;

  return blk;
}


// funcdef = ident(var_list) { block_stmt }
static Function *funcdef() {
  locals = NULL;

  char *name = expect_ident();
  Function *func = new_function(name);

  expect("(");
  Var *var = read_var_list();
  func->params = var;
  locals = var;
  expect(")");

  func->node = block_stmt();
  func->context = new_context(locals);

  return func;
}

// var_list = (ident (, ident)*)?
static Var *read_var_list() {
  Var head = {0};
  Var *cur = &head;

  while (!equal(")")) {
    if (cur != &head)
      expect(",");
    char *name = expect_ident();
    cur = cur->next = new_var(name);
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
  while (!consume("}"))
    if (equal("int"))
      cur = cur->next = declaration();
    else
      cur = cur->next = stmt();

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
      node = new_node_binary(ND_ADD, node, mul(), start);
    else if (consume("-"))
      node = new_node_binary(ND_SUB, node, mul(), start);
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

// unary = ("+" | "-" | "*" | "&")? unary | primary
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
  return primary();
}

// primary = "(" expr ")" | func_or_var | num
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
