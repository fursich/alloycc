#!/bin/bash -x
set -e
COMPILER=$1
BUILDDIR=$2
TARGET=$3

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

struct stat {
  char _[512];
};

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
void exit(int status);
int stat(char *path, struct stat *statbuf);

static void va_end(va_list ap) {}
int printf(char *fmt, ...);
int sprintf(char *buf, char *fmt, ...);
int fprintf(FILE *stream, char *fmt);
int vfprintf(FILE *stream, char *fmt, va_list arg);

int strcmp(char *s1, char *s2);
long strlen(char *p);
int strncmp(char *p, char *q);
int strncasecmp(char *p, char *q, unsigned long count);
void *memcpy(char *dst, char *src, long n);
char *strndup(char *p, long n);
char *strncpy(char *dest, char *src, long n);
char strchr(const char *str, int c);
char *strstr(char *haystack, char *needle);
unsigned long int strtoul(char *str, char **endptr, int base);
double strtod(char *str, char **endptr);

int isspace(int c);
int ispunct(int c);
int isdigit(int c);

EOF

    grep -v '^#' alloycc.h >> $BUILDDIR/$1
    grep -v '^#' $1 >> $BUILDDIR/$1
    sed -i 's/\bbool\b/_Bool/g' $BUILDDIR/$1
    sed -i 's/\berrno\b/__errno_location()/g' $BUILDDIR/$1
    sed -i 's/\btrue\b/1/g' $BUILDDIR/$1
    sed -i 's/\bfalse\b/0/g' $BUILDDIR/$1
    sed -i 's/\bNULL\b/0/g' $BUILDDIR/$1
    sed -i 's/\bva_start\b/__builtin_va_start/g' $BUILDDIR/$1

    (cd $BUILDDIR; ../$COMPILER $1 > ${1%.c}.s)
    gcc -c -o $BUILDDIR/${1%.c}.o $BUILDDIR/${1%.c}.s
}

cc() {
    gcc -c -o $BUILDDIR/${1%.c}.o $1
}

alloycc main.c
alloycc type.c
alloycc parse.c
alloycc codegen.c
alloycc tokenize.c
alloycc preprocess.c

(cd $BUILDDIR; gcc -static -o ../$TARGET *.o)
