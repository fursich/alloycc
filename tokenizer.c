#include "9cc.h"

//
// Tokenizer
//

Token *token;
char *current_filename;
static char *current_input;

void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

static void verror_at(int line_no, char *loc, char *fmt, va_list ap) {

  char *line = loc;
  while (current_input < line && line[-1] != '\n')
    line--;

  char *end = loc;
  while (*end != '\n')
    end++;

  // Print out the line
  int indent = fprintf(stderr, "%s:%d: ", current_filename, line_no);
  fprintf(stderr, "%.*s\n", (int)(end - line), line);

  // Show the error message
  int pos = loc - line + indent;

  fprintf(stderr, "%*s", pos, "");
  fprintf(stderr, "^ ");

  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

void error_at(char *loc, char *fmt, ...) {
  int line_no = 1;
  for (char *p = current_input; p < loc; p++) {
    if (*p == '\n')
      line_no++;
  }

  va_list ap;
  va_start(ap, fmt);

  verror_at(line_no, loc, fmt, ap);
}

void error_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  verror_at(tok->line_no, tok->str, fmt, ap);
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
  char *name = get_identifier(token);
  token = token->next;
  return name;
}

char *get_identifier(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_at(tok->str, "expected an identifier");
  char *name = strndup(tok->str, tok->len);
  return name;
}

