CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
LDFLAGS ?=
LDLIBS ?= -lm
HARDENING_CFLAGS ?= -fstack-protector-strong -fPIE

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
HARDENING_LDFLAGS ?= -pie -Wl,-z,relro,-z,now
else ifeq ($(UNAME_S),Darwin)
HARDENING_LDFLAGS ?=
else
HARDENING_LDFLAGS ?=
endif

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRC = src/main.c src/ping_process.c src/parser.c src/stats.c src/terminal.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean test install uninstall demo

all: cping

cping: $(OBJ)
	$(CC) $(LDFLAGS) $(HARDENING_LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) -c -o $@ $<

test: tests/test_parser tests/test_stats
	./tests/test_parser
	./tests/test_stats

tests/test_parser: tests/test_parser.c src/parser.c src/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/test_parser.c src/parser.c

tests/test_stats: tests/test_stats.c src/stats.c src/stats.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/test_stats.c src/stats.c $(LDLIBS)

install: cping
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 cping $(DESTDIR)$(BINDIR)/cping

demo:
	scripts/record-demo.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/cping

clean:
	rm -f cping $(OBJ) tests/test_parser tests/test_stats
