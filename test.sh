#!/bin/bash

assert() {
  expected="$1"
  input="$2"

  echo "$input" | ./debug/9cc - > debug/tmp.s || exit
  cat <<-FUNC | gcc -c -xc -fno-common -o debug/tmp2.o -
    int ret1()  { return 1; }
    int ret42() { return 42; }
    int echo_self(int a) { return a; }
    int sum3(int a, int b, int c) { return a + b + c; }
	FUNC

  gcc -static -o debug/tmp debug/tmp.s debug/tmp2.o

  ./debug/tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected but got $actual"
    exit 1
  fi
}

assert 2 'int main() { int x=2; { int x=3; } return x; }'
assert 2 'int main() { int x=2; { int x=3; } { int y=4; return x; }}'
assert 3 'int main() { int x=2; { x=3; } return x; }'

assert 3 'int main() { int x=2; { int x=3; { return x; } } }'
assert 3 'int main() { int x=2; { int x=3; { int x=1; } return x; } }'
assert 2 'int main() { int x=2; { int x=3; { int x=1; } } return x; }'

assert 2 'int main() { /* return 0; */ return 2; }'
assert 2 'int main() { // return 1;
return 2; }'

assert 0 'int main() { return ({ 0; }); }'
assert 2 'int main() { return ({ 0; 1; 2; }); }'
assert 1 'int main() { ({ 0; return 1; 2; }); return 3; }'
assert 6 'int main() { return ({ 1; }) + ({ 2; }) + ({ 3; }); }'
assert 3 'int main() { return ({ int x=3; x; }); }'

assert 0 'int main() { return "\x00"[0]; }'
assert 119 'int main() { return "\x77"[0]; }'
assert 165 'int main() { return "\xA5"[0]; }'
assert 255 'int main() { return "\x00ff"[0]; }'

assert 0 'int main() { return "\0"[0]; }'
assert 16 'int main() { return "\20"[0]; }'
assert 65 'int main() { return "\101"[0]; }'
assert 104 'int main() { return "\1500"[0]; }'
assert 3 'int main() { return "\38"[0]; }'

assert 7 'int main() { return "\a"[0]; }'
assert 8 'int main() { return "\b"[0]; }'
assert 9 'int main() { return "\t"[0]; }'
assert 10 'int main() { return "\n"[0]; }'
assert 11 'int main() { return "\v"[0]; }'
assert 12 'int main() { return "\f"[0]; }'
assert 13 'int main() { return "\r"[0]; }'
assert 27 'int main() { return "\e"[0]; }'

assert 106 'int main() { return "\j"[0]; }'
assert 107 'int main() { return "\k"[0]; }'
assert 108 'int main() { return "\l"[0]; }'

assert 7 'int main() { return "\ax\ny"[0]; }'
assert 120 'int main() { return "\ax\ny"[1]; }'
assert 10 'int main() { return "\ax\ny"[2]; }'
assert 121 'int main() { return "\ax\ny"[3]; }'
assert 9 'int main() { return sizeof("\"ab\\cd\"\n"); }'

assert 97 'int main() { return "abc"[0]; }'
assert 98 'int main() { return "abc"[1]; }'
assert 99 'int main() { return "abc"[2]; }'
assert 0 'int main() { return "abc"[3]; }'
assert 4 'int main() { return sizeof("abc"); }'

assert 1 'int main() { char x=1; return x; }'
assert 1 'int main() { char x=1; char y=2; return x; }'
assert 2 'int main() { char x=1; char y=2; return y; }'

assert 1 'int main() { char x; return sizeof(x); }'
assert 10 'int main() { char x[10]; return sizeof(x); }'
assert 1 'int main() { return sub_char(7, 3, 3); } int sub_char(char a, char b, char c) { return a-b-c; }'
assert 3 'int main() { char x[3]; x[0]=-1; x[1]=2; int y=4; return x[0]+y; }'

assert 0 'int x; int main() { return x; }'
assert 3 'int x; int main() { x=3; return x; }'
assert 7 'int x; int y; int main() { x=3; y=4; return x+y; }'
assert 7 'int x, y; int main() { x=3; y=4; return x+y; }'
assert 0 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[0]; }'
assert 1 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[1]; }'
assert 2 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[2]; }'
assert 3 'int x[4]; int main() { x[0]=0; x[1]=1; x[2]=2; x[3]=3; return x[3]; }'

assert 8 'int x; int main() { return sizeof(x); }'
assert 32 'int x[4]; int main() { return sizeof(x); }'

assert 3 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; x[1]=4; x[2]=5; return *(x+2); }'

assert 0 'int main() { int x[2][3]; int *y=x; y[0]=0; return x[0][0]; }'
assert 1 'int main() { int x[2][3]; int *y=x; y[1]=1; return x[0][1]; }'
assert 2 'int main() { int x[2][3]; int *y=x; y[2]=2; return x[0][2]; }'
assert 3 'int main() { int x[2][3]; int *y=x; y[3]=3; return x[1][0]; }'
assert 4 'int main() { int x[2][3]; int *y=x; y[4]=4; return x[1][1]; }'
assert 5 'int main() { int x[2][3]; int *y=x; y[5]=5; return x[1][2]; }'

