#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
bool consume(char *op);
void expect(char *op);
Token *expect_ident(void);
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

  ND_ASSIGN,    // =
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=

  ND_RETURN,    // "return"
  ND_EXPR_STMT, // statement with expression (w/o return)
  ND_VAR,       // local variables
  ND_NUM,       // Integer
} NodeKind;

typedef struct Var Var;
struct Var {
  Var *next;
  char *name;
  int offset;
};

typedef struct Node Node;
struct Node {
  NodeKind kind;
  Node *next;

  Node *lhs;
  Node *rhs;

  Var *var;   // used when kind == ND_VAR
  int val;    // used when kind == ND_NUM
};

extern Var *locals;
extern int stack_size;

Node *program(void);

//
// codegen.c
//

void codegen(Node *node);
