#include "line_reader.h"
#include "parser.h"
#include "stats.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    Stats stats;
} FuzzContext;

static void fuzz_line(const char *line, size_t len, int truncated, void *context) {
    FuzzContext *ctx = (FuzzContext *)context;
    PingReply reply;

    if (!truncated && parse_ping_reply_len(line, len, &reply)) {
        (void)stats_add(&ctx->stats, reply.latency_ms);
    }
}

static int run_buffer(const unsigned char *data, size_t len) {
    LineReader reader;
    FuzzContext context;

    stats_init(&context.stats);
    line_reader_init(&reader);
    line_reader_feed(&reader, data, len, fuzz_line, &context);
    line_reader_finish(&reader, fuzz_line, &context);
    return 0;
}

#if defined(__clang__) && defined(CPING_LIBFUZZER)
int LLVMFuzzerTestOneInput(const unsigned char *data, size_t size) {
    return run_buffer(data, size);
}
#else
int main(void) {
    unsigned char buf[4096];
    size_t nread;

    while ((nread = fread(buf, 1U, sizeof(buf), stdin)) > 0) {
        (void)run_buffer(buf, nread);
    }
    if (ferror(stdin)) {
        fprintf(stderr, "fuzz_parser: read failed\n");
        return 1;
    }
    return 0;
}
#endif
