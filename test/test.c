
int assert(int expected, int actual, char *code) {
  if (expected == actual) {
    printf("%s => %d\n", code, actual);
  } else {
    printf("%s => %d expected, but got %d\n", code, expected, actual);
    exit(1);
  }
  return 0;
}

int g1, g2[4];

int echo_self(int i) {
  return i;
}

int inc(int n) {
  return n + 1;
}

int sum3(int i, int j, int k) {
  return i + j + k;
}

int sub2(int i, int j) {
  return i - j;
}

int fibo(int n) {
  if (n <= 1)
    return 1;
  return fibo(n - 1) + fibo(n - 2);
}

int sub_char(char a, char b, char c) {
  return a-b-c;
}

int main() {

  assert(3, ({ struct {int a,b;} x,y; x.a=3; y=x; y.a; }), "({ struct {int a,b;} x,y; x.a=3; y=x; y.a; })");
  assert(5, ({ struct t {int a,b;}; struct t x; x.a=5; struct t y=x; y.a; }), "({ struct t {int a,b;}; struct t x; x.a=5; struct t y=x; y.a; })");
  assert(7, ({ struct t {int a,b;}; struct t x; x.a=7; struct t y; struct t *z=&y; *z=x; y.a; }), "({ struct t {int a,b;}; struct t x; x.a=7; struct t y; struct t *z=&y; *z=x; y.a; })");
  assert(7, ({ struct t {int a,b;}; struct t x; x.a=7; struct t y, *p=&x, *q=&y; *q=*p; y.a; }), "({ struct t {int a,b;}; struct t x; x.a=7; struct t y, *p=&x, *q=&y; *q=*p; y.a; })");
  assert(5, ({ struct t {char a, b;} x, y; x.a=5; y=x; y.a; }), "({ struct t {char a, b;} x, y; x.a=5; y=x; y.a; })");

  assert(8, ({ union { int a; char b[6]; } x; sizeof(x); }), "({ union { int a; char b[6]; } x; sizeof(x); })");
  assert(3, ({ union { int a; char b[4]; } x; x.a = 515; x.b[0]; }), "({ union { int a; char b[4]; } x; x.a = 515; x.b[0]; })");
  assert(2, ({ union { int a; char b[4]; } x; x.a = 515; x.b[1]; }), "({ union { int a; char b[4]; } x; x.a = 515; x.b[1]; })");
  assert(0, ({ union { int a; char b[4]; } x; x.a = 515; x.b[2]; }), "({ union { int a; char b[4]; } x; x.a = 515; x.b[2]; })");
  assert(0, ({ union { int a; char b[4]; } x; x.a = 515; x.b[3]; }), "({ union { int a; char b[4]; } x; x.a = 515; x.b[3]; })");

  assert(3, ({ struct t {char a;} x; struct t *y = &x; x.a=3; y->a; }), "({ struct t {char a;} x; struct t *y = &x; x.a=3; y->a; })");
  assert(3, ({ struct t {char a;} x; struct t *y = &x; y->a=3; x.a; }), "({ struct t {char a;} x; struct t *y = &x; y->a=3; x.a; })");

  assert(8, ({ struct t {int a; int b;} x; struct t y; sizeof(y); }), "({ struct t {int a; int b;} x; struct t y; sizeof(y); })");
  assert(8, ({ struct t {int a; int b;}; struct t y; sizeof(y); }), "({ struct t {int a; int b;}; struct t y; sizeof(y); })");
  assert(2, ({ struct t {char a[2];}; { struct t {char a[4];}; } struct t y; sizeof(y); }), "({ struct t {char a[2];}; { struct t {char a[4];}; } struct t y; sizeof(y); })");
  assert(3, ({ struct t {int x;}; int t=1; struct t y; y.x=2; t+y.x; }), "({ struct t {int x;}; int t=1; struct t y; y.x=2; t+y.x; })");

  assert(7, ({ int x; int y; char z; char *a=&y; char *b=&z; b-a; }), "({ int x; int y; char z; char *a=&y; char *b=&z; b-a; })");
  assert(1, ({ int x; char y; int z; char *a=&y; char *b=&z; b-a; }), "({ int x; char y; int z; char *a=&y; char *b=&z; b-a; })");

  assert(1, ({ struct {int a; int b;} x; x.a=1; x.b=2; x.a; }), "({ struct {int a; int b;} x; x.a=1; x.b=2; x.a; })");
  assert(2, ({ struct {int a; int b;} x; x.a=1; x.b=2; x.b; }), "({ struct {int a; int b;} x; x.a=1; x.b=2; x.b; })");
  assert(1, ({ struct {char a; int b; char c;} x; x.a=1; x.b=2; x.c=3; x.a; }), "({ struct {char a; int b; char c;} x; x.a=1; x.b=2; x.c=3; x.a; })");
  assert(2, ({ struct {char a; int b; char c;} x; x.b=1; x.b=2; x.c=3; x.b; }), "({ struct {char a; int b; char c;} x; x.b=1; x.b=2; x.c=3; x.b; })");
  assert(3, ({ struct {char a; int b; char c;} x; x.a=1; x.b=2; x.c=3; x.c; }), "({ struct {char a; int b; char c;} x; x.a=1; x.b=2; x.c=3; x.c; })");

  assert(0, ({ struct {int a; int b;} x[3]; int *p=x; p[0]=0; x[0].a; }), "({ struct {int a; int b;} x[3]; int *p=x; p[0]=0; x[0].a; })");
  assert(1, ({ struct {int a; int b;} x[3]; int *p=x; p[1]=1; x[0].b; }), "({ struct {int a; int b;} x[3]; int *p=x; p[1]=1; x[0].b; })");
  assert(2, ({ struct {int a; int b;} x[3]; int *p=x; p[2]=2; x[1].a; }), "({ struct {int a; int b;} x[3]; int *p=x; p[2]=2; x[1].a; })");
  assert(3, ({ struct {int a; int b;} x[3]; int *p=x; p[3]=3; x[1].b; }), "({ struct {int a; int b;} x[3]; int *p=x; p[3]=3; x[1].b; })");

  assert(6, ({ struct {int a[3]; int b[5];} x; int *p=&x; x.a[0]=6; p[0]; }), "({ struct {int a[3]; int b[5];} x; int *p=&x; x.a[0]=6; p[0]; })");
  assert(7, ({ struct {int a[3]; int b[5];} x; int *p=&x; x.b[0]=7; p[3]; }), "({ struct {int a[3]; int b[5];} x; int *p=&x; x.b[0]=7; p[3]; })");

  assert(6, ({ struct { struct { int b; } a; } x; x.a.b=6; x.a.b; }), "({ struct { struct { int b; } a; } x; x.a.b=6; x.a.b; })");

  assert(4, ({ struct {int a;} x; sizeof(x); }), "({ struct {int a;} x; sizeof(x); })");
  assert(8, ({ struct {int a; int b;} x; sizeof(x); }), "({ struct {int a; int b;} x; sizeof(x); })");
  assert(8, ({ struct {int a, b;} x; sizeof(x); }), "({ struct {int a, b;} x; sizeof(x); })");
  assert(12, ({ struct {int a[3];} x; sizeof(x); }), "({ struct {int a[3];} x; sizeof(x); })");
  assert(16, ({ struct {int a;} x[4]; sizeof(x); }), "({ struct {int a;} x[4]; sizeof(x); })");
  assert(24, ({ struct {int a[3];} x[2]; sizeof(x); }), "({ struct {int a[3];} x[2]; sizeof(x); })");
  assert(2, ({ struct {char a; char b;} x; sizeof(x); }), "({ struct {char a; char b;} x; sizeof(x); })");
  assert(8, ({ struct {char a; int b;} x; sizeof(x); }), "({ struct {char a; int b;} x; sizeof(x); })");
  assert(8, ({ struct {int a; char b;} x; sizeof(x); }), "({ struct {int a; char b;} x; sizeof(x); })");

  assert(3, (1,2,3), "(1,2,3)");
  assert(5, ({ int i=2, j=3; (i=5,j)=6; i; }), "({ int i=2, j=3; (i=5,j)=6; i; })");
  assert(6, ({ int i=2, j=3; (i=5,j)=6; j; }), "({ int i=2, j=3; (i=5,j)=6; j; })");

  assert( 2, ({ int x=2; { int x=3; } x; }), "({ int x=2; { int x=3; } x; })");
  assert( 3, ({ int x=2; { x=3; } x; }), "({ int x=2; { x=3; } x; })");
  assert( 2, ({ int x=2; { int x=3; { int x=1; } } x; }), "({ int x=2; { int x=3; { int x=1; } } x; })");
  assert( 2, /* return 0; */ 2, "/* return 0; */ 2;");
  assert( 3, // return 0;
             3, "// return 0;/n 3; ");

  assert( 0, "\x00"[0], "\"\\x00\"[0]");
  assert( 119, "\x77"[0], "\"\\x77\"[0]");

  assert( 0, "\0"[0], "\"\\0\"[0]");
  assert( 16, "\20"[0], "\"\\20\"[0]");
  assert( 65, "\101"[0], "\"\\101\"[0]");
  assert( 104, "\1500"[0], "\"\\1500\"[0]");
  assert( 3, "\38"[0], "\"\38\"[0]");

  assert( 7, "\a"[0], "\"\\a\"[0]");
  assert( 8, "\b"[0], "\"\\b\"[0]");
  assert( 9, "\t"[0], "\"\\t\"[0]");
  assert( 10, "\n"[0], "\"\\n\"[0]");
  assert( 11, "\v"[0], "\"\\v\"[0]");
  assert( 12, "\f"[0], "\"\\f\"[0]");
  assert( 13, "\r"[0], "\"\\r\"[0]");
  assert( 27, "\e"[0], "\"\\e\"[0]");

  assert( 106, "\j"[0], "\"\\j\"[0]");
  assert( 107, "\k"[0], "\"\\k\"[0]");
  assert( 108, "\l"[0], "\"\\l\"[0]");

  assert( 7, "\ax\ny"[0], "\"\\ax\\ny\"[0]");
  assert( 120, "\ax\ny"[1], "\"\\ax\\ny\"[1]");
  assert( 10, "\ax\ny"[2], "\"\\ax\\ny\"[2]");
  assert( 121, "\ax\ny"[3], "\"\\ax\\ny\"[3]");
  assert( 9, sizeof("\"ab\\cd\"\n"), "sizeof(\"\\\"ab\\\\cd\\\"\\n\")");

  assert( 97, "abc"[0], "\"abc\"[0]");
  assert( 98, "abc"[1], "\"abc\"[1]");
  assert( 99, "abc"[2], "\"abc\"[2]");
  assert( 0, "abc"[3], "\"abc\"[3]");
  assert( 4, sizeof("abc"), "sizeof(\"abc\")");

  assert( 1, ({ char x=1; x; }), "({ char x=1; x; })");
  assert( 1, ({ char x=1; char y=2; x; }), "({ char x=1; char y=2; x; })");
  assert( 2, ({ char x=1; char y=2; y; }), "({ char x=1; char y=2; y; })");
  assert( 1, ({ char x; sizeof(x); }), "({ char x; sizeof(x); })");
  assert( 10, ({ char x[10]; sizeof(x); }), "({ char x[10]; sizeof(x); })");
  assert( 1, ({ sub_char(7, 3, 3); }), "({ sub_char(7, 3, 3); })");
  assert( 3, ({ char x[3]; x[0]=-1; x[1]=2; int y=4; x[0]+y; }), "({ char x[3]; x[0]=-1; x[1]=2; int y=4; x[0]+y; })");

  assert( 0, g1, "g1");
  assert( 3, ({ g1=3; g1; }), "({ g1=3; g1; })");
  assert( 7, ({ g1=3; int x=4; g1+x; }), "({ g1=3; int x=4; g1+x; })");
  assert( 0, ({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[0]; }) ,"({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[0]; })");
  assert( 1, ({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[1]; }) ,"({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[1]; })");
  assert( 2, ({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[2]; }) ,"({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[2]; })");
  assert( 3, ({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[3]; }) ,"({ g2[0]=0; g2[1]=1; g2[2]=2; g2[3]=3; g2[3]; })");

  assert( 0, ({ int x[4]; x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[0]; }), "({ x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[0]; })");
  assert( 1, ({ int x[4]; x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[1]; }), "({ x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[1]; })");
  assert( 2, ({ int x[4]; x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[2]; }), "({ x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[2]; })");
  assert( 3, ({ int x[4]; x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[3]; }), "({ x[0]=0; x[1]=1; x[2]=2; x[3]=3; x[3]; })");

  assert( 4, ({ int x; sizeof(x); }), "({ int x; sizeof(x); })");
  assert( 16, ({ int x[4]; sizeof(x); }), "({ int x; sizeof(x); })");

  assert( 3, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *x; }), "({ int x[3]; *x=3; x[1]=4; x[2]=5; *x; })");
  assert( 4, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+1); }), "({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+1); })");
  assert( 5, ({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+2); }), "({ int x[3]; *x=3; x[1]=4; x[2]=5; *(x+2); })");

  assert( 0, ({ int x[2][3]; int *y=x; y[0]=0; x[0][0]; }), "({ int x[2][3]; int *y=x; y[0]=0; x[0][0]; })");
  assert( 1, ({ int x[2][3]; int *y=x; y[1]=1; x[0][1]; }), "({ int x[2][3]; int *y=x; y[1]=1; x[0][1]; })");
  assert( 2, ({ int x[2][3]; int *y=x; y[2]=2; x[0][2]; }), "({ int x[2][3]; int *y=x; y[2]=2; x[0][2]; })");
  assert( 3, ({ int x[2][3]; int *y=x; y[3]=3; x[1][0]; }), "({ int x[2][3]; int *y=x; y[3]=3; x[1][0]; })");
  assert( 4, ({ int x[2][3]; int *y=x; y[4]=4; x[1][1]; }), "({ int x[2][3]; int *y=x; y[4]=4; x[1][1]; })");
  assert( 5, ({ int x[2][3]; int *y=x; y[5]=5; x[1][2]; }), "({ int x[2][3]; int *y=x; y[5]=5; x[1][2]; })");

  assert( 0, ({ int x[2][3]; int *y=x; *y=0; **x; }), "({ int x[2][3]; int *y=x; *y=0; **x; })");
  assert( 1, ({ int x[2][3]; int *y=x; *(y+1)=1; *(*x+1); }), "({ int x[2][3]; int *y=x; *(y+1)=1; *(*x+1); })");
  assert( 2, ({ int x[2][3]; int *y=x; *(y+2)=2; *(*x+2); }), "({ int x[2][3]; int *y=x; *(y+2)=2; *(*x+2); })");
  assert( 3, ({ int x[2][3]; int *y=x; *(y+3)=3; **(x+1); }), "({ int x[2][3]; int *y=x; *(y+3)=3; **(x+1); })");
  assert( 4, ({ int x[2][3]; int *y=x; *(y+4)=4; *(*(x+1)+1); }), "({ int x[2][3]; int *y=x; *(y+4)=4; *(*(x+1)+1); })");
  assert( 5, ({ int x[2][3]; int *y=x; *(y+5)=5; *(*(x+1)+2); }), "({ int x[2][3]; int *y=x; *(y+5)=5; *(*(x+1)+2); })");

  assert( 0, ({ int x[2][3]; int *y=x; *y=0; **x; }), "({ int x[2][3]; int *y=x; *y=0; **x; })");
  assert( 1, ({ int x[2][3]; int *y=x; *(y+1)=1; *(*x+1); }), "({ int x[2][3]; int *y=x; *(y+1)=1; *(*x+1); })");
  assert( 2, ({ int x[2][3]; int *y=x; *(y+2)=2; *(*x+2); }), "({ int x[2][3]; int *y=x; *(y+2)=2; *(*x+2); })");
  assert( 3, ({ int x[2][3]; int *y=x; *(y+3)=3; **(x+1); }), "({ int x[2][3]; int *y=x; *(y+3)=3; **(x+1); })");
  assert( 4, ({ int x[2][3]; int *y=x; *(y+4)=4; *(*(x+1)+1); }), "({ int x[2][3]; int *y=x; *(y+4)=4; *(*(x+1)+1); })");
  assert( 5, ({ int x[2][3]; int *y=x; *(y+5)=5; *(*(x+1)+2); }), "({ int x[2][3]; int *y=x; *(y+5)=5; *(*(x+1)+2); })");

  assert( 3, ({ int x[2]; int *y=&x; *y=3; *x; }), "({ int x[2]; int *y=&x; *y=3; *x; })");
  assert( 3, ({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *x; }), "({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *x; })");
  assert( 4, ({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *(x+1); }), "({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *(x+1); })");
  assert( 5, ({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *(x+2); }), "({ int x[3]; *x=3; *(x+1)=4; *(x+2)=5; *(x+2); })");

  assert( 4, ({ int x; sizeof(x); }), "({ int x; sizeof(x); })");
  assert( 4, ({ int x; sizeof x; }), "({ int x; sizeof x; })");
  assert( 8, ({ int *x; sizeof(x); }), "({ int *x; sizeof(x); })");
  assert( 8, ({ int x; sizeof(&x); }), "({ int x; sizeof(&x); })");
  assert( 4, ({ int x = 3; sizeof(x = x - 2); }), "({ int x = 3; sizeof(x = x - 2); })");
  assert( 3, ({ int x = 3; sizeof(x = x - 2); x; }), "({ int x = 3; sizeof(x = x - 2); x; })");

  assert( 9, ({ inc(8); }), "({ inc(8); })");

  assert( 3, ({ int x = 7, y = 5; *(&x + 1) = 3; y; }), "({ int x = 7, y = 5; *(&x + 1) = 3; y; })");
  assert( 3, ({ int x = 7, y = 5; *(1 + &x) = 3; y; }), "({ int x = 7, y = 5; *(1 + &x) = 3; y; })");
  assert( 2, ({ int x, y, z; &z - &x; }), "({ int x, y, z; &z - &x; })");
  assert( 3, ({ int x = 7, y = 5; *(&y - 1) = 3; x; }), "({ int x = 7, y = 5; *(&y - 1) = 3; x; })");

  assert( 6, ({ int x = 3, *y = &x; x + *y; }), "({ int x = 3, *y = &x; x + *y; })");
  assert( 2, ({ int x = 3, y = x - 2; x - y; }), "({ int x = 3, y = x - 2; x - y; })");
  assert( 28, ({ int x = 28, *y = &x, **z = &y; **z; }), "({ int x = 28, *y = &x, **z = &y; **z; })");

  assert( 5, ({ int x = 3, y = 2; x + y; }), "({ int x = 3, y = 2; x + y; })");
  assert( 2, ({ int x = 3, y = x - 2; x - y; }), "({ int x = 3, y = x - 2; x - y; })");

  assert( 3, ({ int x = 3; *&x; }), "({ int x = 3; *&x; })");
  assert( 2, ({ int x = 2, *y = &x; *y; }), "({ int x = 2, y = &x; *y; })");
  assert( 7, ({ int x = 9, y = 7; *(&x + 1); }), "({ int x = 9, y = 7; *(&x + 1); })");
  assert( 9, ({ int x = 9, y = 7; *(&y - 1); }), "({ int x = 9, y = 7; *(&y - 1); })");
  assert( 4, ({ int x = 9, *y = &x; *y = 4; x; }), "({ int x = 9, y = &x; *y = 4; x; })");
  assert( 7, ({ int x = 7, y = 5; *(&x + 1) = 3; x; }), "({ int x = 7, y = 5; *(&x + 1) = 3; x; })");
  assert( 3, ({ int x = 7, y = 5; *(&x + 1) = 3; y; }), "({ int x = 7, y = 5; *(&x + 1) = 3; y; })");
  assert( 4, ({ int x = 7, y = 5; *(&y - 1) = 4; x; }), "({ int x = 7, y = 5; *(&y - 1) = 4; x; })");
  assert( 5, ({ int x = 7, y = 5; *(&y - 1) = 4; y; }), "({ int x = 7, y = 5; *(&y - 1) = 4; y; })");

  assert( 89, fibo(10), "fibo(11)");
  assert( 7, sub2(10, 3), "sub2(10, 3)");
  assert( 3, echo_self(3), "echo_self(3)");
  assert( 42, sum3(7, 12, 23), "sum3(7, 12, 23)");
  assert( 7, ({ int foo = 3; int bar = 2; int baz = sum3(foo, -bar, foo * bar); baz; }), "({ int foo = 3; int bar = 2; int baz = sum3(foo, -bar, foo * bar); baz; })");

  assert( 42, ({ int foo=42; { int bar=foo*2; bar=bar+1;} foo; }), "({ int foo=42; { int bar=foo*2; bar=bar+1;} foo; })");
  assert( 0, ({ int x; if (1!=1) { int foo=1; x=foo; } else { int foo=0; x=foo; } x; }), "({ int x; if (1!=1) { int foo=1; x=foo; } else { int foo=0; x=foo; } x; })");
  assert( 1, ({ int x; if (1==1) { int foo=1; x=foo; } else { int foo=0; x=foo; } x; }), "{( int x; if (1==1) { int foo=1; x=foo; } else { int foo=0; x=foo; } x; })");

  assert( 16, ({ int foo=1; int i; for (i=0; i<4; i=i+1) foo=foo*2; foo; }), "({ int foo=1; int i; for (i=0; i<4; i=i+1) foo=foo*2; foo; })");
  assert( 10, ({ int i=0; for (; i<10; i=i+1) 1==1; i; }), "({ int i=0; for (; i<10; i=i+1) 1==1; i; })");
  assert( 42, ({ int i; for (i=0; i<42;) i=i+1; i; }), "({ int i; for (i=0; i<42;) i=i+1; i; })");

  assert( 42, ({ int i=0; for (; i<42;) i=i+1; i; }), "({ int i=0; for (; i<42;) i=i+1; i; })");
  assert( 1, ({ int foo=100; while (foo!=1) foo=foo-1; foo; }), "({ int foo=100; while (foo!=1) foo=foo-1; foo; })");
  assert( 42, ({ int foo=42; int bar=0; while ((foo=foo -1)>=0) bar=bar+1; bar; }), "({ int foo=42; int bar=0; while ((foo=foo -1)>=0) bar=bar+1; bar; })");
  assert( 1, ({ int x, foo=1; if (foo==1) x=1; else x=0; x; }), "({ int x, foo=1; if (foo==1) x=foo; else x=0; x; })");
  assert( 0, ({ int x, foo=2; if (foo==1) x=0; else x=0; x; }), "({ int x, foo=2; if (foo==1) x=foo; else x=0; x; })");

  assert( 1, ({ int a=1; a; }), "({ int a=1; a; })");
  assert( 2, ({ int z; int a=z=2; z; }), "({ int z; int a=z=2; z; })");
  assert( 2, ({ int z; int a=z=2; a; }), "({ int z; int a=z=2; a; })");
  assert( 1, ({ int a=3; int b=2; int c=a-b; c; }), "({ int a=3; int b=2; int c=a-b; c; })");
  assert( 6, ({ int a=3; int b=2; int c=4; int z=a*4/(3*b-c); z; }), "({ int a=3; int b=2; int c=4; int z=a*4/(3*b-c); z; })");

  assert( 0, 1!=2*1-1, "1!=2*1-1");
  assert( 1, 7==-5*(-2)+10/(-2)-(-2), "7==-5*(-2)+10/(-2)-(-2)");

  assert( 0, 0>1, "0>1");
  assert( 0, 1>1, "1>1");
  assert( 1, 1>0, "1>0");

  assert( 0, 0>=1, "0>=1");
  assert( 1, 1>=1, "1>=1");
  assert( 1, 1>=0, "1>=0");

  assert( 0, 1<0, "1<0");
  assert( 0, 1<1, "1<1");
  assert( 1, 0<1, "0<1");

  assert( 0, 1<=0, "1<=0");
  assert( 1, 1<=1, "1<=1");
  assert( 1, 0<=1, "0<=1");

  assert( 0, 0==1, "0==1");
  assert( 1, 42==42, "42==42");
  assert( 0, 42!=42, "42!=42");
  assert( 1, 0!=1, "0!=1");

  assert( 0, 0, "0");
  assert( 42, 42, "42");
  assert( 21, 5+20-4, "5+20-4");
  assert( 41, 12+ 34-5 , "12+ 34-5 ");
  assert( 7, 3*12- 34+5 , "3*12- 34+5 ");
  assert( 4, 6*(3+9)- 12*10/15-5*(2+10), "6*(3+9)- 12*10/15-5*(2+10)");
  assert( 7, -5*(-2)+10/(-2)-(-2), "-5*(-2)+10/(-2)-(-2)");

  printf("OK\n");
  return 0;
}

