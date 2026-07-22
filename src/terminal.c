#include "terminal.h"

#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int terminal_supports_unicode(void) {
    const char *locale = setlocale(LC_CTYPE, "");
    if (!locale) {
        locale = getenv("LC_ALL");
    }
    if (!locale) {
        locale = getenv("LC_CTYPE");
    }
    if (!locale) {
        locale = getenv("LANG");
    }
    if (!locale) {
        return 0;
    }
    return strstr(locale, "UTF-8") || strstr(locale, "utf8") || strstr(locale, "UTF8");
}

void terminal_refresh_size(Terminal *term) {
    struct winsize ws;
    term->width = 80;
    if (term->is_tty && ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        term->width = ws.ws_col;
    }
}

void terminal_init(Terminal *term, int force_ascii, int no_color) {
    const char *term_name = getenv("TERM");
    int dumb = term_name && strcmp(term_name, "dumb") == 0;

    term->is_tty = isatty(STDOUT_FILENO);
    terminal_refresh_size(term);
    term->use_unicode = term->is_tty && !force_ascii && !dumb && terminal_supports_unicode();
    term->use_color = term->is_tty && !no_color && !dumb && getenv("NO_COLOR") == NULL;
}

void terminal_hide_cursor(const Terminal *term) {
    if (term->is_tty) {
        fputs("\033[?25l", stdout);
        fflush(stdout);
    }
}

void terminal_show_cursor(const Terminal *term) {
    if (term->is_tty) {
        fputs("\033[?25h", stdout);
        fflush(stdout);
    }
}

void terminal_clear_line(const Terminal *term) {
    if (term->is_tty) {
        fputs("\r\033[2K", stdout);
    }
}

void terminal_finish_line(const Terminal *term) {
    if (term->is_tty) {
        fputs("\r\033[2K\n", stdout);
    } else {
        fputc('\n', stdout);
    }
    fflush(stdout);
}

void terminal_make_bar(char *buf, size_t buflen, int width, double value, double ceiling, int unicode) {
    int filled;
    int i;
    size_t used = 0;
    const char *fill = unicode ? "█" : "#";
    const char *empty = unicode ? "░" : "-";
    size_t fill_len = strlen(fill);
    size_t empty_len = strlen(empty);

    if (width < 1 || buflen == 0) {
        if (buflen > 0) {
            buf[0] = '\0';
        }
        return;
    }

    if (value < 0.0) {
        value = 0.0;
    }
    if (ceiling <= 0.0) {
        ceiling = 1.0;
    }

    filled = (int)((value / ceiling) * (double)width + 0.5);
    if (filled > width) {
        filled = width;
    }

    if (used + 1 < buflen) {
        buf[used++] = '[';
    }
    for (i = 0; i < width && used + fill_len + 1 < buflen; i++) {
        const char *part = i < filled ? fill : empty;
        size_t part_len = i < filled ? fill_len : empty_len;
        memcpy(buf + used, part, part_len);
        used += part_len;
    }
    if (used + 1 < buflen) {
        buf[used++] = ']';
    }
    buf[used < buflen ? used : buflen - 1] = '\0';
}

const char *terminal_color_for_latency(double value, double ceiling) {
    double ratio = ceiling > 0.0 ? value / ceiling : 1.0;
    if (ratio < 0.4) {
        return "\033[32m";
    }
    if (ratio < 0.75) {
        return "\033[33m";
    }
    return "\033[31m";
}
