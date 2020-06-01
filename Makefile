CC         = gcc
CFLAGS     = -std=c11 -static

# for release build
SRCS       = $(wildcard *.c)
OBJS       = $(SRCS:.c=.o)
TARGET     = 9cc
HEADER     = $(TARGET).h
RELCFLAGS  = -g -o3 -DNDEBUG

# for debug build
DBGDIR     = debug
DBGOBJS    = $(addprefix $(DBGDIR)/, $(OBJS))
DBGTARGET  = $(DBGDIR)/$(TARGET)
DBGCFLAGS  = -g -o0 -DDEBUG

TSTDIR     = test
TSTTARGET  = $(TSTDIR)/tmp
TSTSOURCE  = $(TSTDIR)/test.c
TSTLDFLAGS = -static

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
test: $(TSTTARGET)
	$(TSTTARGET)

$(TSTTARGET): $(TSTSOURCE) $(DBGTARGET)
	$(DBGTARGET) $(TSTSOURCE) > $(TSTTARGET).s
	echo 'int static_fn() { return 5; }' | $(CC) -xc -c -o $(TSTDIR)/tmp2.o -
	$(CC) $(TSTLDFLAGS) -o $(TSTTARGET) $(TSTTARGET).s $(TSTDIR)/tmp2.o


clean:
	rm -f $(TARGET) *.o *~ tmp* $(DBGTARGET) $(DBGDIR)/*.o $(DBGDIR)/tmp* $(TSTTARGET) $(TSTDIR)/*.o $(TSTDIR)/tmp*

prep:
	mkdir -p $(DBGDIR)
	mkdir -p $(TSTDIR)

.PHONY: test clean release debug prep
