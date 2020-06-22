#include <stdio.h>

int main() {
  printf("%ld\n", (long) (char)255);
  printf("%ld\n", (long) (unsigned char)255);
  printf("%ld\n", (long) (unsigned short)0xffff);
  printf("%ld\n", (long) (unsigned int)0xffffffff);
  printf("%ld\n", (long) ((unsigned int)1 * (char)-1));   // 0xffffffff
  printf("%ld\n", (long) ((unsigned short)1 * (char)-1)); // 1 (unsigned short will be up-cased as signed int)
  printf("%ld\n", ((signed char)-1) * (unsigned short)1);
}
