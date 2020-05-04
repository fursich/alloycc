#include "9cc.h"

int stack_size;
Var *locals = NULL;

int main(int argc, char **argv) {
  if (argc != 2) {
    error("引数の個数が正しくありません: %s\n", argv[0]);
    return 1;
  }

  user_input = argv[1];
  token = tokenize();
  Node *node = program();

  int offset = 0;
  for (Var *var = locals; var; var = var->next) {
    offset += 8;
    var->offset = offset;
  }
  stack_size = offset;

  codegen(node);
  return 0;
}
