# Alloycc

A self-hosting C compiler that operates on x86-64 linux.

## Setting up

For macOS:

1. clone this repository.

2. run `make` to build alloycc

(for macOS)
```bash
$ docker build -t compilerbook https://www.sigbus.info/compilerbook/Dockerfile
$ docker run --rm -it -v $PWD:/home/user compilerbook

:~$ make # rebuild alloycc on self-hosted basis
```

### Miscellaneous

* run `make test` to see alloycc passes all (simple but comprehensive) unit tests described in test/test.c
* `make test-all` will double-check this test with self-hosted compiler, as well as ensuring self-hosted binaries does not differ from first build to second.
* `make clean` will clean up binaries and tmp files.

## Acknowledgement
Original ideas and design of this compiler are introduced by Rui Ueyama in [compilerbook](https://www.sigbus.info/compilerbook), a fantastic in-depth tutorial document to implement C compiler in "incremental" approach.

You could also refer to some of his open repositories, such as [9cc](https://github.com/rui314/9cc) or [chibicc](https://github.com/rui314/chibicc) for actual implementations.
