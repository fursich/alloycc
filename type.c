#include "alloycc.h"

Type *ty_void  = &(Type) {TY_VOID,  1, 1};
Type *ty_bool  = &(Type) {TY_BOOL,  1, 1};

Type *ty_char  = &(Type) {TY_CHAR,  1, 1};
Type *ty_short = &(Type) {TY_SHORT, 2, 2};
Type *ty_int   = &(Type) {TY_INT,   4, 4};
Type *ty_long  = &(Type) {TY_LONG,  8, 8};

Type *ty_uchar  = &(Type) {TY_CHAR,  1, 1, true};
Type *ty_ushort = &(Type) {TY_SHORT, 2, 2, true};
Type *ty_uint   = &(Type) {TY_INT,   4, 4, true};
Type *ty_ulong  = &(Type) {TY_LONG,  8, 8, true};

Type *ty_float  = &(Type) {TY_FLOAT,  4, 4};
Type *ty_double = &(Type) {TY_DOUBLE, 8, 8};

Type *new_type(TypeKind kind, int size, int align) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = kind;
  ty->size = size;
  ty->align = align;
  return ty;
}

int align_to(int n, int base) {
  assert((base & (base - 1)) == 0);
  return (n + base - 1) & ~(base - 1);
}

Type *copy_ty(Type *org) {
  Type *ty = calloc(1, sizeof(Type));
  *ty = *org;
  return ty;
}

Type *pointer_to(Type *base) {
  Type *ty = new_type(TY_PTR, 8, 8);
  ty->base = base;
  return ty;
}

Type *func_returning(Type *return_ty) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = TY_FUNC;
  ty->return_ty = return_ty;
  return ty;
}

Type *array_of(Type *base, int len) {
  Type *ty = new_type(TY_ARRAY, size_of(base) * len, base->align);
  ty->base = base;
  ty->array_len = len;
  return ty;
}

Type *enum_type(void) {
  return new_type(TY_ENUM, 4, 4);
}

Type *struct_type(void) {
  return new_type(TY_STRUCT, 0, 1);
}

int size_of(Type *ty) {
  // FIXME: let abstract-decorator assign ty->ident (token for error handling)
  // (for instance, sizeof(void) causes segmention falut, since ty->indent is NULL)
  if (ty->kind == TY_VOID)
    error_tok(ty->ident, "void type");
  if (ty->is_incomplete)
    error_tok(ty->ident, "incomplete type");
  return ty->size;
}

bool is_integer(Type *ty) {
  TypeKind kd = ty->kind;
  return kd == TY_BOOL  || kd == TY_ENUM || kd == TY_CHAR ||
         kd == TY_SHORT || kd == TY_INT  || kd == TY_LONG;
}

bool is_flonum(Type *ty) {
  return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

bool is_numeric(Type *ty) {
  return is_integer(ty) || is_flonum(ty);
}

static bool is_scalar(Type *ty) {
  return is_numeric(ty) || is_pointer_like(ty);
}

// types having its base type that 'behaves like' a pointer
bool is_pointer_like(Type *ty) {
  return ty->base;
}

static Type *get_common_type(Type *ty1, Type *ty2) {
  if (ty1->base)
    return pointer_to(ty1->base);

  if (ty1->kind == TY_DOUBLE || ty2->kind == TY_DOUBLE)
    return ty_double;
  if (ty1->kind == TY_FLOAT || ty2->kind == TY_FLOAT)
    return ty_float;

  if (size_of(ty1) < 4)
    ty1 = ty_int;
  if (size_of(ty2) < 4)
    ty2 = ty_int;

  if (size_of(ty1) < size_of(ty2))
    return ty2;
  if (size_of(ty1) > size_of(ty2))
    return ty1;

  if (ty2->is_unsigned)
    return ty2;

  return ty1;
}

static void usual_arith_conv(Node **lhs, Node **rhs) {
  Type *ty = get_common_type((*lhs)->ty, (*rhs)->ty);
  *lhs = new_node_cast(*lhs, ty);
  *rhs = new_node_cast(*rhs, ty);
}

static void set_type_for_expr(Node *node) {
  switch(node->kind) {
    case ND_NUM:
      // for in-code numbers, proper types must already have been inferred at tokeninizer.
      // this is to catch the remaining "small numbers" that are generated internally.
      // (e.g. `++foo` is parsed as `foo + 1`, which requires a new ND_NUM that holds 1)
      node->ty = ty_int;
      return;
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
      usual_arith_conv(&node->lhs, &node->rhs);
      node->ty = node->lhs->ty;
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      usual_arith_conv(&node->lhs, &node->rhs);
      node->ty = ty_int;
      return;
    case ND_ASSIGN:
      if (is_scalar(node->rhs->ty))
        node->rhs = new_node_cast(node->rhs, node->lhs->ty);
      node->ty = node->lhs->ty;
      return;
    case ND_FUNCALL:
      node->ty = ty_long; // TODO: use return_ty
      return;
    case ND_COMMA:
      node->ty = node->rhs->ty;
      return;
    case ND_MEMBER:
      node->ty = node->member->ty;
      return;
    case ND_NOT:
    case ND_LOGOR:
    case ND_LOGAND:
      node->ty = ty_int;
      return;
    case ND_BITNOT:
    case ND_SHL:
    case ND_SHR:
      node->ty = node->lhs->ty;
      return;
    case ND_VAR:
      node->ty = node->var->ty;
      return;
    case ND_COND:
      if (node->then->ty->kind == TY_VOID || node->els->ty->kind == TY_VOID) {
        node->ty = ty_void;
      } else {
        usual_arith_conv(&node->then, &node->els);
        node->ty = node->then->ty;
      }
      return;
    case ND_ADDR: {
      Type *ty = node->lhs->ty;
      if (ty->kind == TY_ARRAY)
        // adddress of an element of array should be pointer to the element type
        //e.g. for char arr[2],  char *p = &arr[1] (p is a pointer to char, the element type)
        node->ty = pointer_to(ty->base);
      else
        // otherwise, address type should be a pointer to the given type
        node->ty = pointer_to(ty);
      return;
    }
    case ND_DEREF: {
      Type *ty = node->lhs->ty;

      if (ty->kind == TY_FUNC) {
        *node = *node->lhs;
        return;
      }

      if (!is_pointer_like(ty))
        error_tok(node->token, "invalid pointer dereference");
      if (ty->base->kind == TY_VOID)
        error_tok(node->token, "dereferencing a void poiter");

      node->ty = ty->base;
      return;
    }
    case ND_STMT_EXPR: {
      Node *stmt = node->body;
      while (stmt->next)
        stmt = stmt->next;
      node->ty = stmt->lhs->ty;
      return;
    }
  }
}

void generate_type(Node *node) {
  if (!node || node->ty)
    return;

  generate_type(node->lhs);
  generate_type(node->rhs);
  generate_type(node->cond);
  generate_type(node->then);
  generate_type(node->els);
  generate_type(node->init);
  generate_type(node->inc);

  for(Node *nd = node->body; nd; nd = nd->next)
    generate_type(nd);

  set_type_for_expr(node);
}

