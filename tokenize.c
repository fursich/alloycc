#include "alloycc.h"

//
// Tokenizer
//

char *current_filename;
static char *current_input;

void error(char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  exit(1);
}

static void verror_at(char *filename, char *input, int line_no,
                      char *loc, char *fmt, va_list ap) {

  char *line = loc;
  while (input < line && line[-1] != '\n')
    line--;

  char *end = loc;
  while (*end && *end != '\n')
    end++;

  // Print out the line
  int indent = fprintf(stderr, "%s:%d: ", filename, line_no);
  fprintf(stderr, "%.*s\n", (int)(end - line), line);

  // Show the error message
  int pos = loc - line + indent;

  fprintf(stderr, "%*s", pos, "");
  fprintf(stderr, "^ ");

  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
}

void error_at(char *loc, char *fmt, ...) {
  int line_no = 1;
  for (char *p = current_input; p < loc; p++) {
    if (*p == '\n')
      line_no++;
  }

  va_list ap;
  va_start(ap, fmt);

  verror_at(current_filename, current_input, line_no, loc, fmt, ap);
  exit(1);
}

void error_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  verror_at(tok->filename, tok->input, tok->line_no, tok->str, fmt, ap);
  exit(1);
}

void warn_tok(Token *tok, char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  verror_at(tok->filename, tok->input, tok->line_no, tok->str, fmt, ap);
}

/* compare token name (str) without consuming it (no checks are done against its kind) */
bool equal(Token *tok, char *op) {
  return strlen(op) == tok->len && !strncmp(tok->str, op, tok->len);
}

/* consume token if its name (str) equals to the given *op (no checks against its kind) */
bool consume(Token **rest, Token *tok, char *op) {
  if (equal(tok, op)) {
    *rest = tok->next;
    return true;
  }
  *rest = tok;
  return false;
}

/* assert token name (str) equals to *op (no checks against its kind) */
Token *skip(Token *tok, char *op) {
  if (!equal(tok, op))
    error_tok(tok, "expected '%s'", op);
  return tok->next;
}

char *expect_ident(Token **rest, Token *tok) {
  char *name = get_identifier(tok);
  *rest = tok->next;
  return name;
}

char *get_identifier(Token *tok) {
  if (tok->kind != TK_IDENT)
    error_tok(tok, "expected an identifier");
  char *name = strndup(tok->str, tok->len);
  return name;
}

char *expect_string(Token **rest, Token *tok) {
  if (tok->kind != TK_STR)
    error_tok(tok, "expected a string");
  char *s = tok->str;
  *rest = tok->next;
  return s;
}

