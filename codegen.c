#include "9cc.h"

//
// Code generator
//

static void gen_expr(Node *node);
static void gen_stmt(Node *node);

static int labelseq = 1;
static char *funcname;

static void gen_addr(Node *node) {
  if (node->kind != ND_VAR)
    error("左辺値ではありません\n");

  printf("  mov rax, rbp\n");
  printf("  sub rax, %d\n", node->var->offset);
  printf("  push rax\n");
}

static void load() {
  printf("  pop rax\n");
  printf("  mov rax, [rax]\n");
  printf("  push rax\n");
}

static void store() {
  printf("  pop rdi\n");
  printf("  pop rax\n");
  printf("  mov [rax], rdi\n");
  printf("  push rdi\n");
}

static const char *argreg[] = {
  "rdi", "rsi", "rdx", "rcx", "r8", "r9"
};

static void load_args(Node *args) {
  int argc = 0;

  for(Node *cur = args; cur != NULL; cur = cur->next) {
    gen_expr(cur);
    argc++;
  }

  for(int i = 0; i < argc; i++) {
    printf("  pop %s\n", argreg[argc - 1 - i]);
  }
}

static void store_args(Var *params) {
  int i = 0;

  for (Var *arg = params; arg; arg = arg->next) {
    printf("  mov [rbp-%d], %s\n", arg->offset, argreg[i++]);
  }
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_ASSIGN:
    gen_addr(node->lhs);
    gen_expr(node->rhs);

    store();
    return;
  case ND_NUM:
    printf("  push %d\n", node->val);
    return;
  case ND_FUNCALL: {
    load_args(node->args);

    printf("  mov rax, 0\n");
    printf("  call %s\n", node->funcname);
    printf("  push rax\n");
    return;
  }
  case ND_VAR:
    gen_addr(node);

    load();
    return;
  }

  gen_expr(node->lhs);
  gen_expr(node->rhs);

  printf("  pop rdi\n"); // rhs
  printf("  pop rax\n");  // lhs

  switch (node->kind) {
  case ND_ADD:
    printf("  add rax, rdi\n");
    break;
  case ND_SUB:
    printf("  sub rax, rdi\n");
    break;
  case ND_MUL:
    printf("  imul rax, rdi\n");
    break;
  case ND_DIV:
    printf("  cqo\n");
    printf("  idiv rdi\n");
    break;
  case ND_EQ:
    printf("  cmp rax, rdi\n");
    printf("  sete al\n");
    printf("  movzx rax, al\n");
    break;
  case ND_NE:
    printf("  cmp rax, rdi\n");
    printf("  setne al\n");
    printf("  movzx rax, al\n");
    break;
  case ND_LT:
    printf("  cmp rax, rdi\n");
    printf("  setl al\n");
    printf("  movzx rax, al\n");
    break;
  case ND_LE:
    printf("  cmp rax, rdi\n");
    printf("  setle al\n");
    printf("  movzx rax, al\n");
    break;
  default:
    error("不正な式です\n");
  }

  printf("  push rax\n");
}

static void gen_stmt(Node *node) {
  switch (node->kind) {
  case ND_IF: {
    int seq = labelseq++;

    if (node->els) {
      gen_expr(node->cond);
      printf("  pop rax\n");
      printf("  cmp rax, 0\n");
      printf("  je .L.else.%d\n", seq);
      gen_stmt(node->then);
      printf("  jmp .L.end.%d\n", seq);
      printf(".L.else.%d:\n", seq);
      gen_stmt(node->els);
      printf(".L.end.%d:\n", seq);
    } else {
      gen_expr(node->cond);
      printf("  pop rax\n");
      printf("  cmp rax, 0\n");
      printf("  je .L.end.%d\n", seq);
      gen_stmt(node->then);
      printf(".L.end.%d:\n", seq);
    }
    return;
  }
  case ND_FOR: {
    int seq = labelseq++;

    if (node->init)
      gen_stmt(node->init);
    printf(".L.begin.%d:\n", seq);
    if (node->cond) {
      gen_expr(node->cond);
      printf("  pop rax\n");
      printf("  cmp rax, 0\n");
      printf("  je .L.end.%d\n", seq);
    }
    gen_stmt(node->then);
    if (node->inc)
      gen_stmt(node->inc);
    printf("  jmp .L.begin.%d\n", seq);
    printf(".L.end.%d:\n", seq);
    return;
  }
  case ND_RETURN:
    gen_expr(node->lhs);
    printf("  pop rax\n");
    printf("  jmp .L.return.%s\n", funcname);
    return;
  case ND_BLOCK: {
    Node *stmt = node->body;
    while(stmt) {
      gen_stmt(stmt);
      stmt = stmt->next;
    }
    return;
  }
  case ND_EXPR_STMT:
    gen_expr(node->lhs);
    printf("  add rsp, 8\n");
    return;
  default:
    error("不正な文です\n");
  }
}

void codegen(Function *func) {
  printf(".intel_syntax noprefix\n");
  printf(".global main\n");

  for(Function *fn = func; fn; fn = fn->next) {
    ScopedContext *ctx = fn->context;
    funcname = fn->name;

    // label of the function
    printf("%s:\n", funcname);

    // prologue
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, %d\n", ctx->stack_size); // TODO: reserve registers (R12-R15) as well

    store_args(fn->params);

    for (Node *n = fn->node; n; n = n->next)
      gen_stmt(n);

    // epilogue
    printf(".L.return.%s:\n", funcname);
    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");

    printf("  ret\n");
  }
}
