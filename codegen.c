#include "alloycc.h"

//
// Code generator
//

static void gen_expr(Node *node);
static void gen_stmt(Node *node);
static void gen_addr(Node *node);
static void load(Type *ty);
static void store(Type *ty);
static void pop_to(char *rg, Type *ty);
static void push_from(char *rg, Type *ty);

static int labelseq = 1;
static int brkseq;
static int contseq;
static const char *argreg8[]  = { "dil", "sil", "dl", "cl", "r8l", "r9l" };
static const char *argreg16[] = { "di",  "si",  "dx", "cx", "r8w", "r9w" };
static const char *argreg32[] = { "edi", "esi", "edx", "ecx", "r8d", "r9d" };
static const char *argreg64[] = { "rdi", "rsi", "rdx", "rcx", "r8", "r9" };
static Function  *current_fn;

static char *reg(Type *ty, int idx, bool treat_integer_as64) {
  static char *reg64[] = {"rax", "rsi", "rdi"};
  static char *reg32[] = {"eax", "esi", "edi"};
  static char *freg[]  = {"xmm0", "xmm1", "xmm2"};

  if (is_flonum(ty))
    return freg[idx];

  if (ty->base || size_of(ty) == 8)
    return reg64[idx];

  return treat_integer_as64 ? reg64[idx] : reg32[idx];
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
  if (ty->kind == TY_ARRAY || ty->kind == TY_STRUCT || ty->kind == TY_FUNC) {
    // NOOP for array type node
    // an entire array cannot be "loaded". Instead the variable
    // is interperted as the address of the first element
    // (therefore, unlike any other variables, this does not refer to its stored value)
    return;
  }

  printf("  pop rax\n");

  // in-memory flonum can be treated as a mere 32/64bit "integer",
  // when loading its value to the stack
  if (ty->kind == TY_FLOAT) {
    printf("  mov eax, dword ptr [rax]\n");
    printf("  mov eax, eax\n"); // make sure upper 32-bit is cleared out
    printf("  push rax\n");
    return;
  } else if (ty->kind == TY_DOUBLE) {
    printf("  mov rax, [rax]\n");
    printf("  push rax\n");
    return;
  }

  int sz = size_of(ty);
  char *insn = ty->is_unsigned ? "movzx" : "movsx";

  if (sz == 1)
    printf("  %s rax, byte ptr [rax]\n", insn);
  else if (sz == 2)
    printf("  %s rax, word ptr [rax]\n", insn);
  else if (sz == 4)
    // NOTE: upper 32-bit is 0-extended (care about proper sign if casting as 64-bit)
    printf("  mov eax, dword ptr [rax]\n");
  else
    printf("  mov rax, [rax]\n");

  printf("  push rax\n");
}

static void store(Type *ty) {
  int sz = size_of(ty);

  char *rs64 = reg(ty, 1, true);
  printf("  pop rsi\n"); // rhs
  printf("  pop rdi\n"); // lhs (lvalue)

  if (ty->kind == TY_STRUCT) {
    for (int i = 0; i < sz; i++) {
      printf("  mov al, [rsi+%d]\n", i);
      printf("  mov [rdi+%d], al\n", i);
    }
  } else if (ty->kind == TY_FLOAT) {
    // NOTE:
    // in-memory flonum can be treated as a mere 32/64bit "integer",
    // when loading its value to the stack
    printf("  mov dword ptr [rdi], esi\n");
  } else if (ty->kind == TY_DOUBLE) {
    // NOTE:
    // in-memory flonum can be treated as a mere 32/64bit "integer",
    // when loading its value to the stack
    printf("  mov [rdi], rsi\n");
  } else if (sz == 1) {
    printf("  mov byte ptr [rdi], sil\n");
  } else if (sz == 2) {
    printf("  mov word ptr [rdi], si\n");
  } else if (sz == 4) {
    printf("  mov dword ptr [rdi], esi\n");
  } else {
    printf("  mov [rdi], rsi\n");
  }

  printf("  push rsi\n");
}

