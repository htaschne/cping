#include "terminal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_terminal: %s\n", message);
        exit(1);
    }
}

int main(void) {
    char buf[16];

    terminal_make_bar(buf, sizeof(buf), 4, 2.0, 4.0, 0);
    check(strcmp(buf, "[##--]") == 0, "ascii bar");

    terminal_make_bar(buf, sizeof(buf), 100, 50.0, 100.0, 1);
    check(memchr(buf, '\0', sizeof(buf)) != NULL, "unicode bar remains terminated");
    check(strchr(buf, '[') == buf, "unicode bar starts cleanly");
    check(strchr(buf, ']') != NULL, "unicode bar closes when space permits");

    terminal_make_bar(buf, sizeof(buf), 4, HUGE_VAL, 4.0, 0);
    check(strcmp(buf, "[----]") == 0, "non-finite value clamps to zero");

    puts("test_terminal: ok");
    return 0;
}
