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

static Node *new_node(NodeKind kind) {
  Node *node = calloc(1, sizeof(Node));
  node->kind = kind;
  return node;
}

static Var *new_var(char *name) {
  Var *var = calloc(1, sizeof(Var));
  var->name = name;
  return var;
}

static Node *new_node_unary(NodeKind kind, Node *lhs) {
  Node *node = new_node(kind);
  node->lhs = lhs;
  return node;
}

static Node *new_node_binary(NodeKind kind, Node *lhs, Node *rhs) {
  Node *node = new_node(kind);
  node->lhs = lhs;
  node->rhs = rhs;
  return node;
}

static Node *new_node_var(Var *var) {
  Node *node = new_node(ND_VAR);
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

static Node *new_node_num(int val) {
  Node *node = new_node(ND_NUM);
  node->val = val;
  return node;
}

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

  expect("{");
  while (!consume("}"))
    cur = cur->next = stmt();

  Node *node = new_node(ND_BLOCK);
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

  expect("if");
  node = new_node(ND_IF);
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

  expect("while");
  node = new_node(ND_FOR);
  expect("(");
  node->cond = expr();
  expect(")");
  node->then = stmt();

  return node;
}

// for_stmt = "for" ( init; cond; inc) then_stmt
static Node *for_stmt() {
  Node *node;

  expect("for");
  node = new_node(ND_FOR);
  expect("(");
  if(!consume(";")) {
    node->init = new_node_unary(ND_EXPR_STMT, expr());
    expect(";");
  }
  if(!consume(";")) {
    node->cond = expr();
    expect(";");
  }
  if(!consume(")")) {
    node->inc = new_node_unary(ND_EXPR_STMT, expr());
    expect(")");
  }
  node->then = stmt();

  return node;
}

// return_stmt = "return" expr
static Node *return_stmt() {
  Node *node;

  expect("return");
  node = new_node_unary(ND_RETURN, expr());
  expect(";");
  return node;
}

// expr_stmt
static Node *expr_stmt() {
  Node *node;

  node = new_node_unary(ND_EXPR_STMT, expr());
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
  if (consume("="))
    node = new_node_binary(ND_ASSIGN, node, assign());

  return node;
}

// equality = relational ('==' relational | '!=' relational)*
static Node *equality() {
  Node *node = relational();

  for(;;) {
    if (consume("=="))
      node = new_node_binary(ND_EQ, node, relational());
    else if (consume("!="))
      node = new_node_binary(ND_NE, node, relational());
    else
      return node;
  }
}

// relational = add ("<" add | "<=" add | ">" add | ">=" add)*
static Node *relational() {
  Node *node = add();

  for(;;) {
    if (consume("<"))
      node = new_node_binary(ND_LT, node, add());
    else if (consume("<="))
      node = new_node_binary(ND_LE, node, add());
    else if (consume(">"))
      node = new_node_binary(ND_LT, add(), node);
    else if (consume(">="))
      node = new_node_binary(ND_LE, add(), node);
    else
      return node;
  }
}

// add = mul ("+" mul | "-" mul)*
static Node *add() {
  Node *node = mul();

  for(;;) {
    if (consume("+"))
      node = new_node_binary(ND_ADD, node, mul());
    else if (consume("-"))
      node = new_node_binary(ND_SUB, node, mul());
    else
      return node;
  }
}

// mul = unary ("*" unary | "/" unary)*
static Node *mul() {
  Node *node = unary();

  for(;;) {
    if (consume("*"))
      node = new_node_binary(ND_MUL, node, unary());
    else if (consume("/"))
      node = new_node_binary(ND_DIV, node, unary());
    else
      return node;
  }
}

// unary = ("+" | "-")? unary | primary
static Node *unary() {

  if (consume("+"))
    return unary();
  if (consume("-"))
    return new_node_binary(ND_SUB, new_node_num(0), unary());
  return primary();
}

// primary = "(" expr ")" | func_or_var | num
static Node *primary() {

  if (consume("(")) {
    Node *node = expr();
    expect(")");
    return node;
  }

  if (token->kind == TK_IDENT) {
    return func_or_var();
  }

  return new_node_num(expect_number());
}

// func_or_var = func(arg_list) | var
static Node *func_or_var() {
  char *name = expect_ident();

  if (consume("(")) {
    Node *node = new_node(ND_FUNCALL);
    node->funcname = name;
    node->args = arg_list();
    expect(")");
    return node;
  }

  Var *var = lookup_var(name);
  if (!var) {
    var = new_var(name);
    var->next = locals;
    locals = var;
  }
  return new_node_var(var);
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
