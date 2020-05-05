#!/bin/bash

assert() {
  expected="$1"
  input="$2"

  ./debug/9cc "$input" > debug/tmp.s
  cc -o debug/tmp debug/tmp.s

  ./debug/tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected but got $actual"
    exit 1
  fi
}

assert 1  'foo = 1; while (foo == 1) return 1; return 0;'
assert 0  'foo = 2; while (foo == 1) return 1; return 0;'
assert 1  'foo = 100; while (foo != 1) foo = foo - 1; return foo;'
assert 42 'foo = 42; bar = 0; while ((foo = foo -1) >= 0) bar = bar + 1; return bar;'

assert 1  'foo = 1; if (foo == 1) return foo; return 0;'
assert 0  'foo = 2; if (foo == 1) return foo; return 0;'
assert 1  'foo = 1; if (foo == 1) return foo; else return 0;'
assert 0  'foo = 2; if (foo == 1) return foo; else return 0;'
assert 1  'foo = 1; bar = 3; if (foo == 1) return 1; else if (bar == 3) return 3; else 10;'
assert 3  'foo = 2; bar = 3; if (foo == 1) return 1; else if (bar == 3) return 3; return 10;'
assert 10 'foo = 2; bar = 7; if (foo == 1) return 1; else if (bar == 3) return 3; else return 10;'

assert 1  '_foo = 1; return _foo;'
assert 2  'foo = bar = 2; return foo;'
assert 3  'foo = bar = 3; return bar;'
assert 11 'foo = 3; bar0 = 2; bar1 = foo - bar0; return bar1 + 10;'
assert 6  'foo = 3; bar = 2; _baz = 4; quix = foo * 4 / (3 * bar - 2 * _baz); return -quix;'

assert 1  'a = 1; return a;'
assert 2  'a = z = 2; return z;'
assert 2  'a = z = 2; return a;'
assert 1  'a = 3; b = 2; c = a - b; return c;'
assert 6  'a = 3; b = 2; c = 4; z = a * 4 / (3 * b - c); return z;'

assert 1  'return 1; 2; 3;'
assert 2  '1; return 2; 3;'
assert 3  '1; 2; return 3;'
assert 1  'return 1; return 2; return 3;'

assert 0  'return 1 != 2 * 1 - 1;'
assert 1  'return 7 == -5 * (-2) + 10 / (-2) - (-2);'

assert 0  'return 0 > 1;'
assert 0  'return 1 > 1;'
assert 1  'return 1 > 0;'
assert 0  'return 0 >= 1;'
assert 1  'return 1 >= 1;'
assert 1  'return 1 >= 0;'

assert 0  'return 1 < 0;'
assert 0  'return 1 < 1;'
assert 1  'return 0 < 1;'
assert 0  'return 1 <= 0;'
assert 1  'return 1 <= 1;'
assert 1  'return 0 <= 1;'

assert 0  'return 0 == 1;'
assert 1  'return 42 == 42;'
assert 0  'return 42 != 42;'
assert 1  'return 0 != 1;'

assert 0  'return 0;'
assert 42 'return 42;'
assert 21 'return 5+20-4;'
assert 41 'return 12 +  34 - 5 ;'
assert 7  'return 3 * 12 -  34 + 5 ;'
assert 4  'return 6 * (3 + 9) -  12 * 10 / 15 - 5 * (2 + 10);'
assert 7  'return -5 * (-2) + 10 / (-2) - (-2);'

echo OK
