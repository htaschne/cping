CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -pedantic -O2
CPPFLAGS ?=
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
LDFLAGS ?=
LDLIBS ?= -lm
HARDENING_CFLAGS ?= -fstack-protector-strong -fPIE
STRICT_WARNINGS = -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wformat=2 -Wundef -Wcast-align -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes -Wwrite-strings -Wnull-dereference -Wdouble-promotion -Wvla -Werror=implicit-function-declaration
UBSAN_FLAGS = -O1 -g3 -fno-omit-frame-pointer -fsanitize=undefined

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
HARDENING_LDFLAGS ?= -pie -Wl,-z,relro,-z,now
SANITIZE_FLAGS ?= -O1 -g3 -fno-omit-frame-pointer -fsanitize=address,undefined
else ifeq ($(UNAME_S),Darwin)
HARDENING_LDFLAGS ?=
SANITIZE_FLAGS ?= $(UBSAN_FLAGS)
else
HARDENING_LDFLAGS ?=
SANITIZE_FLAGS ?= $(UBSAN_FLAGS)
endif

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

SRC = src/main.c src/ping_process.c src/parser.c src/stats.c src/terminal.c src/line_reader.c
OBJ = $(SRC:.c=.o)
TEST_HOOK_CPPFLAGS = -DCPING_ENABLE_TEST_HOOKS

.PHONY: all clean test strict sanitize sanitize-undefined analyze fuzz fuzz-smoke install uninstall demo

all: cping

cping: $(OBJ)
	$(CC) $(LDFLAGS) $(HARDENING_LDFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) -c -o $@ $<

test: tests/test_parser tests/test_stats tests/test_line_reader tests/test_terminal tests/cping-integration
	./tests/test_parser
	./tests/test_stats
	./tests/test_line_reader
	./tests/test_terminal
	sh ./tests/test_integration.sh ./tests/cping-integration

tests/test_parser: tests/test_parser.c src/parser.c src/parser.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/test_parser.c src/parser.c

tests/test_stats: tests/test_stats.c src/stats.c src/stats.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/test_stats.c src/stats.c $(LDLIBS)

tests/test_line_reader: tests/test_line_reader.c src/line_reader.c src/line_reader.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/test_line_reader.c src/line_reader.c

tests/test_terminal: tests/test_terminal.c src/terminal.c src/terminal.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/test_terminal.c src/terminal.c $(LDLIBS)

tests/cping-integration: $(SRC)
	$(CC) $(CPPFLAGS) $(TEST_HOOK_CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ $(SRC) $(LDLIBS)

strict:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 $(STRICT_WARNINGS) -O2" test

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -Wall -Wextra -Wpedantic $(SANITIZE_FLAGS)" LDFLAGS="$(SANITIZE_FLAGS)" test

sanitize-undefined:
	$(MAKE) clean
	$(MAKE) CFLAGS="-std=c11 -Wall -Wextra -Wpedantic $(UBSAN_FLAGS)" LDFLAGS="$(UBSAN_FLAGS)" test

analyze:
	@if command -v clang >/dev/null 2>&1; then \
		clang --analyze $(CPPFLAGS) -Isrc $(SRC); \
	else \
		echo "clang is not available; skipping clang static analyzer"; \
	fi
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --enable=warning,style,performance,portability --std=c11 --error-exitcode=1 -Isrc src tests; \
	else \
		echo "cppcheck is not available; skipping cppcheck"; \
	fi
	@if command -v clang-tidy >/dev/null 2>&1; then \
		clang-tidy $(SRC) -- $(CPPFLAGS) -Isrc -std=c11; \
	else \
		echo "clang-tidy is not available; skipping clang-tidy"; \
	fi

tests/fuzz_parser: tests/fuzz_parser.c src/parser.c src/line_reader.c src/stats.c src/parser.h src/line_reader.h src/stats.h
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HARDENING_CFLAGS) $(HARDENING_LDFLAGS) -Isrc -o $@ tests/fuzz_parser.c src/parser.c src/line_reader.c src/stats.c $(LDLIBS)

fuzz: tests/fuzz_parser
	@echo "Run: ./tests/fuzz_parser < input-corpus-file"

fuzz-smoke: tests/fuzz_parser
	@for f in tests/corpus/*; do ./tests/fuzz_parser < "$$f"; done
	@printf 'abc\000time=1\n' | ./tests/fuzz_parser

install: cping
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 cping $(DESTDIR)$(BINDIR)/cping

demo:
	scripts/record-demo.sh

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/cping

clean:
	rm -f cping $(OBJ) tests/test_parser tests/test_stats tests/test_line_reader tests/test_terminal tests/cping-integration tests/fuzz_parser *.plist
	rm -rf tests/*.dSYM
