#ifndef CPING_PARSER_H
#define CPING_PARSER_H

#include <stddef.h>

typedef struct {
    int has_latency;
    double latency_ms;
    int has_sequence;
    unsigned long sequence;
} PingReply;

int parse_ping_reply(const char *line, PingReply *reply);
int parse_ping_reply_len(const char *line, size_t len, PingReply *reply);

#endif