static void pop_to(char *rg, Type *ty) {
  if (ty->kind == TY_FLOAT) {
    // (sort of) equivalent operations to 'pop xmm_i'
    printf("  movss %s, DWORD PTR [rsp]\n", rg);
    printf("  add rsp, 8\n");
  } else if (ty->kind == TY_DOUBLE) {
    // (sort of) equivalent operations to 'pop xmm_i'
    printf("  movsd %s, QWORD PTR [rsp]\n", rg);
    printf("  add rsp, 8\n");
  } else {
    printf("  pop %s\n", rg);
  }
}

static void push_from(char *rg, Type *ty) {
  if (ty->kind == TY_FLOAT) {
    // (sort of) equivalent operations to 'push xmm_i'
    printf("  sub rsp, 8\n");
    printf("  mov QWORD PTR [rsp], 0\n");         // clear full 64bit before pushing 32bit value
    printf("  movss DWORD PTR [rsp], %s\n", rg);
  } else if (ty->kind == TY_DOUBLE) {
    // (sort of) equivalent operations to 'push xmm_i'
    printf("  sub rsp, 8\n");
    printf("  movsd QWORD PTR [rsp], %s\n", rg);
  } else {
    printf("  push %s\n", rg);
  }
}

static void cmp_zero(Type *ty) {
  if (ty->kind == TY_FLOAT) {
    pop_to("xmm1", ty);
    // compare against zero as float
    printf("  xorps xmm0, xmm0\n");
    printf("  ucomiss xmm0, xmm1\n");
  } else if (ty->kind == TY_DOUBLE) {
    pop_to("xmm1", ty);
    // compare against zero as double
    printf("  xorpd xmm0, xmm0\n");
    printf("  ucomisd xmm0, xmm1\n");
  } else {
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
  }
}

static void cast(Type *from, Type *to) {
  if (to->kind == TY_VOID)
    return;

  if (to->kind == TY_BOOL) {
    cmp_zero(from);
    printf("  setne al\n");
    printf("  movsx rax, al\n");

    printf("  push rax\n");
    return;
  }

  if (from->kind == TY_FLOAT) {
    if (to->kind == TY_FLOAT)
      return;

    if (to->kind == TY_DOUBLE) {
      printf("  cvtss2sd xmm0, DWORD PTR [rsp]\n");
      printf("  movsd QWORD PTR [rsp], xmm0\n");
    } else {
      printf("  cvttss2si rax, DWORD PTR [rsp]\n");
      printf("  mov [rsp], rax\n");
    }
    return;
  }

  if (from->kind == TY_DOUBLE) {
    if (to->kind == TY_DOUBLE)
      return;

    if (to->kind == TY_FLOAT) {
      printf("  cvtsd2ss xmm0, QWORD PTR [rsp]\n");
      printf("  mov QWORD PTR [rsp], 0\n");         // clear full 64bit before pushing 32bit value
      printf("  movss DWORD PTR [rsp], xmm0\n");
    } else {
      printf("  cvttsd2si rax, QWORD PTR [rsp]\n");
      printf("  mov [rsp], rax\n");
    }
    return;
  }

  if (to->kind == TY_FLOAT) {
    printf("  cvtsi2ss xmm0, QWORD PTR [rsp]\n");
    printf("  mov QWORD PTR [rsp], 0\n");         // clear full 64bit before pushing 32bit value
    printf("  movss DWORD PTR [rsp], xmm0\n");
    return;
  }

  if (to->kind == TY_DOUBLE) {
    printf("  cvtsi2sd xmm0, QWORD PTR [rsp]\n");
    printf("  movsd QWORD PTR [rsp], xmm0\n");
    return;
  }

  printf("  pop rax\n");

  char *insn = to->is_unsigned ? "movzx" : "movsx";
  if (size_of(to) == 1)
    printf("  %s rax, al\n", insn);
  else if (size_of(to) == 2)
    printf("  %s rax, ax\n", insn);
  else if  (size_of(to) == 4)
    printf("  mov eax, eax\n"); // upper 32-bit is cleared
  else if  (is_integer(from) && size_of(from) < 8 && !from->is_unsigned)
    printf("  movsxd rax, eax\n");
  // NOTE: casting not needed for the unsigned integers, as they all are supposed to zero extended when loaded
  // same applies for singed integers, expect for 32bit doubleword types that are always zero-extended

  printf("  push rax\n");
}

