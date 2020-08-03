#!/bin/bash -x
set -e
COMPILER=./$1
BUILDDIR=$2
TARGET=$3

alloycc() {
#     (cd $BUILDDIR && \
#       ../$COMPILER -I../include -I/usr/local/include -I/usr/include \
#       -I/usr/include/linux -I/usr/include/x86_64-linux-gnu \
#       ../$1 > ${1%.c}.s)
#     gcc -c -o $BUILDDIR/${1%.c}.o $BUILDDIR/${1%.c}.s
    $COMPILER -Iinclude -I/usr/local/include -I/usr/include \
      -I/usr/include/linux -I/usr/include/x86_64-linux-gnu \
      $1 > $BUILDDIR/${1%.c}.s
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
