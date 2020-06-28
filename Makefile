SHELL = /bin/bash

CC          = gcc
CFLAGS      = -std=c11 -static -fno-common

# for release build
SRCS        = $(wildcard *.c)
OBJS        = $(SRCS:.c=.o)
TARGET      = alloycc
HEADER      = $(TARGET).h

# for stg1 build
STG1DIR     = build-stg1
STG1OBJS    = $(addprefix $(STG1DIR)/, $(OBJS))
STG1TARGET  = $(STG1DIR)/$(TARGET)
BUILDCFLAGS = -g -o0 -DDEBUG

# for stg2 build
STG2DIR     = build-stg2
STG2OBJS    = $(addprefix $(STG2DIR)/, $(OBJS))
STG2TARGET  = $(STG2DIR)/$(TARGET)

# for stg3 build
STG3DIR     = build-stg3
STG3OBJS    = $(addprefix $(STG3DIR)/, $(OBJS))
STG3TARGET  = $(STG3DIR)/$(TARGET)

TSTDIR      = test
TSTSOURCE   = $(TSTDIR)/test.c

all: prep release

#release rules
release: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJS): $(HEADER)

# << stg1 rules >>
stg1: $(STG1TARGET)

$(STG1TARGET): $(STG1OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(STG1DIR)/%.o: %.c $(HEADER)
	$(CC) -c $(CFLAGS) $(BUILDCFLAGS) -o $@ $<

# for testing (w/ stg1)
test: $(STG1TARGET) $(TSTSOURCE) $(TSTDIR)/extern.o
	$(STG1TARGET) $(TSTSOURCE) > $(TSTDIR)/tmp.s
	$(CC) -static -g -o $(TSTDIR)/tmp $(TSTDIR)/tmp.s $(TSTDIR)/extern.o
	$(TSTDIR)/tmp

# << stg2 rules >>
stg2: $(STG2TARGET)

$(STG2TARGET): $(STG1TARGET) $(SRCS) $(HEADER) self.sh
	./self.sh $(STG1TARGET) $(STG2DIR) $(STG2TARGET)

# for testing (w/ stg2)
test-stg2: $(STG2TARGET) $(TSTSOURCE) $(TSTDIR)/extern.o
	$(STG2TARGET) $(TSTSOURCE) > $(TSTDIR)/tmp.s
	$(CC) -static -g -o $(TSTDIR)/tmp $(TSTDIR)/tmp.s $(TSTDIR)/extern.o
	$(TSTDIR)/tmp

# << stg3 rules >>
stg3: $(STG3TARGET)

$(STG3TARGET): $(STG2TARGET) $(SRCS) $(HEADER) self.sh
	./self.sh $(STG2TARGET) $(STG3DIR) $(STG3TARGET)

# for testing (w/ stg3)
test-stg3: $(STG3TARGET)
	diff $(STG2TARGET) $(STG3TARGET) && echo 'OK'

test-all: test test-stg2 test-stg3

# for debugging (use it in macOS, or run `sudo apt get xxd`)
hexdiff: $(STG2TARGET) $(STG3TARGET)
	diff <(xxd $(STG2TARGET)) <(xxd $(STG3TARGET)) && echo 'No difference found.'

clean:
	rm -f $(TARGET) *.o *~ tmp* $(STG1DIR)/* $(STG2DIR)/* $(TSTTARGET) $(TSTDIR)/*.o $(TSTDIR)/tmp*

prep:
	mkdir -p $(STG1DIR)
	mkdir -p $(STG2DIR)
	mkdir -p $(STG3DIR)
	mkdir -p $(TSTDIR)

.PHONY: test test-stg2 test-stg3 clean release stg1 stg2 prep hexdiff
