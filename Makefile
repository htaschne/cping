CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -pedantic -O2
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?= -lm

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRC = src/main.c src/ping_process.c src/parser.c src/stats.c src/terminal.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean test install uninstall demo

all: cping

cping: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: tests/test_parser tests/test_stats
	./tests/test_parser
	./tests/test_stats

tests/test_parser: tests/test_parser.c src/parser.c src/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ tests/test_parser.c src/parser.c

tests/test_stats: tests/test_stats.c src/stats.c src/stats.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -o $@ tests/test_stats.c src/stats.c $(LDLIBS)

install: cping
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 cping $(DESTDIR)$(BINDIR)/cping

demo:
	scripts/record-demo.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/cping

clean:
	rm -f cping $(OBJ) tests/test_parser tests/test_stats
