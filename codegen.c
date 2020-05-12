#include "9cc.h"

//
// Code generator
//

static void gen_expr(Node *node);
static void gen_stmt(Node *node);
static void gen_addr(Node *node);
static void load(Type *ty);
static void store(void);

static int labelseq = 1;
static Function  *current_fn;

static void gen_addr(Node *node) {
  switch (node->kind) {
    case ND_VAR:
      if (node->var->is_local) {
        printf("  mov rax, rbp\n");
        printf("  sub rax, %d\n", node->var->offset);
        printf("  push rax\n");
      } else {
        printf("  mov rax, offset %s\n", node->var->name);
        printf("  push rax\n");
      }
      return;
    case ND_DEREF: // *(foo + 8) = 123; (DEREF as lvalue)
      gen_expr(node->lhs);
      return;
  }

  error_tok(node->token, "not an lvalue");
}

static void load(Type *ty) {
  if (ty->kind == TY_ARRAY) {
    // NOOP for array type node
    // an entire array cannot be "loaded". Instead the variable
    // is interperted as the address of the first element
    // (therefore, unlike any other variables, this does not refer to its stored value)
    return;
  }

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

  for (Var *arg = params; arg; arg = arg->next)
    i++;

  for (Var *arg = params; arg; arg = arg->next) {
    printf("  mov [rbp-%d], %s\n", arg->offset, argreg[--i]);
  }
}

static void gen_expr(Node *node) {
  switch (node->kind) {
  case ND_ASSIGN:
    if (node->ty->kind == TY_ARRAY)
      error_tok(node->token, "not an lvalue");

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

    load(node->ty);
    return;
  case ND_ADDR:
    gen_addr(node->lhs);
    return;
  case ND_DEREF:
    gen_expr(node->lhs);
    load(node->ty);
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
    error_tok(node->token, "invalid expression");
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
    printf("  jmp .L.return.%s\n", current_fn->name);
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
    error_tok(node->token, "invalid statement");
  }
}

static void emit_data(Program *prog) {
  printf(".data\n");

  for (Var *var = prog->globals; var; var = var->next) {
    printf("%s:\n", var->name);
    printf("  .zero %d\n", var->ty->size);
  }
}

static void emit_text(Program *prog) {
  printf(".text\n");

  for(Function *fn = prog->fns; fn; fn = fn->next) {
    ScopedContext *ctx = fn->context;
    current_fn = fn;

    // label of the function
    printf(".globl %s\n", fn->name);
    printf("%s:\n", fn->name);

    // prologue
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, %d\n", ctx->stack_size); // TODO: reserve registers (R12-R15) as well

    store_args(fn->params);

    for (Node *n = fn->node; n; n = n->next)
      gen_stmt(n);

    // epilogue
    printf(".L.return.%s:\n", fn->name);
    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");

    printf("  ret\n");
  }
}

void codegen(Program *prog) {
  printf(".intel_syntax noprefix\n");
  emit_data(prog);
  emit_text(prog);
}
