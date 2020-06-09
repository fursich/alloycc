#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
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
  long val;
  char *str;
  int len;

  char *contents; // string literal contents, including '\0' terminator
  int cont_len; // string literal length

  int line_no;
};

void error(char *fmt, ...);
void error_at(char *loc, char *fmt, ...);
void error_tok(Token *tok, char *fmt, ...);
void warn_tok(Token *tok, char *fmt, ...);
bool equal(Token *tok, char *op);
bool consume(Token **rest, Token *tok, char *op);
Token *skip(Token *tok, char *op);

char *expect_ident(Token **rest, Token *tok);
char *get_identifier(Token *tok);
char *expect_string(Token **rest, Token *tok);
long expect_number(Token **rest, Token *tok);

Token *tokenize_file(char *path);
extern char *current_filename;

//
// parser.c
//

typedef enum {
  ND_ADD,       // +
  ND_SUB,       // -
  ND_MUL,       // *
  ND_DIV,       // /
  ND_MOD,       // %

  ND_ADDR,      // unary &
  ND_DEREF,     // unary *
  ND_NOT,       // !
  ND_BITNOT,    // ~
  ND_LOGAND,    // &&
  ND_LOGOR,     // ||

  ND_BITAND,    // &
  ND_BITOR,     // |
  ND_BITXOR,    // ^
  ND_SHL,       // <<
  ND_SHR,       // >>

  ND_ASSIGN,    // =
  ND_COMMA,     // ,
  ND_MEMBER,    // . (struct member)
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=

  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_SWITCH,    // "switch"
  ND_CASE,      // "case"
  ND_BREAK,     // "break"
  ND_CONTINUE,  // "continue"
  ND_GOTO,      // "goto"
  ND_LABEL,     // labeled statement
  ND_RETURN,    // "return"
  ND_BLOCK,     // compound statement
  ND_EXPR_STMT, // statement with expression (w/o return)
  ND_STMT_EXPR, // statement expression (GNU extension)
  ND_FUNCALL,   // function call
  ND_VAR,       // local variables
  ND_NUM,       // Integer
  ND_CAST,      // type cast
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
  Type *func_ty;
  Node *args;

  // Goto or labeled statement
  char *label_name;

  // switch-cases
  Node *case_next;
  Node *default_case;
  int case_label;
  int case_end_label;

  // variable
  Var *var;

  // numeric literal
  long val;
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  Var *params;
  bool is_static;

  Node *node;
  Var *locals;
  int stack_size;
};

typedef struct Program Program;
struct Program {
  Function *fns;
  Var *globals;
};

Node *new_node_cast(Node *expr, Type *ty);
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
  TY_BOOL,
  TY_ENUM,
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
  int size;           // used as sizeof() value
  int align;          // alignment
  bool is_incomplete; // incomplete type
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
  Token *tok; // for error message
  char *name;
  int offset;
};

extern Type *ty_void;
extern Type *ty_bool;
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
Type *enum_type(void);
Type *struct_type(void);
int size_of(Type *ty);
bool is_integer(Type *ty);
bool is_pointer_like(Type *ty);
void generate_type(Node *node);
