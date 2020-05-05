#include "9cc.h"

//
// Parser
//

Var *locals = NULL;

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

static Var *lookup_var(Token *tok) {
  for (Var *var = locals; var; var = var->next) {
    if ((strlen(var->name) == tok->len) && !strncmp(var->name, tok->str, tok->len))
      return var;
  }
  return NULL;
}

static Node *new_node_num(int val) {
  Node *node = new_node(ND_NUM);
  node->val = val;
  return node;
}

static Node *stmt(void);

static Node *if_stmt(void);
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

// scoped_block = stmt*
ScopedContext *parse() {
  Node head = {0};
  Node *cur = &head;

  while (!at_eof())
    cur = cur->next = stmt();

  ScopedContext *block = calloc(1, sizeof(ScopedContext));
  block->node = head.next;
  block->locals = locals;
  return block;
}

// stmt = if_stmt | return_stmt | expr_stmt
static Node *stmt() {
  Node *node;

  if (consume("if")) {
    node = if_stmt();
    return node;
  }

  if (consume("return")) {
    node = return_stmt();
    return node;
  }

  node = expr_stmt();
  return node;
}

// if_stmt (without "if") = (cond) then_stmt ("else" els_stmt)
static Node *if_stmt() {
  Node *node;

  node = new_node(ND_IF);
  expect("(");
  node->cond = expr();
  expect(")");
  node->then = stmt();
  if (consume("else"))
    node->els = stmt();
  return node;
}

// return_stmt = "return" expr
static Node *return_stmt() {
  Node *node;

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

// primary = num | ident | "(" expr ")"
static Node *primary() {

  if (consume("(")) {
    Node *node = expr();
    expect(")");
    return node;
  }

  if (token->kind == TK_IDENT) {
    Var *var = lookup_var(token);
    if (!var) {
      char *name = strndup(token->str, token->len);
      var = new_var(name);
      var->next = locals;
      locals = var;
    }
    expect_ident();

    return new_node_var(var);
  }

  return new_node_num(expect_number());
}
