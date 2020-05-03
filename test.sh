#!/bin/bash

assert() {
  expected="$1"
  input="$2"

  ./9cc "$input" > tmp.s
  cc -o tmp tmp.s

  ./tmp
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected but got $actual"
    exit 1
  fi
}

assert 0  'return 0;'
assert 42 'return 42;'
assert 21 'return 5+20-4;'
assert 41 'return 12 +  34 - 5 ;'
assert 7  'return 3 * 12 -  34 + 5 ;'
assert 4  'return 6 * (3 + 9) -  12 * 10 / 15 - 5 * (2 + 10);'
assert 7  'return -5 * (-2) + 10 / (-2) - (-2);'

assert 0  'return 0 == 1;'
assert 1  'return 42 == 42;'
assert 0  'return 42 != 42;'
assert 1  'return 0 != 1;'

assert 0  'return 1 < 0;'
assert 0  'return 1 < 1;'
assert 1  'return 0 < 1;'
assert 0  'return 1 <= 0;'
assert 1  'return 1 <= 1;'
assert 1  'return 0 <= 1;'

assert 0  'return 0 > 1;'
assert 0  'return 1 > 1;'
assert 1  'return 1 > 0;'
assert 0  'return 0 >= 1;'
assert 1  'return 1 >= 1;'
assert 1  'return 1 >= 0;'

assert 0  'return 1 != 2 * 1 - 1;'
assert 1  'return 7 == -5 * (-2) + 10 / (-2) - (-2);'

assert 1  'return 1; 2; 3;'
assert 2  '1; return 2; 3;'
assert 3  '1; 2; return 3;'
assert 1  'return 1; return 2; return 3;'
echo OK
