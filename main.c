#include "9cc.h"

int stack_size;

int main(int argc, char **argv) {
  if (argc != 2) {
    error("引数の個数が正しくありません: %s\n", argv[0]);
    return 1;
  }

  user_input = argv[1];
  token = tokenize();
  ScopedContext *block = parse();

  int offset = 0;
  for (Var *var = block->locals; var; var = var->next) {
    offset += 8;
    var->offset = offset;
  }
  block->stack_size = offset;

  codegen(block);
  return 0;
}
