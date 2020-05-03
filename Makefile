CC        = gcc
CFLAGS    = -std=c11 -static

# for release build
SRCS      = $(wildcard *.c)
OBJS      = $(SRCS:.c=.o)
TARGET    = 9cc
HEADER    = $(TARGET).h
RELCFLAGS = -g -o3 -DNDEBUG

# for debug build
DBGDIR    = debug
DBGOBJS   = $(addprefix $(DBGDIR)/, $(OBJS))
DBGTARGET = $(DBGDIR)/$(TARGET)
DBGCFLAGS = -g -o0 -DDEBUG

all: prep release

# debug rules
debug: $(DBGTARGET)

$(DBGTARGET): $(DBGOBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(DBGDIR)/%.o: %.c
	$(CC) -c $(CFLAGS) $(DBGCFLAGS) -o $@ $<

$(DBGOBJS): $(HEADER)

#release rules
release: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

$(OBJS): $(HEADER)

# for testing (w/ debug)
test: $(DBGTARGET)
	./test.sh

clean:
	rm -f $(TARGET) *.o *~ tmp* $(DBGDIR)/tmp* $(DBGTARGET) $(DBGDIR)/*.o

prep:
	mkdir -p $(DBGDIR)

.PHONY: test clean release debug prep
