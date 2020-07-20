#include "alloycc.h"

//
// Preprocessor
//

static bool is_hash(Token *tok) {
  return tok->at_bol && equal(tok, "#");
}

// skip extraneous tokens
// (certain directives such as #include allow extraneous tokens
// before newline)
static Token *skip_line(Token *tok) {
  if (tok->at_bol)
    return tok;

  warn_tok(tok, "ignored as extra token");
  while (!tok->at_bol)
    tok = tok->next;

  return tok;
}

static Token *copy_token(Token *tok) {
  Token *t = malloc(sizeof(Token));
  *t = *tok;
  t->next = NULL;
  return t;
}

// append tok2 to the end of tok1
static Token *append(Token *tok1, Token *tok2) {
  if (!tok1 || tok1->kind == TK_EOF)
    return tok2;

  Token head = {};
  Token *cur = &head;

  for (; tok1 && tok1->kind != TK_EOF; tok1 = tok1->next)
    cur = cur->next = copy_token(tok1);

  cur->next = tok2;
  return head.next;
}

// visit all tokens in `tok` while evaluating preprocessing
// macros and directives
static Token *preprocess2(Token *tok) {
  Token head = {};
  Token *cur = &head;

  while (tok->kind != TK_EOF) {
    // pass through if it is not a "#"
    if (!is_hash(tok)) {
      cur = cur->next = tok;
      tok = tok->next;
      continue;
    }

    tok = tok->next;

    if (equal(tok, "include")) {
      tok = tok->next;

      if (tok->kind != TK_STR)
        error_tok(tok, "expected a filename");

      char *path = tok->contents;
      Token *tok2 = tokenize_file(path);
      if (!tok2)
        error_tok(tok, "%s", strerror(errno));

      tok = skip_line(tok->next);
      tok = append(tok2, tok);
      continue;
    }

    // NOTE: `#`-only lines are legal ("null directives")
    if (tok->at_bol)
      continue;

    error_tok(tok, "invalid preprocessor directive");
  }

  cur->next = tok;
  return head.next;
}

// entry point function of the preprocessor
Token *preprocess(Token *tok) {
  tok = preprocess2(tok);
  convert_keywords(tok);
  return tok;
}