static void load_args(Node *node) {

  for(int i = 0; i < node->nargs; i++) {
    Var *arg = node->args[i];
    int sz = size_of(arg->ty);

    char *insn = arg->ty->is_unsigned ? "movzx" : "movsx";

    if (sz == 1)
      printf("  %s %s, byte ptr [rbp-%d]\n", insn, argreg32[i], arg->offset);
    else if (sz == 2)
      printf("  %s %s, word ptr [rbp-%d]\n", insn, argreg32[i], arg->offset);
    else if (sz == 4)
      printf("  mov %s, dword ptr [rbp-%d]\n", argreg32[i], arg->offset);
    else
      printf("  mov %s, [rbp-%d]\n", argreg64[i], arg->offset);
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

static void divmod(Node *node, char *rs, char *rd, char *res64, char *res32) {
  if (size_of(node->ty) == 8) {
    printf("  mov rax, %s\n", rd);
    if (node->ty->is_unsigned) {
      printf("  mov rdx, 0\n"); // zero-extention
      printf("  div %s\n", rs);
    } else {
      printf("  cqo\n");
      printf("  idiv %s\n", rs);
    }
    printf("  mov %s, %s\n", rd, res64);
  } else {
    printf("  mov eax, %s\n", rd);
    if (node->ty->is_unsigned) {
      printf("  mov edx, 0\n"); // zero-extention
      printf("  div %s\n", rs);
    } else {
      printf("  cdq\n");
      printf("  idiv %s\n", rs);
    }
    printf("  movsxd rdi, %s\n", res32); // NOTE: result extended to 64-bit
  }
}

static void builtin_va_start(Node *node) {
  int n = 0;
  for (Var *var = current_fn->params; var; var = var->next)
    n++;

  // va_list given as the first argument
  printf("  mov rax, [rbp-%d]\n", node->args[0]->offset);
  // set gp_offset as n * 8
  // * gp_offset holds the offset in bytes from reg_save_area to the place
  //   where the next available general purpose argument register is saved
  printf("  mov dword ptr [rax], %d\n", n * 8);

  // set reg_save_area as rbp-80
  printf("  mov [rax+16], rbp\n");
  printf("  sub qword ptr [rax+16], 80\n");
  // return with void value
  printf("  sub rsp, 8\n");
}

static void gen_expr(Node *node) {
  printf(".loc 1 %d\n", node->token->line_no);

  switch (node->kind) {
  case ND_ASSIGN:
    if (node->ty->kind == TY_ARRAY)
      error_tok(node->token, "not an lvalue");
    if (node->lhs->ty->is_const && !node->is_init)
      error_tok(node->token, "cannot assign to a const variable");

    gen_addr(node->lhs);
    gen_expr(node->rhs);

    store(node->ty);
    return;
  case ND_NUM:
    if (node->ty->kind == TY_FLOAT) {
      float fval = node->fval;
      printf("  mov rax, %u\n", *(int *)&fval);
      printf("  push rax\n");
    } else if (node->ty->kind == TY_DOUBLE) {
      printf("  mov rax, %lu\n", *(long *)&node->fval);
      printf("  push rax\n");
    } else if (node->ty->kind == TY_LONG) {
      printf("  movabs rax, %lu\n", node->val);
      printf("  push rax\n");
    } else {
      printf("  mov rax, %lu\n", node->val);
      printf("  push rax\n");
    }
    return;
  case ND_CAST:
    gen_expr(node->lhs);
    cast(node->lhs->ty, node->ty);
    return;
  case ND_COND: {
    int seq = labelseq++;

    gen_expr(node->cond);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  je .L.else.%d\n", seq);
    gen_expr(node->then);
    printf("  jmp .L.end.%d\n", seq);
    printf(".L.else.%d:\n", seq);
    gen_expr(node->els);
    printf(".L.end.%d:\n", seq);
    return;
  }
  case ND_NOT:
    gen_expr(node->lhs);
    char *rs = reg(node->lhs->ty, 0, false);

    printf("  pop rax\n");
    printf("  cmp %s, 0\n", rs);
    printf("  sete al\n");
    printf("  movzx rax, al\n");
    printf("  push rax\n");
    return;
  case ND_BITNOT:
    gen_expr(node->lhs);
    printf("  pop rax\n");
    printf("  not rax\n");
    printf("  push rax\n");
    return;
  case ND_LOGAND: {
    int seq = labelseq++;

    gen_expr(node->lhs);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  je .L.false.%d\n", seq);
    gen_expr(node->rhs);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  je .L.false.%d\n", seq);
    printf("  push 1\n");
    printf("  jmp .L.end.%d\n", seq);
    printf(".L.false.%d:\n", seq);
    printf("  push 0\n");
    printf(".L.end.%d:\n", seq);
    return;
  }
  case ND_LOGOR: {
    int seq = labelseq++;

    gen_expr(node->lhs);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  jne .L.true.%d\n", seq);
    gen_expr(node->rhs);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  jne .L.true.%d\n", seq);
    printf("  push 0\n");
    printf("  jmp .L.end.%d\n", seq);
    printf(".L.true.%d:\n", seq);
    printf("  push 1\n");
    printf(".L.end.%d:\n", seq);
    return;
  }
  case ND_FUNCALL: {
    if (node->lhs->kind == ND_VAR &&
        !strcmp(node->lhs->var->name, "__builtin_va_start")) {
      builtin_va_start(node);
      return;
    }

    load_args(node);

    // save caller-saved registers
    printf("  push r10\n");
    printf("  push r11\n");

    gen_expr(node->lhs);  // function address
    printf("  pop r10\n");

    printf("  mov rax, 0\n");
    printf("  call r10\n");

    // restore caller-saved registers
    printf("  pop r11\n");
    printf("  pop r10\n");

    // According to The System V x86-64 ABI, a function that returns a boolean is
    // required to set the lower 8 bits only.
    // Hense, the upper 56 bits might contain arbitary values.
    if (node->ty->kind == TY_BOOL)
      printf("  movzx rax, al\n");
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
  case ND_NULL_EXPR:
    printf("  sub rsp, 8\n");
    return;
  }

  char *rs64 = reg(node->lhs->ty, 1, true);
  char *rd64 = reg(node->lhs->ty, 2, true);
  char *rs = reg(node->lhs->ty, 1, false);
  char *rd = reg(node->lhs->ty, 2, false);

  gen_expr(node->lhs);
  gen_expr(node->rhs);

  pop_to(rs64, node->lhs->ty); // rhs
  pop_to(rd64, node->lhs->ty); // lhs

  switch (node->kind) {
  case ND_ADD:
    if (node->ty->kind == TY_FLOAT)
      printf("  addss %s, %s\n", rd, rs);
    else if (node->ty->kind == TY_DOUBLE)
      printf("  addsd %s, %s\n", rd, rs);
    else
      printf("  add %s, %s\n", rd, rs);

    push_from(rd64, node->ty);
    return;
  case ND_SUB:
    if (node->ty->kind == TY_FLOAT)
      printf("  subss %s, %s\n", rd, rs);
    else if (node->ty->kind == TY_DOUBLE)
      printf("  subsd %s, %s\n", rd, rs);
    else
      printf("  sub %s, %s\n", rd, rs);

    push_from(rd64, node->ty);
    return;
  case ND_MUL:
    if (node->ty->kind == TY_FLOAT)
      printf("  mulss %s, %s\n", rd, rs);
    else if (node->ty->kind == TY_DOUBLE)
      printf("  mulsd %s, %s\n", rd, rs);
    else
      printf("  imul %s, %s\n", rd, rs); // can use imul regardless operands' signs

    push_from(rd64, node->ty);
    return;
  case ND_DIV:
    if (node->ty->kind == TY_FLOAT)
      printf("  divss %s, %s\n", rd, rs);
    else if (node->ty->kind == TY_DOUBLE)
      printf("  divsd %s, %s\n", rd, rs);
    else
      divmod(node, rs, rd, "rax", "eax");

    push_from(rd64, node->ty);
    return;
  case ND_MOD:
    divmod(node, rs, rd, "rdx", "edx");
    printf("  push %s\n", rd64);
    return;
  case ND_BITAND:
    printf("  and %s, %s\n", rd, rs);
    printf("  push %s\n", rd64);
    return;
  case ND_BITOR:
    printf("  or %s, %s\n", rd, rs);
    printf("  push %s\n", rd64);
    return;
  case ND_BITXOR:
    printf("  xor %s, %s\n", rd, rs);
    printf("  push %s\n", rd64);
    return;
  case ND_EQ:
    if (node->lhs->ty->kind == TY_FLOAT)
      printf("  ucomiss %s, %s\n", rd, rs);
    else if (node->lhs->ty->kind == TY_DOUBLE)
      printf("  ucomisd %s, %s\n", rd, rs);
    else
      printf("  cmp %s, %s\n", rd, rs);

    printf("  sete al\n");
    printf("  movzx rax, al\n");
    printf("  push rax\n");
    return;
  case ND_NE:
    if (node->lhs->ty->kind == TY_FLOAT)
      printf("  ucomiss %s, %s\n", rd, rs);
    else if (node->lhs->ty->kind == TY_DOUBLE)
      printf("  ucomisd %s, %s\n", rd, rs);
    else
      printf("  cmp %s, %s\n", rd, rs);

    printf("  setne al\n");
    printf("  movzx rax, al\n");
    printf("  push rax\n");
    return;
  case ND_LT:
    if (node->lhs->ty->kind == TY_FLOAT) {
      printf("  ucomiss %s, %s\n", rd, rs);
      printf("  setb al\n");
    } else if (node->lhs->ty->kind == TY_DOUBLE) {
      printf("  ucomisd %s, %s\n", rd, rs);
      printf("  setb al\n");
    } else {
      printf("  cmp %s, %s\n", rd, rs);
      if (node->lhs->ty->is_unsigned)
        printf("  setb al\n");
      else
        printf("  setl al\n");
    }

    printf("  movzx rax, al\n");
    printf("  push rax\n");
    return;
  case ND_LE:
    if (node->lhs->ty->kind == TY_FLOAT) {
      printf("  ucomiss %s, %s\n", rd, rs);
      printf("  setbe al\n");
    } else if (node->lhs->ty->kind == TY_DOUBLE) {
      printf("  ucomisd %s, %s\n", rd, rs);
      printf("  setbe al\n");
    } else {
      printf("  cmp %s, %s\n", rd, rs);
      if (node->lhs->ty->is_unsigned)
       printf("  setbe al\n");
      else
       printf("  setle al\n");
    }

    printf("  movzx rax, al\n");
    printf("  push rax\n");
    return;
  case ND_SHL:
    printf("  mov rcx, rsi\n");   // make sure that rcx contains all possible source bits from rs (rsi / esi)
    printf("  shl %s, cl\n", rd);
    printf("  push %s\n", rd64);
    return;
  case ND_SHR:
    printf("  mov rcx, rsi\n");   // make sure that rcx contains all possible source bits from rs (rsi / esi)
    if (node->lhs->ty->is_unsigned)
      printf("  shr %s, cl\n", rd);
    else
      printf("  sar %s, cl\n", rd);
    printf("  push %s\n", rd64);
    return;
  default:
    error_tok(node->token, "invalid expression");
  }
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
    int prevbrk = brkseq;
    int prevcont = contseq;
    brkseq = contseq = seq;

    if (node->init)
      gen_stmt(node->init);
    printf(".L.begin.%d:\n", seq);
    if (node->cond) {
      gen_expr(node->cond);
      printf("  pop rax\n");
      printf("  cmp rax, 0\n");
      printf("  je .L.break.%d\n", seq);
    }
    gen_stmt(node->then);
    printf(".L.continue.%d:\n", seq);
    if (node->inc)
      gen_stmt(node->inc);
    printf("  jmp .L.begin.%d\n", seq);
    printf(".L.break.%d:\n", seq);

    brkseq = prevbrk;
    contseq = prevcont;
    return;
  }
  case ND_DO: {
    int seq = labelseq++;
    int brk = brkseq;
    int cont = contseq;
    brkseq = contseq = seq;

    printf(".L.begin.%d:\n", seq);
    gen_stmt(node->then);
    printf(".L.continue.%d:\n", seq);
    gen_expr(node->cond);
    printf("  pop rax\n");
    printf("  cmp rax, 0\n");
    printf("  jne .L.begin.%d\n", seq);
    printf(".L.break.%d:\n", seq);

    brkseq = brk;
    contseq = cont;
    return;
  }
  case ND_SWITCH: {
    int seq = labelseq++;
    int prevbrk = brkseq;
    brkseq = seq;
    node->case_label = seq;

    gen_expr(node->cond);
    printf("  pop rax\n");

    for (Node *nd = node->case_next; nd; nd = nd->case_next) {
      nd->case_label = labelseq++;
      nd->case_end_label = seq;
      printf("  cmp rax, %ld\n", nd->val);
      printf("  je .L.case.%d\n", nd->case_label);
    }

    if (node->default_case) {
      int i = labelseq++;
      node->default_case->case_label = i;
      node->default_case->case_end_label = seq;
      printf("  jmp .L.case.%d\n", i);
    }

    printf("  jmp .L.break.%d\n", seq);
    gen_stmt(node->then);
    printf(".L.break.%d:\n", seq);

    brkseq = prevbrk;
    return;
  }
  case ND_CASE:
    printf(".L.case.%d:\n", node->case_label);
    gen_stmt(node->lhs);
    return;
  case ND_BREAK:
    if (brkseq == 0)
      error_tok(node->token, "stray break");
    printf("  jmp .L.break.%d\n", brkseq);
    return;
  case ND_CONTINUE:
    if (contseq == 0)
      error_tok(node->token, "stray continue");
    printf("  jmp .L.continue.%d\n", contseq);
    return;
  case ND_GOTO:
    printf("  jmp .L.label.%s.%s\n", current_fn->name, node->label_name);
    return;
  case ND_LABEL:
    printf(".L.label.%s.%s:\n", current_fn->name, node->label_name);
    gen_stmt(node->lhs);
    return;
  case ND_RETURN:
    if (node->lhs) {
      gen_expr(node->lhs);
      printf("  pop rax\n");
    }
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

static void emit_bss(Program *prog) {
  printf(".bss\n");

  for (Var *var = prog->globals; var; var = var->next) {
    if (var->init_data)
      continue;

    printf(".align %d\n", var->align);
    if (!var->is_static)
      printf(".globl %s\n", var->name);
    printf("%s:\n", var->name);
    printf("  .zero %d\n", size_of(var->ty));
  }
}

static void emit_data(Program *prog) {
  printf(".data\n");

  for (Var *var = prog->globals; var; var = var->next) {
    if (!var->init_data)
      continue;

    printf(".align %d\n", var->align);
    if (!var->is_static)
      printf(".globl %s\n", var->name);
    printf("%s:\n", var->name);

    Relocation *rel = var->rel;
    int pos = 0;
    while (pos < size_of(var->ty)) {
      if (rel && rel->offset == pos) {
        printf("  .quad %s%+ld\n", rel->label, rel->addend);
        rel = rel->next;
        pos += 8;
      } else {
        printf("  .byte %d\n", var->init_data[pos++]);
      }
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
    printf("  sub rsp, %d\n", fn->stack_size);
    // preserve callee-saved registers
    printf("  mov [rbp-8], r12\n");
    printf("  mov [rbp-16], r13\n");
    printf("  mov [rbp-24], r14\n");
    printf("  mov [rbp-32], r15\n");

    // save arg registers if function is variadic
    if (fn->is_variadic) {
      printf("  mov [rbp-80], rdi\n");
      printf("  mov [rbp-72], rsi\n");
      printf("  mov [rbp-64], rdx\n");
      printf("  mov [rbp-56], rcx\n");
      printf("  mov [rbp-48], r8\n");
      printf("  mov [rbp-40], r9\n");
    }

    store_args(fn->params);

    for (Node *n = fn->node; n; n = n->next)
      gen_stmt(n);

    // epilogue
    printf(".L.return.%s:\n", fn->name);
    // restore callee-saved registers
    printf("  mov r12, [rbp-8]\n");
    printf("  mov r13, [rbp-16]\n");
    printf("  mov r14, [rbp-24]\n");
    printf("  mov r15, [rbp-32]\n");
    printf("  mov rsp, rbp\n");
    printf("  pop rbp\n");

    printf("  ret\n");
  }
}

void codegen(Program *prog) {
  // emit a .file directive for the assembler
  printf(".file 1 \"%s\"\n", current_filename);

  printf(".intel_syntax noprefix\n");

  emit_bss(prog);
  emit_data(prog);
  emit_text(prog);
}