char *expect_string() {
  if (token->kind != TK_STR)
    error_at(token->str, "expected a string");
  char *s = token->str;
  token = token->next;
  return s;
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

static bool is_keyword(Token *tok) {
  static const char *keywords[] = {
    "if",
    "else",
    "while",
    "for",
    "return",

    "sizeof",

    "char",
    "short",
    "int",
    "long",

    "struct",
    "union",
  };

  /* Keywords */
  for (int i = 0; i < sizeof(keywords) / sizeof(*keywords); i++) {
    const char *kw = keywords[i];
    int len = strlen(kw);

    if (strlen(kw) == tok->len && !strncmp(tok->str, kw, tok->len))
      return true;
  }
  return false;
}

static bool is_hex(char c) {
  return ('0' <= c && c <= '9') ||
         ('a' <= c && c <= 'f') ||
         ('A' <= c && c <= 'F');
}

static int from_hex(char c) {
  if ('0' <= c && c <= '9')
    return c - '0';
  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;
  if ('A' <= c && c <= 'F')
    return c - 'A' + 10;
}

static Token *read_operators(Token *cur, char *start) {
  static const char *multi_letter_ops[] = {
    "==",
    "!=",
    ">=",
    "<=",
    "->",
  };

  /* Reserved Symbol: Multi-letter punctuators */
  for (int i = 0; i < sizeof(multi_letter_ops) / sizeof(*multi_letter_ops); i++) {
    int len = strlen(multi_letter_ops[i]);

    if (startswith(start , multi_letter_ops[i])) {
      Token *token = new_token(TK_RESERVED, cur, start, len);
      return token;
    }
  }

  /* Reserved Symbol: Single-letter punctuators */
  /* NOTE: when put ahead of identifier detection, this will mis-detect identifiers that start with '_' */
  if (ispunct(*start)) {
    Token *token = new_token(TK_RESERVED, cur, start, 1);
    return token;
  }
}

static char read_escaped_char(char **pos, char *p) {
  if ('0' <= *p && *p <= '7') {
    // Read an octal number.
    int c = *p++ - '0';
    if ('0' <= *p && *p <= '7') {
      c = (c << 3) | (*p++ - '0');
      if ('0' <= *p && *p <= '7')
        c = (c << 3) | (*p++ - '0');
    }

    *pos = p;
    return c;
  }

  if (*p == 'x') {
    // Read a hexadecimal number.
    p++;
    if (!is_hex(*p))
      error_at(p, "invalid hex escape sequence");

    int c = 0;
    for (; is_hex(*p); *p++) {
      c = (c << 4) | from_hex(*p);
      if (c > 255)
        error_at(p, "hex escape sequence out of range");
    }

    *pos = p;
    return c;
  }

  *pos = p + 1;
  switch(*p) {
  case 'a': return '\a';
  case 'b': return '\b';
  case 't': return '\t';
  case 'n': return '\n';
  case 'v': return '\v';
  case 'f': return '\f';
  case 'r': return '\r';
  case 'e': return 27;
  default: return *p;
  }
}

static Token *read_string_literal(Token *cur, char *start) {
  char *p = start + 1;
  char *end = p;

  for (; *end != '"'; end++) {
    if (!*end)
      error_at(start, "unclosed string literal");
    if (*end == '\\')
      end++;
  }

  char *buf = malloc(end - p + 1);
  int len = 0;

  while (*p != '"') {
    if (*p == '\\')
      buf[len++] = read_escaped_char(&p, p + 1);
    else
      buf[len++] = *p++;
  }

  buf[len++] = '\0';

  Token *tok = new_token(TK_STR, cur, start, p - start + 1);
  tok->contents = buf;
  tok->cont_len = len;
  return tok;
}

static void convert_keywords(Token *tok) {
  for (Token *t = tok; t->kind != TK_EOF; t = t->next)
    if (t->kind == TK_IDENT && is_keyword(t))
      t->kind = TK_RESERVED;
}

static void add_line_info(Token *tok) {
  char *p = current_input;
  int line_no = 1;
  
  do {
    if (p == tok->str) {
      tok->line_no = line_no;
      tok = tok->next;
    }
    if (*p == '\n')
      line_no++;
  } while(*p++);
}

static Token *tokenize() {
  char *p = current_input;
  Token head;
  head.next = NULL;
  Token *cur = &head;

  while(*p) {
    /* Skip line comments */
    if (startswith(p, "//")) {
      p += 2;
      while (*p != '\n')
        p++;
      continue;
    }

    /* Skip block comments */
    if (startswith(p, "/*")) {
      char *q = strstr(p + 2, "*/");
      if (!q)
        error_at(p, "unclosed block comment");
      p = q + 2;
      continue;
    }

    if (isspace(*p)) {
      p++;
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

    /* String Literal */
    if (*p == '"') {
      cur = read_string_literal(cur, p);
      p += cur->len;
      continue;
    }

    /* Identifier and keywords*/
    if (is_alpha(*p)) {
      char *p0 = p++;
      while (is_alnum(*p))
        p++;
      cur = new_token(TK_IDENT, cur, p0, p - p0);
      continue;
    }

    /* Single-letter and multi-letter punctuators */
    /* NOTE: when put ahead of identifier detection, this will mis-detect identifiers that start with '_' */
    if (ispunct(*p)) {
      cur = read_operators(cur, p);
      p += cur->len;
      continue;
    }

    error_at(p, "invalid token");
  }

  new_token(TK_EOF, cur, p, 0);
  add_line_info(head.next);
  convert_keywords(head.next);

  return head.next;
}

static char *read_file(char *path) {
  FILE *fp;

  if (strcmp(path, "-") == 0) {
    // By convention, read from stdin if "-" is provided as filename
    fp = stdin;
  } else {
    fp = fopen(path, "r");
    if (!fp)
      error("cannot open %s: %s", path, strerror(errno));
  }

  int buflen = 4096;
  int nread = 0;
  char *buf = malloc(buflen);

  for (;;) {
    int end = buflen - 2;
    int n = fread(buf + nread, 1, end - nread, fp);
    if (n == 0)
      break;
    nread += n;
    if (nread == end) {
      buflen *= 2;
      buf = realloc(buf, buflen);
    }
  }

  if (fp != stdin)
    fclose(fp);

  // Canonicalize the source code by ensuring all the lines to end with "\n"
  if (nread == 0 || buf[nread - 1] != '\n')
    buf[nread++] = '\n';
  buf[nread] = '\0';
  return buf;
}

Token *tokenize_file(char *path) {
  current_input = read_file(path);
  current_filename = path;
  return tokenize();
}
