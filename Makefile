SHELL = /bin/bash

CC          = gcc
CFLAGS      = -std=c11 -static -fno-common

# for release build
SRCS        = $(wildcard *.c)
OBJS        = $(SRCS:.c=.o)
TARGET      = alloycc
HEADER      = $(TARGET).h

# for stg1 build
BUILDDIR    = tmp
BUILDCFLAGS = -g -o0 -DDEBUG
STG1TARGET  = alloycc-stage1
STG2TARGET  = alloycc-stage2
STG3TARGET  = alloycc-stage3

TSTDIR      = test
TSTSOURCE   = test.c

all: prep release

#release rules
release: stg3
	cp $(STG3TARGET) $(TARGET)

# << stg1 rules >>
stg1: $(STG1TARGET)

$(STG1TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OJBS): %.c $(HEADER)
	$(CC) -c $(CFLAGS) $(BUILDCFLAGS) -o $@ $<

# for testing (w/ stg1)
test: $(STG1TARGET) $(TSTDIR)/$(TSTSOURCE) $(TSTDIR)/extern.o
	(cd $(TSTDIR); ../$(STG1TARGET) -I. $(TSTSOURCE)) > $(TSTDIR)/tmp.s
	$(CC) -static -g -o $(TSTDIR)/tmp $(TSTDIR)/tmp.s $(TSTDIR)/extern.o
	$(TSTDIR)/tmp

# << stg2 rules >>
stg2: $(STG2TARGET)

$(STG2TARGET): $(STG1TARGET) $(SRCS) $(HEADER) self.sh
	./self.sh ./$(STG1TARGET) $(BUILDDIR) $(STG2TARGET)

# for testing (w/ stg2)
test-stg2: $(STG2TARGET) $(TSTDIR)/$(TSTSOURCE) $(TSTDIR)/extern.o
	(cd $(TSTDIR); ../$(STG2TARGET) -I. $(TSTSOURCE)) > $(TSTDIR)/tmp.s
	$(CC) -static -g -o $(TSTDIR)/tmp $(TSTDIR)/tmp.s $(TSTDIR)/extern.o
	$(TSTDIR)/tmp

# << stg3 rules >>
stg3: $(STG3TARGET)

$(STG3TARGET): $(STG2TARGET) $(SRCS) $(HEADER) self.sh
	./self.sh ./$(STG2TARGET) $(BUILDDIR) $(STG3TARGET)

# for testing (w/ stg3)
test-stg3: $(STG3TARGET)
	diff $(STG2TARGET) $(STG3TARGET) && echo 'OK'

test-all: test test-stg2 test-stg3

# for debugging (use it in macOS, or run `sudo apt get xxd`)
hexdiff: $(STG2TARGET) $(STG3TARGET)
	diff <(xxd $(STG2TARGET)) <(xxd $(STG3TARGET)) && echo 'No difference found.'

clean:
	rm -f $(TARGET) *~ $(BUILDDIR)/* $(STG1TARGET) $(STG2TARGET) $(STG3TARGET) $(TSTDIR)/*.o $(TSTDIR)/tmp*

prep:
	mkdir -p $(BUILDDIR)
	mkdir -p $(TSTDIR)

.PHONY: release stg1 stg2 stg3 prep hexdiff test test-stg2 test-stg3 clean
