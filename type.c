#include "9cc.h"

Type *ty_char = &(Type) {TY_CHAR, 1};
Type *ty_int = &(Type) {TY_INT, 8};

Type *new_type(TypeKind kind, int size) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = kind;
  ty->size = size;
  return ty;
}

Type *copy_ty(Type *org) {
  Type *ty = calloc(1, sizeof(Type));
  *ty = *org;
  return ty;
}

Type *pointer_to(Type *base) {
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = TY_PTR;
  ty->size = 8;
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
  Type *ty = calloc(1, sizeof(Type));
  ty->kind = TY_ARRAY;
  ty->base = base;
  ty->size = base->size * len;
  ty->array_len = len;
  return ty;
}

bool is_integer(Type *ty) {
  return ty->kind == TY_CHAR || ty->kind == TY_INT;
}

// types having its base type that 'behaves like' a pointer
bool is_pointer_like(Type *ty) {
  return ty->base;
}

static void set_type_for_expr(Node *node) {
  switch(node->kind) {
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_ASSIGN:
      node->ty = node->lhs->ty;
      return;
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE:
      node->ty = ty_int; // TODO: bool?
      return;
    case ND_NUM:
      node->ty = ty_int;
      return;
    case ND_FUNCALL:
      node->ty = ty_int; // TODO: use return_ty
      return;
    case ND_COMMA:
      node->ty = node->rhs->ty;
      return;
    case ND_VAR:
      node->ty = node->var->ty;
      return;
    case ND_ADDR:
      if (node->lhs->ty->kind == TY_ARRAY)
        // adddress of an element of array should be pointer to the element type
        //e.g. for char arr[2],  char *p = &arr[1] (p is a pointer to char, the element type)
        node->ty = pointer_to(node->lhs->ty->base);
      else
        // otherwise, address type should be a pointer to the give type
        node->ty = pointer_to(node->lhs->ty);
      return;
    case ND_DEREF: {
      Type *ty = node->lhs->ty;
      if (is_pointer_like(ty))
        node->ty = ty->base;
      else
        node->ty = ty_int;
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
  for(Node *nd = node->args; nd; nd = nd->next)
    generate_type(nd);

  set_type_for_expr(node);
}

