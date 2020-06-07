#include "9cc.h"

//
// Code generator
//

static void gen_expr(Node *node);
static void gen_stmt(Node *node);
static void gen_addr(Node *node);
static void load(Type *ty);
static void store(Type *ty);

static int labelseq = 1;
static const char *argreg8[]  = { "dil", "sil", "dl", "cl", "r8b", "r9b" };
static const char *argreg16[] = { "di",  "si",  "dx", "cx", "r8w", "r9w" };
static const char *argreg32[] = { "edi", "esi", "edx", "ecx", "e8", "e9" };
static const char *argreg64[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
static Function  *current_fn;

static char *xreg(Type *ty, int idx) {
  static char *reg64[] = {"rax", "rsi", "rdi"};
  static char *reg32[] = {"eax", "esi", "edi"};

  if (ty->base || size_of(ty) == 8)
    return reg64[idx];

  return reg32[idx];
}

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
    case ND_COMMA:
      gen_expr(node->lhs);
      printf("  add rsp, 8\n");
      gen_addr(node->rhs);
      return;
    case ND_MEMBER:
      gen_addr(node->lhs);
      printf("  pop rax\n");
      printf("  add rax, %d\n", node->member->offset);
      printf("  push rax\n");
      return;
  }

  error_tok(node->token, "not an lvalue");
}

static void load(Type *ty) {
  if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT) {
    // NOOP for array type node
    // an entire array cannot be "loaded". Instead the variable
    // is interperted as the address of the first element
    // (therefore, unlike any other variables, this does not refer to its stored value)
    return;
  }

  int sz = size_of(ty);
  printf("  pop rax\n");

  if (sz == 1)
    printf("  movsx rax, byte ptr [rax]\n");
  else if (sz == 2)
    printf("  movsx rax, word ptr [rax]\n");
  else if (sz == 4)
    printf("  movsxd rax, dword ptr [rax]\n");
  else
    printf("  mov rax, [rax]\n");

  printf("  push rax\n");
}

static void store(Type *ty) {
  int sz = size_of(ty);

  printf("  pop rsi\n");
  printf("  pop rdi\n");

  if (ty->kind == TY_STRUCT) {
    for (int i = 0; i < sz; i++) {
      printf("  mov al, [rsi+%d]\n", i);
      printf("  mov [rdi+%d], al\n", i);
    }
  } else if (sz == 1) {
    printf("  mov [rdi], sil\n");
  } else if (sz == 2) {
    printf("  mov [rdi], si\n");
  } else if (sz == 4) {
    printf("  mov [rdi], esi\n");
  } else {
    printf("  mov [rdi], rsi\n");
  }

  printf("  push rsi\n");
}

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  printf("  pop rax\n");

  if (to->kind == TY_BOOL) {
    printf("  cmp rax, 0\n");
    printf("  setne al\n");
    printf("  movsx rax, al\n");

    printf("  push rax\n");
    return;
  }

  if (size_of(to) == 1)
    printf("  movsx rax, al\n");
  else if (size_of(to) == 2)
    printf("  movsx rax, ax\n");
  else if  (size_of(to) == 4)
    printf("  movsxd rax, eax\n");
  else if  (is_integer(from) && size_of(from) < 8)
    printf("  movsx rax, eax\n");

  printf("  push rax\n");
}

static void load_args(Node *args) {
  int argc = 0;

  for(Node *cur = args; cur != NULL; cur = cur->next) {
    gen_expr(cur);
    argc++;
  }

  for(int i = 0; i < argc; i++) {
    printf("  pop %s\n", argreg64[argc - 1 - i]);
  }
}

static void store_args(Var *params) {
  int i = 0;

  for (Var *arg = params; arg; arg = arg->next)
    i++;

  for (Var *arg = params; arg; arg = arg->next) {
    int sz = size_of(arg->ty);

    if (sz == 1)
      printf("  mov [rbp-%d], %s\n", arg->offset, argreg8[--i]);
    else if (sz == 2)
      printf("  mov [rbp-%d], %s\n", arg->offset, argreg16[--i]);
    else if (sz == 4)
      printf("  mov [rbp-%d], %s\n", arg->offset, argreg32[--i]);
    else
      printf("  mov [rbp-%d], %s\n", arg->offset, argreg64[--i]);
  }
}

