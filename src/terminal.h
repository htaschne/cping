#ifndef CPING_TERMINAL_H
#define CPING_TERMINAL_H

#include <stddef.h>

typedef struct {
    int is_tty;
    int width;
    int use_color;
    int use_unicode;
} Terminal;

void terminal_init(Terminal *term, int force_ascii, int no_color);
void terminal_refresh_size(Terminal *term);
void terminal_hide_cursor(const Terminal *term);
void terminal_show_cursor(const Terminal *term);
void terminal_clear_line(const Terminal *term);
void terminal_finish_line(const Terminal *term);
void terminal_make_bar(char *buf, size_t buflen, int width, double value, double ceiling, int unicode);
const char *terminal_color_for_latency(double value, double ceiling);

#endif
