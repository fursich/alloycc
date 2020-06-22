#!/bin/bash

docker run --rm -v $PWD:/home/user/misc -w /home/user/misc compilerbook ./test.sh $1
echo $?
