#include "alloycc.h"

int stack_size;

int main(int argc, char **argv) {
  if (argc != 2) {
    error("%s: invalid number of arguments", argv[0]);
    return 1;
  }

  Token *tok = tokenize_file(argv[1]);
  Program *prog = parse(tok);

  for (Function *fn = prog->fns; fn; fn = fn->next) {
    // first 32 bytes are reserved for callee saved resigisters
    // optionally 48 bytes can be used for variadic vars (if requried)
    // save R12-R15
    int offset = fn->is_variadic? 80 : 32;

    for (Var *var = fn->locals; var; var = var->next) {
      offset = align_to(offset, var->align);
      offset += size_of(var->ty);
      var->offset = offset;
    }
    fn->stack_size = align_to(offset, 16);
  }

  codegen(prog);
  return 0;
}
