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
typedef struct Relocation Relocation;

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
  double fval;
  Type *ty; // Used if TK_NUM
  char *str;
  int len;

  char *contents; // string literal contents, including '\0' terminator
  int cont_len;   // string literal length

  char *filename; // input filename
  char *input;    // entire input string
  int line_no;    // line number: for debugging
  int file_no;    // file number: for .loc directivbe
  bool at_bol;    // true if this token is at beginning of line
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
void convert_keywords(Token *tok);

Token *tokenize_file(char *path);
extern char *current_filename;

//
// preprocess.c
//
Token *preprocess(Token *tok);

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
  ND_COND,      // ? :
  ND_COMMA,     // ,
  ND_MEMBER,    // . (struct member)
  ND_EQ,        // ==
  ND_NE,        // !=
  ND_LT,        // <
  ND_LE,        // <=

  ND_IF,        // "if"
  ND_FOR,       // "for" or "while"
  ND_DO,        // "do"
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
  ND_NULL_EXPR, // do nothing

  ND_VAR,       // local variables
  ND_NUM,       // Integer
  ND_CAST,      // type cast
} NodeKind;

typedef struct Var Var;
struct Var {
  Var *next;
  char *name;
  Type *ty;
  Token *tok;     // representative token
  bool is_local;
  int align;

  // for local variables
  int offset;

  // for global variables
  bool is_static;
  char *init_data;
  Relocation *rel;
};

// used for gloval vars initialization using a pointer to another global vars
struct Relocation {
  Relocation *next;
  int offset;
  char *label;
  long addend;
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

  // assignment
  bool is_init;

  // block or statement espression
  Node *body;

  // struct member access
  Member *member;

  // Function call
  char *funcname;
  Type *func_ty;
  Var **args;
  int nargs;

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
  double fval;
};

typedef struct Function Function;
struct Function {
  Function *next;
  char *name;
  Var *params;
  bool is_static;
  bool is_variadic;

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
long const_expr(Token **rest, Token *tok);
Program *parse(Token *tok);

//
// codegen.c
//

void codegen(Program *prog);

//
// main.c
//
extern bool opt_E;

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
  TY_FLOAT,
  TY_DOUBLE,
  TY_PTR,
  TY_FUNC,
  TY_ARRAY,
  TY_STRUCT,
} TypeKind;

struct Type {
  TypeKind kind;
  int size;           // used as sizeof() value
  int align;          // alignment
  bool is_unsigned;   // unsigned or signed
  bool is_incomplete; // incomplete type
  bool is_const;      // const
  Type *base;

  // used for declaration
  Token *ident;
  Token *name_pos;

  // function
  Type *return_ty;
  Type *params;
  bool is_variadic;
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
  int align;
  int offset;
};

extern Type *ty_void;
extern Type *ty_bool;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;

extern Type *ty_float;
extern Type *ty_double;

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
bool is_flonum(Type *ty);
bool is_numeric(Type *ty);
bool is_pointer_like(Type *ty);
void generate_type(Node *node);
