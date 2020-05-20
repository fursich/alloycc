#include "9cc.h"

int stack_size;

int main(int argc, char **argv) {
  if (argc != 2) {
    error("%s: invalid number of arguments", argv[0]);
    return 1;
  }

  // TODO: user_input = argv[1];
  token = tokenize_file(argv[1]);
  Program *prog = parse();

  for (Function *fn = prog->fns; fn; fn = fn->next) {
    int offset = 0;

    for (Var *var = fn->locals; var; var = var->next) {
      offset = align_to(offset, var->ty->align);
      offset += var->ty->size;
      var->offset = offset;
    }
    fn->stack_size = align_to(offset, 16);
  }

  codegen(prog);
  return 0;
}