assert 0 'int main() { int x[2][3]; int *y=x; *y=0; return **x; }'
assert 1 'int main() { int x[2][3]; int *y=x; *(y+1)=1; return *(*x+1); }'
assert 2 'int main() { int x[2][3]; int *y=x; *(y+2)=2; return *(*x+2); }'
assert 3 'int main() { int x[2][3]; int *y=x; *(y+3)=3; return **(x+1); }'
assert 4 'int main() { int x[2][3]; int *y=x; *(y+4)=4; return *(*(x+1)+1); }'
assert 5 'int main() { int x[2][3]; int *y=x; *(y+5)=5; return *(*(x+1)+2); }'

assert 0 'int main() { int x[2][3]; int *y=x; *y=0; return **x; }'
assert 1 'int main() { int x[2][3]; int *y=x; *(y+1)=1; return *(*x+1); }'
assert 2 'int main() { int x[2][3]; int *y=x; *(y+2)=2; return *(*x+2); }'
assert 3 'int main() { int x[2][3]; int *y=x; *(y+3)=3; return **(x+1); }'
assert 4 'int main() { int x[2][3]; int *y=x; *(y+4)=4; return *(*(x+1)+1); }'
assert 5 'int main() { int x[2][3]; int *y=x; *(y+5)=5; return *(*(x+1)+2); }'

assert 3 'int main() { int x[2]; int *y=&x; *y=3; return *x; }'
assert 3 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *x; }'
assert 4 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+1); }'
assert 5 'int main() { int x[3]; *x=3; *(x+1)=4; *(x+2)=5; return *(x+2); }'

assert 8  'int main() { int x; return sizeof(x); }'
assert 8  'int main() { int x; return sizeof x; }'
assert 8  'int main() { int *x; return sizeof(x); }'
assert 8  'int main() { int x; return sizeof(&x); }'

assert 8  'int main() { int x = 4; return sizeof(x = x - 2); }'
assert 4  'int main() { int x = 4; sizeof(x = x - 2); return x; }'

# TODO: cast return type for function call (no reference to its declaration so far)
assert 9  'int **inc(int x) { return x + 1; } int main() { return inc(8); }'

assert 3  'int main() { int x = 7, y = 5; *(&x + 1) = 3; return y; }'
assert 3  'int main() { int x = 7, y = 5; *(1 + &x) = 3; return y; }'
assert 2  'int main() { int x, y, z; return &z - &x; }'
assert 3  'int main() { int x = 7, y = 5; *(&y - 1) = 3; return x; }'

assert 6  'int main() { int x = 3, *y = &x; return x + *y; }'
assert 2  'int *main() { int x = 3, y = x - 2; return x - y; }'
assert 28 'int main() { int x = 28, *y = &x, **z = &y; return **z; }'

assert 5  'int main() { int x = 3, y = 2; return x + y; }'
assert 2  'int main() { int x = 3, y = x - 2; return x - y; }'

assert 3  'int main() { int x = 3; return *&x; }'
assert 2  'int main() { int x = 2, y = &x; return *y; }'
assert 7  'int main() { int x = 9, y = 7; return *(&x + 1); }'
assert 9  'int main() { int x = 9, y = 7; return *(&y - 1); }'
assert 4  'int main() { int x = 9, y = &x; *y = 4; return x; }'
assert 7  'int main() { int x = 7, y = 5; *(&x + 1) = 3; return x; }'
assert 3  'int main() { int x = 7, y = 5; *(&x + 1) = 3; return y; }'
assert 4  'int main() { int x = 7, y = 5; *(&y - 1) = 4; return x; }'
assert 5  'int main() { int x = 7, y = 5; *(&y - 1) = 4; return y; }'

assert 9  'int inc(int x) { return x + 1; } int main() { return inc(8); }'
assert 4  'int sub2(int x, int y) { return x - y; } int main() { return sub2(6, 2); }'
assert 7  'int adder(int p, int q, int r, int x, int y, int z) { return p + q + r + x + y + z; } int main() { return adder(2, -5, 3, 3, -4, 8); }'
assert 89 '
  int fibo(int n) {
    if (n <= 0) {
      return 0;
    }
    if (n == 1) {
      return 1;
    }
    return fibo(n - 1) + fibo(n - 2);
  }
  int main() {
    return fibo(11);
  }
'

assert 3  'int foo() { return 3; } int main() { return foo(); }'
assert 4  'int foo() { int hoge = 1; return hoge; } int main() { int bar = 3; return foo() + bar; }'
assert 42 'int foo() { return 14; } int bar() { return 4; } int baz() { return 7; } int main() { return foo() + bar() * baz(); }'

assert 3  'int main() { return echo_self(3); }'
assert 42 'int main() { return sum3(7, 12, 23); }'
assert 7  'int main() { int foo = 3; int bar = 2; int baz = sum3(foo, -bar, foo * bar); return baz; }'

