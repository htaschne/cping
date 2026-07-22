#ifndef CPING_PARSER_H
#define CPING_PARSER_H

typedef struct {
    int has_latency;
    double latency_ms;
    int has_sequence;
    long sequence;
} PingReply;

int parse_ping_reply(const char *line, PingReply *reply);

#endif