static void gen_expr(Node *node) {
  printf(".loc 1 %d\n", node->token->line_no);

  switch (node->kind) {
  case ND_ASSIGN:
    if (node->ty->kind == TY_ARRAY)
      error_tok(node->token, "not an lvalue");

    gen_addr(node->lhs);
    gen_expr(node->rhs);

    store(node->ty);
    return;
  case ND_NUM:
    if (node->val == (int)node->val) {
      printf("  push %lu\n", node->val);
    } else {
      printf("  mov rax, %lu\n", node->val);
      printf("  push rax\n");
    }
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_NOT:
    gen_expr(node->lhs);
    char *rs = xreg(node->lhs->ty, 0);

    printf("  pop rax\n");
    printf("  cmp %s, 0\n", rs);
    printf("  sete al\n");
    printf("  movzx rax, al\n");
    printf("  push rax\n");
    return;
  case ND_FUNCALL: {
    load_args(node->args);

    printf("  mov rax, 0\n");
    printf("  call %s\n", node->funcname);
    printf("  push rax\n");
    return;
  }
  case ND_STMT_EXPR: {
    for (Node *n = node->body; n; n = n->next)
      gen_stmt(n);
    printf("  sub rsp, 8\n");
    return;
  }
  case ND_COMMA:
    gen_expr(node->lhs);
    printf("  add rsp, 8\n");
    gen_expr(node->rhs);
    return;
  case ND_VAR:
  case ND_MEMBER:
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

  char *rs = xreg(node->lhs->ty, 1);
  char *rd = xreg(node->lhs->ty, 2);

  printf("  pop rsi\n");  // rhs
  printf("  pop rdi\n");  // lhs

  switch (node->kind) {
  case ND_ADD:
    printf("  add %s, %s\n", rd, rs);
    break;
  case ND_SUB:
    printf("  sub %s, %s\n", rd, rs);
    break;
  case ND_MUL:
    printf("  imul %s, %s\n", rd, rs);
    break;
  case ND_DIV:
    if (size_of(node->ty) == 8) {
      printf("  mov rax, %s\n", rd);
      printf("  cqo\n");
      printf("  idiv %s\n", rs);
      printf("  mov %s, rax\n", rd);
    } else {
      printf("  mov eax, %s\n", rd);
      printf("  cdq\n");
      printf("  idiv %s\n", rs);
      printf("  movsxd rdi, eax\n"); // result extended to 64-bit
    }
    break;
  case ND_EQ:
    printf("  cmp %s, %s\n", rd, rs);
    printf("  sete al\n");
    printf("  movzx %s, al\n", rd);
    break;
  case ND_NE:
    printf("  cmp %s, %s\n", rd, rs);
    printf("  setne al\n");
    printf("  movzx %s, al\n", rd);
    break;
  case ND_LT:
    printf("  cmp %s, %s\n", rd, rs);
    printf("  setl al\n");
    printf("  movzx %s, al\n", rd);
    break;
  case ND_LE:
    printf("  cmp %s, %s\n", rd, rs);
    printf("  setle al\n");
    printf("  movzx %s, al\n", rd);
    break;
  default:
    error_tok(node->token, "invalid expression");
  }

  printf("  push rdi\n");
}

static void gen_stmt(Node *node) {
  printf(".loc 1 %d\n", node->token->line_no);

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

    if (!var->init_data) {
      printf("  .zero %d\n", size_of(var->ty));
      continue;
    }

    for (int i = 0; i < size_of(var->ty); i++) {
      printf("  .byte %d\n", var->init_data[i]);
    }
  }
}

static void emit_text(Program *prog) {
  printf(".text\n");

  for(Function *fn = prog->fns; fn; fn = fn->next) {
    current_fn = fn;

    // label of the function
    if (!fn->is_static)
      printf(".globl %s\n", fn->name);
    printf("%s:\n", fn->name);

    // prologue
    printf("  push rbp\n");
    printf("  mov rbp, rsp\n");
    printf("  sub rsp, %d\n", fn->stack_size); // TODO: reserve registers (R12-R15) as well

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
  // emit a .file directive for the assembler
  printf(".file 1 \"%s\"\n", current_filename);

  printf(".intel_syntax noprefix\n");

  emit_data(prog);
  emit_text(prog);
}
