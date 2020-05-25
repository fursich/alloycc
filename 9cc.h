#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

typedef struct Type Type;
typedef struct Member Member;

//
// tokenize.c
//

typedef enum {
  TK_RESERVED,
  TK_IDENT,
  TK_STR,
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

  char *contents; // string literal contents, including '\0' terminator
  int cont_len; // string literal length

  int line_no;
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
void skip(char *op);
bool consume(char *op);
void expect(char *op);
char *expect_ident(void);
char *get_identifier(Token *tok);
char *expect_string(void);
int expect_number(void);
bool at_eof(void);
Token *tokenize_file(char *path);

extern Token *token;
extern char *current_filename;

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
  ND_COMMA,     // ,
  ND_MEMBER,    // . (struct member)
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=

  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_RETURN,    // "return"
  ND_BLOCK,     // compound statement
  ND_EXPR_STMT, // statement with expression (w/o return)
  ND_STMT_EXPR, // statement expression (GNU extension)
  ND_FUNCALL,   // function call
  ND_VAR,       // local variables
  ND_NUM,       // Integer
} NodeKind;

typedef struct Var Var;
struct Var {
  Var *next;
  char *name;
  Type *ty;
  bool is_local;

  // for local variables
  int offset;

  // for global variables
  char *init_data;
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

  // block or statement espression
  Node *body;

  // struct member access
  Member *member;

  // Function call
  char *funcname;
  Node *args;

  Var *var;   // used when kind == ND_VAR
  int val;    // used when kind == ND_NUM
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  Var *params;
  Node *node;
  Var *locals;
  int stack_size;
};

typedef struct Program Program;
struct Program {
  Function *fns;
  Var *globals;
};

Program *parse(Token *tok);

//
// codegen.c
//

void codegen(Program *prog);

//
// type.c
//
typedef enum {
  TY_VOID,
  TY_CHAR,
  TY_SHORT,
  TY_INT,
  TY_LONG,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
  TY_STRUCT,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;       // used as sizeof() value
  int align;      // alignment
  Type *base;

  // used for declaration
  Token *ident;

  // function
  Type *return_ty;
  Type *params;
  Type *next;

  // array
  int array_len;

  // struct
  Member *members;
};

struct Member {
  Member *next;
  Type *ty;
  char *name;
  int offset;
};

extern Type *ty_void;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

Type *new_type(TypeKind kind, int size, int align);
int align_to(int n, int align);
Type *pointer_to(Type *base);
Type *copy_ty(Type *ty);
Type *func_returning(Type *ty);
Type *array_of(Type *ty, int len);
int size_of(Type *ty);
bool is_integer(Type *ty);
bool is_pointer_like(Type *ty);
void generate_type(Node *node);
