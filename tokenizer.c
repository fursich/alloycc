#include "9cc.h"

//
// Tokenizer
//

char *user_input;
Token *token;

void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

static void verror_at(char *loc, char *fmt, va_list ap) {
  int pos = loc - user_input;
  fprintf(stderr, "%s\n", user_input);
  fprintf(stderr, "%*s", pos, "");
  fprintf(stderr, "^ ");

  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

void error_at(char *loc, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  verror_at(loc, fmt, ap);
}

void error_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  verror_at(tok->str, fmt, ap);
}

/* compare token name (str) without consuming it (no checks are done against its kind) */
bool equal(char *op) {
  return strlen(op) == token->len && !strncmp(token->str, op, token->len);
}

/* consume token if its name (str) equals to the given *op (no checks against its kind) */
bool consume(char *op) {
  if (equal(op)) {
    token = token->next;
    return true;
  }
  return false;
}

/* assert token name (str) equals to *op and consumes that token (no checks against its kind) */
void expect(char *op) {
  if (!equal(op))
    error_at(token->str, "expected '%s'", op);
  token = token->next;
}

char *expect_ident() {
  if (token->kind != TK_IDENT)
    error_at(token->str, "expected an identifier");
  char *name = strndup(token->str, token->len);
  token = token->next;
  return name;
}

int expect_number() {
  if (token->kind != TK_NUM)
    error_at(token->str, "expected a number");
  int val = token->val;
  token = token->next;
  return val;
}

bool at_eof() {
  return token->kind == TK_EOF;
}

static Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
  Token *tok = calloc(1, sizeof(Token));
  tok->kind = kind;
  tok->str = str;
  tok->len = len;
  cur->next = tok;
  return tok;
}

static bool startswith(char *p, const char *q) {
  return strncmp(p, q, strlen(q)) == 0;
}

static bool is_alpha(char c) {
  return ('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') || c == '_';
}

static bool is_alnum(char c) {
  return is_alpha(c) || ('0' <= c && c <='9');
}

static const char *starts_with_reserved(char *p) {

  static const char *keywords[] = {
    "if",
    "else",
    "while",
    "for",
    "return",
    "int",
    "sizeof",
  };

  /* Keywords */
  for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
    int len = strlen(keywords[i]);

    if (startswith(p, keywords[i]) && !is_alnum(p[len]))
      return keywords[i];
  }

  static const char *operators[] = {
    "==",
    "!=",
    ">=",
    "<=",
  };

  /* Reserved Symbol: Multi-letter punctuators */
  for (int i = 0; i < sizeof(operators) / sizeof(*operators); i++) {
    int len = strlen(operators[i]);

    if (startswith(p, operators[i]) && !is_alnum(p[len]))
      return operators[i];
  }

  return NULL;
}

Token *tokenize() {
  char *p = user_input;
  Token head;
  head.next = NULL;
  Token *cur = &head;

  while(*p) {
    if (isspace(*p)) {
      p++;
      continue;
    }

    /* Keywords and multi-letter Symbols */
    const char *kw = starts_with_reserved(p);
    if (kw) {
      int len = strlen(kw);
      cur = new_token(TK_RESERVED, cur, p, len);
      p += len;
      continue;
    }

    /* Identifier */
    if (is_alpha(*p)) {
      char *p0 = p++;
      while (is_alnum(*p))
        p++;
      cur = new_token(TK_IDENT, cur, p0, p - p0);
      continue;
    }

    /* Reserved Symbol: Single-letter punctuators */
    /* NOTE: when put ahead of identifier detection, this will mis-detect identifiers that start with '_' */
    if (ispunct(*p)) {
      cur = new_token(TK_RESERVED, cur, p++, 1);
      continue;
    }

    /* Integer Literal */
    if (isdigit(*p)) {
      cur = new_token(TK_NUM, cur, p, 0);
      char *q = p;
      cur->val = strtol(p, &p, 10);
      cur->len = p - q;
      continue;
    }

    error_at(p, "invalid token");
  }

  new_token(TK_EOF, cur, p, 0);
  return head.next;
}
