#include "alloycc.h"

//
// Preprocessor
//

static bool is_hash(Token *tok) {
  return tok->at_bol && equal(tok, "#");
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
