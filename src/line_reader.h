#ifndef CPING_LINE_READER_H
#define CPING_LINE_READER_H

#include <stddef.h>

#define CPING_MAX_LINE_LEN 4096U

typedef void (*LineReaderCallback)(const char *line, size_t len, int truncated, void *context);

typedef struct {
    char buf[CPING_MAX_LINE_LEN + 1U];
    size_t len;
    int discarding;
} LineReader;

void line_reader_init(LineReader *reader);
void line_reader_feed(LineReader *reader, const unsigned char *data, size_t len, LineReaderCallback callback, void *context);
void line_reader_finish(LineReader *reader, LineReaderCallback callback, void *context);

#endif
