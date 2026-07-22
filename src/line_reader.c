#include "line_reader.h"

void line_reader_init(LineReader *reader) {
    reader->len = 0;
    reader->discarding = 0;
}

void line_reader_feed(LineReader *reader,
                      const unsigned char *data,
                      size_t len,
                      LineReaderCallback callback,
                      void *context) {
    size_t i;

    for (i = 0; i < len; i++) {
        unsigned char ch = data[i];
        if (ch == '\n') {
            if (reader->discarding) {
                callback(reader->buf, reader->len, 1, context);
            } else {
                reader->buf[reader->len] = '\0';
                callback(reader->buf, reader->len, 0, context);
            }
            reader->len = 0;
            reader->discarding = 0;
        } else if (!reader->discarding) {
            if (reader->len < CPING_MAX_LINE_LEN) {
                reader->buf[reader->len] = (char)ch;
                reader->len++;
            } else {
                reader->discarding = 1;
            }
        }
    }
}

void line_reader_finish(LineReader *reader, LineReaderCallback callback, void *context) {
    if (reader->len > 0 || reader->discarding) {
        reader->buf[reader->len] = '\0';
        callback(reader->buf, reader->len, reader->discarding, context);
        reader->len = 0;
        reader->discarding = 0;
    }
}