assert 1  'int main() { return ret1(); }'
assert 42 'int main() { return ret42(); }'

assert 2  'int main() { int foo = 1; foo = foo * 2; return foo; }'
assert 42 'int main() { int foo = 42; { int bar = foo * 2; bar = bar + 1;} return foo; }'
assert 0  'int main() { if (1 != 1) { int foo = 1; return foo; } else { int foo = 0; return foo; } }'
assert 1  'int main() { if (1 == 1) { int foo = 1; return foo; } else { int foo = 0; return foo; } }'


assert 16 'int main() { int foo = 1; int i; for (i=0; i<4; i = i + 1) foo = foo * 2; return foo; }'
assert 10 'int main() { int i = 0; for (; i<10; i = i + 1) 1 == 1; return i; }'
assert 42 'int main() { int i; for (i = 0; i<42;) i = i + 1; return i; }'
assert 42 'int main() { int i = 0; for (; i<42;) i = i + 1; return i; }'
assert 1  'int main() { for (;;) return 1; return 0; }'

assert 1  'int main() { int foo = 1; while (foo == 1) return 1; return 0; }'
assert 0  'int main() { int foo = 2; while (foo == 1) return 1; return 0; }'
assert 1  'int main() { int foo = 100; while (foo != 1) foo = foo - 1; return foo; }'
assert 42 'int main() { int foo = 42; int bar = 0; while ((foo = foo -1) >= 0) bar = bar + 1; return bar; }'

assert 1  'int main() { int foo = 1; if (foo == 1) return foo; return 0; }'
assert 0  'int main() { int foo = 2; if (foo == 1) return foo; return 0; }'
assert 1  'int main() { int foo = 1; if (foo == 1) return foo; else return 0; }'
assert 0  'int main() { int foo = 2; if (foo == 1) return foo; else return 0; }'
assert 1  'int main() { int foo = 1; int bar = 3; if (foo == 1) return 1; else if (bar == 3) return 3; else 10; }'
assert 3  'int main() { int foo = 2; int bar = 3; if (foo == 1) return 1; else if (bar == 3) return 3; return 10; }'
assert 10 'int main() { int foo = 2; int bar = 7; if (foo == 1) return 1; else if (bar == 3) return 3; else return 10; }'

assert 1  'int main() { int _foo = 1; return _foo; }'
assert 2  'int main() { int bar; int foo = bar = 2; return foo; }'
assert 3  'int main() { int bar; int foo = bar = 3; return bar; }'
assert 11 'int main() { int foo = 3; int bar0 = 2; int bar1 = foo - bar0; return bar1 + 10; }'
assert 6  'int main() { int foo = 3; int bar = 2; int _baz = 4; int quix = foo * 4 / (3 * bar - 2 * _baz); return -quix; }'

assert 1  'int main() { int a = 1; return a; }'
assert 2  'int main() { int z; int a = z = 2; return z; }'
assert 2  'int main() { int z; int a = z = 2; return a; }'
assert 1  'int main() { int a = 3; int b = 2; int c = a - b; return c; }'
assert 6  'int main() { int a = 3; int b = 2; int c = 4; int z = a * 4 / (3 * b - c); return z; }'

assert 1  'int main() { return 1; 2; 3; }'
assert 2  'int main() { 1; return 2; 3; }'
assert 3  'int main() { 1; 2; return 3; }'
assert 1  'int main() { return 1; return 2; return 3; }'

assert 0  'int main() { return 1 != 2 * 1 - 1; }'
assert 1  'int main() { return 7 == -5 * (-2) + 10 / (-2) - (-2); }'

assert 0  'int main() { return 0 > 1; }'
assert 0  'int main() { return 1 > 1; }'
assert 1  'int main() { return 1 > 0; }'
assert 0  'int main() { return 0 >= 1; }'
assert 1  'int main() { return 1 >= 1; }'
assert 1  'int main() { return 1 >= 0; }'

assert 0  'int main() { return 1 < 0; }'
assert 0  'int main() { return 1 < 1; }'
assert 1  'int main() { return 0 < 1; }'
assert 0  'int main() { return 1 <= 0; }'
assert 1  'int main() { return 1 <= 1; }'
assert 1  'int main() { return 0 <= 1; }'

assert 0  'int main() { return 0 == 1; }'
assert 1  'int main() { return 42 == 42; }'
assert 0  'int main() { return 42 != 42; }'
assert 1  'int main() { return 0 != 1; }'

assert 0  'int main() { return 0; }'
assert 42 'int main() { return 42; }'
assert 21 'int main() { return 5+20-4; }'
assert 41 'int main() { return 12 +  34 - 5 ; }'
assert 7  'int main() { return 3 * 12 -  34 + 5 ; }'
assert 4  'int main() { return 6 * (3 + 9) -  12 * 10 / 15 - 5 * (2 + 10); }'
assert 7  'int main() { return -5 * (-2) + 10 / (-2) - (-2); }'

echo OK
