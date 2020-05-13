#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct Type Type;

//
// tokenize.c
//

typedef enum {
  TK_RESERVED,
  TK_IDENT,
  TK_NUM,
  TK_EOF,
} TokenKind;

typedef struct Token Token;
struct Token {
  TokenKind kind;
  Token *next;
  int val;
  char *str;
  int len;
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(char *op);
void skip(char *op);
bool consume(char *op);
void expect(char *op);
char *expect_ident(void);
int expect_number(void);
bool at_eof(void);
Token *tokenize(void);

extern char *user_input;
extern Token *token;

//
// parser.c
//

typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /

  ND_ADDR,      // unary &
  ND_DEREF,     // unary *

  ND_ASSIGN,    // =
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=

  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_RETURN,    // "return"
  ND_BLOCK,     // compound statement
  ND_EXPR_STMT, // statement with expression (w/o return)
  ND_FUNCALL,   // function call
  ND_VAR,       // local variables
  ND_NUM,       // Integer
} NodeKind;

typedef struct Var Var;
struct Var {
  Var *next;
  char *name;
  Type *ty;
  int offset;
  bool is_local;
};

typedef struct Node Node;
struct Node {
  NodeKind kind;
  Node *next;
  Type *ty;      // type for the node (not evaluated for stmt, expr only)
  Token *token;

  Node *lhs;
  Node *rhs;

  // if-statement, while-statement, for-statement
  Node *cond;
  Node *then;
  Node *els;
  Node *init;
  Node *inc;

  // block
  Node *body;

  // Function call
  char *funcname;
  Node *args;

  Var *var;   // used when kind == ND_VAR
  int val;    // used when kind == ND_NUM
};

typedef struct ScopedContext ScopedContext;
struct ScopedContext {
  Var *locals;
  int stack_size;
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  Var *params;
  Node *node;
  ScopedContext *context;
};

typedef struct Program Program;
struct Program {
  Function *fns;
  Var *globals;
};

Program *parse(void);

//
// codegen.c
//

void codegen(Program *prog);

//
// type.c
//
typedef enum {
  TY_CHAR,
  TY_INT,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;       // used as sizeof() value
  Type *base;
  char *identifier;

  // function
  Type *return_ty;
  Type *params;
  Type *next;

  // array
  int array_len;
};

extern Type *ty_char;
extern Type *ty_int;
Type *pointer_to(Type *base);
Type *copy_ty(Type *ty);
Type *func_returning(Type *ty);
Type *array_of(Type *ty, int len);
bool is_integer(Type *ty);
bool is_pointer_like(Type *ty);
void generate_type(Node *node);
