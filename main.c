#include "alloycc.h"

int stack_size;

static void usage(void) {
  fprintf(stderr, "alloycc [ -I<path> ] <file>\n");
  exit(1);
}

int main(int argc, char **argv) {
  char *filename = NULL;

  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--help"))
      usage();

    if (argv[i][0] == '-' && argv[i][1] != '\0')
      error("unknown argument: %s", argv[i]);

    filename = argv[i];
  }

  Token *tok = tokenize_file(filename);
  if (!tok)
    error("%s: %s", filename, strerror(errno));

  tok = preprocess(tok);
  Program *prog = parse(tok);

  for (Function *fn = prog->fns; fn; fn = fn->next) {
    // first 32 bytes are reserved for callee saved resigisters
    // additional 96 bytes can be used for variadic vars (if requried)
    int offset = fn->is_variadic? 128 : 32;

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
