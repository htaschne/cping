#include "line_reader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    size_t calls;
    size_t truncated;
    char joined[128];
} Capture;

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_line_reader: %s\n", message);
        exit(1);
    }
}

static void capture_line(const char *line, size_t len, int truncated, void *context) {
    Capture *capture = (Capture *)context;
    size_t used = strlen(capture->joined);

    capture->calls++;
    if (truncated) {
        capture->truncated++;
    }
    if (!truncated && used + len + 2U < sizeof(capture->joined)) {
        memcpy(capture->joined + used, line, len);
        capture->joined[used + len] = '|';
        capture->joined[used + len + 1U] = '\0';
    }
}

int main(void) {
    LineReader reader;
    Capture capture;
    unsigned char long_data[CPING_MAX_LINE_LEN + 2U];
    size_t i;

    memset(&capture, 0, sizeof(capture));
    line_reader_init(&reader);
    {
        const unsigned char data[] = "one\ntwo\nthree";
        line_reader_feed(&reader, data, sizeof(data) - 1U, capture_line, &capture);
        line_reader_finish(&reader, capture_line, &capture);
    }
    check(capture.calls == 3U, "multiple and final lines");
    check(strcmp(capture.joined, "one|two|three|") == 0, "line contents");

    memset(&capture, 0, sizeof(capture));
    line_reader_init(&reader);
    {
        const unsigned char data[] = "abc\n";
        for (i = 0; i < sizeof(data) - 1U; i++) {
            line_reader_feed(&reader, data + i, 1U, capture_line, &capture);
        }
    }
    check(capture.calls == 1U, "one byte at a time");
    check(strcmp(capture.joined, "abc|") == 0, "one byte content");

    memset(long_data, 'x', sizeof(long_data));
    long_data[sizeof(long_data) - 1U] = '\n';
    memset(&capture, 0, sizeof(capture));
    line_reader_init(&reader);
    line_reader_feed(&reader, long_data, sizeof(long_data), capture_line, &capture);
    check(capture.calls == 1U, "overlong line emits one diagnostic callback");
    check(capture.truncated == 1U, "overlong line truncated");

    puts("test_line_reader: ok");
    return 0;
}