static Token *new_token(TokenKind kind, Token *cur, char *str, int len) {
  Token *tok = calloc(1, sizeof(Token));
  tok->kind = kind;
  tok->str = str;
  tok->len = len;
  tok->filename = current_filename;
  tok->input = current_input;
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
    "do",
    "while",
    "for",
    "break",
    "continue",

    "goto",
    "switch",
    "case",
    "default",

    "return",

    "sizeof",
    "alignof",
    "_Alignas",
    "extern",
    "static",
    "const",
    "volatile",

    "void",
    "_Bool",
    "enum",

    "signed",
    "unsigned",

    "char",
    "short",
    "int",
    "long",
    "float",
    "double",

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
  // longer keywords must come earlier
  static const char *multi_letter_ops[] = {
    "<<=",
    ">>=",
    "...",

    "==",
    "!=",
    ">=",
    "<=",
    "->",
    "+=",
    "-=",
    "*=",
    "/=",
    "%=",
    "++",
    "--",
    "&=",
    "|=",
    "^=",
    "&&",
    "||",
    "<<",
    ">>",
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

static Token *read_char_literal(Token *cur, char *start) {
  char *p = start + 1;

  if (*p == '\0')
    error_at(start, "unclosed char literal");
  
  char c;
  if (*p == '\\')
    c = read_escaped_char(&p, p + 1);
  else
    c = *p++;

  if (*p != '\'')
    error_at(p, "char literal too long");
  p++;

  Token *tok = new_token(TK_NUM, cur, start, p - start);
  tok->val = c;
  tok->ty = ty_int;
  return tok;
}

static Token *read_int_literal(Token *cur, char *start) {
  char *p = start;

  // check if the number is a binary, octal , decimal, or hexadecimal
  // to determine the base
  int base = 10;
  if (!strncasecmp(p, "0x", 2) && is_hex(p[2])) {
    p += 2;
    base = 16;
  } else if (!strncasecmp(p, "0b", 2) && (p[2] == '0' || p[2] == '1')) {
    p += 2;
    base = 2;
  } else if (*p == '0') {
    base = 8;
  }

  long val = strtoul(p, &p, base);

  // read U, L or LL suffixes if any to determine the integer type
  bool l = false;
  bool u = false;

  if (startswith(p, "LLU") || startswith(p, "LLu") ||
      startswith(p, "llU") || startswith(p, "llu") ||
      startswith(p, "ULL") || startswith(p, "Ull") ||
      startswith(p, "uLL") || startswith(p, "ull")) {
    p += 3;
    l = u = true;
  } else if (!strncasecmp(p, "lu", 2) || !strncasecmp(p, "ul", 2) ) {
    p += 2;
    l = u = true;
  } else if (startswith(p, "LL") || startswith(p, "ll") ) {
    p += 2;
    l = true;
  } else if (*p == 'L' || *p == 'l') {
    p++;
    l = true;
  } else if (*p == 'U' || *p == 'u') {
    p++;
    u = true;
  }

  // infer a type
  Type *ty;
  if (base == 10) {
    if (l && u)
      ty = ty_ulong;
    else if (l)
      ty = ty_long;
    else if (u)
      ty = (val >> 32) ? ty_ulong : ty_uint;
    else
      ty = (val >> 31) ? ty_long : ty_int;
  } else {
    if (l && u)
      ty = ty_ulong;
    else if (l)
      ty = (val >> 63) ? ty_ulong : ty_long;
    else if (u)
      ty = (val >> 32) ? ty_ulong : ty_uint;
    else if (val >> 63)
      ty = ty_ulong;
    else if (val >> 32)
      ty = ty_long;
    else if (val >> 31)
      ty = ty_uint;
    else
      ty = ty_int;
  }

  Token *tok = new_token(TK_NUM, cur, start, p - start);
  tok->val = val;
  tok->ty = ty;
  return tok;
}

static Token *read_number(Token *cur, char *start) {
  // try parsing the current literal as integer constant
  Token *tok = read_int_literal(cur, start);
  if (!strchr(".eEfF", start[tok->len]))
    return tok;

  // must be a floating point const, if not an integer
  char *end;
  double fval = strtod(start, &end);

  Type *ty;
  if (*end == 'f' || *end == 'F') {
    ty = ty_float;
    end++;
  } else if (*end == 'l' || *end == 'L') {
    ty = ty_double;
    end++;
  } else {
    ty = ty_double;
  }

  // discard the integer token, and rebuild one as floating point
  tok = new_token(TK_NUM, cur, start, end - start);
  tok->fval = fval;
  tok->ty = ty;
  return tok;
}

void convert_keywords(Token *tok) {
  for (Token *t = tok; t->kind != TK_EOF; t = t->next)
    if (t->kind == TK_IDENT && is_keyword(t))
      t->kind = TK_RESERVED;
}

static void add_line_info(Token *tok) {
  char *p = current_input;
  int line_no = 1;
  bool at_bol = true;
  bool has_space = false;
  
  do {
    if (p == tok->str) {
      tok->line_no = line_no;
      tok->at_bol = at_bol;
      tok->has_space = has_space;
      tok = tok->next;
    }

    if (*p == '\n') {
      line_no++;
      at_bol = true;
    } else if (isspace(*p)) {
      has_space = true;
    } else {
      at_bol = false;
      has_space = false;
    }
  } while(*p++);
}

static Token *tokenize(char *filename, int file_no, char *p) {
  current_filename = filename;
  current_input = p;

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

    /* Numeric Literal */
    if (isdigit(*p) || (p[0] == '.' && isdigit(p[1]))) {
      cur = read_number(cur, p);
      p += cur->len;
      continue;
    }

    /* String Literal */
    if (*p == '"') {
      cur = read_string_literal(cur, p);
      p += cur->len;
      continue;
    }

    /* Character literal */
    if (*p == '\'') {
      cur = read_char_literal(cur, p);
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
  for (Token *t = head.next; t; t = t->next)
    t->file_no = file_no;
  add_line_info(head.next);

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
      return NULL;
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
  char *p = read_file(path);
  if (!p)
    return NULL;

  static int file_no;
  if (!opt_E)
    printf(".file %d \"%s\"\n", ++file_no, path);

  return tokenize(path, file_no, p);
}
