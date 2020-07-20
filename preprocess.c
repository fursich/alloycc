#include "alloycc.h"

//
// Preprocessor
//

// entry point function of the preprocessor
Token *preprocess(Token *tok) {
  convert_keywords(tok);
  return tok;
}
