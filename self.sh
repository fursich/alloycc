#!/bin/bash -x
set -e
COMPILER=$1
BUILDDIR=$2

alloycc() {
    cat <<EOF > $BUILDDIR/$1
typedef struct FILE FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

typedef struct {
  int gp_offset;
  int fp_offset;
  void *overflow_arg_area;
  void *reg_save_area;
} va_list[1];

void *malloc(long size);
void *calloc(long nmenb, long size);
void *realloc(void *buf, long size);
int *__errno_location();
char *strerror(int errnum);
FILE *fopen(char *pathname, char *mode);
long fread(void *ptr, long size, long nmemb, FILE *stream);
int fclose(FILE *fp);
int feof(FILE *stream);
static void assert() {}
int strcmp(char *s1, char *s2);
static void va_end(va_list ap) {}
EOF

    grep -v '^#' alloycc.h >> $BUILDDIR/$1
    grep -v '^#' $1 >> $BUILDDIR/$1
    sed -i 's/\bbool\b/_Bool/g' $BUILDDIR/$1
    sed -i 's/\berrno\b/__errno_location()/g' $BUILDDIR/$1
    sed -i 's/\btrue\b/1/g' $BUILDDIR/$1
    sed -i 's/\bfalse\b/0/g' $BUILDDIR/$1
    sed -i 's/\bNULL\b/0/g' $BUILDDIR/$1
    sed -i 's/\bva_start\b/__builtin_va_start/g' $BUILDDIR/$1

    ./$COMPILER $BUILDDIR/$1 > $BUILDDIR/${1%.c}.s
    gcc -c -o $BUILDDIR/${1%.c}.o $BUILDDIR/${1%.c}.s
}

cc() {
    gcc -c -o $BUILDDIR/${1%.c}.o $1
}

alloycc main.c
alloycc type.c
cc parse.c
cc codegen.c
cc tokenize.c
