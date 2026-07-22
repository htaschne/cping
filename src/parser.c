#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int parse_unsigned_long(const char *start, long *value) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(start, &end, 10);
    if (errno != 0 || end == start || parsed < 0) {
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_latency(const char *line, double *latency_ms) {
    const char *time_field = strstr(line, "time=");
    int less_than_one = 0;
    if (!time_field) {
        time_field = strstr(line, "time<");
        less_than_one = 1;
    }
    if (!time_field) {
        return 0;
    }

    const char *p = time_field + 5;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }

    if (less_than_one) {
        *latency_ms = 0.5;
        return 1;
    }

    char *end = NULL;
    errno = 0;
    double parsed = strtod(p, &end);
    if (errno != 0 || end == p || parsed < 0.0) {
        return 0;
    }
    *latency_ms = parsed;
    return 1;
}

static int parse_sequence(const char *line, long *sequence) {
    const char *fields[] = {"icmp_seq=", "icmp_seq ", "seq="};
    size_t i;

    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        const char *p = strstr(line, fields[i]);
        if (!p) {
            continue;
        }
        p += strlen(fields[i]);
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        return parse_unsigned_long(p, sequence);
    }

    return 0;
}

int parse_ping_reply(const char *line, PingReply *reply) {
    double latency = 0.0;
    long sequence = 0;

    reply->has_latency = 0;
    reply->latency_ms = 0.0;
    reply->has_sequence = 0;
    reply->sequence = 0;

    if (parse_latency(line, &latency)) {
        reply->has_latency = 1;
        reply->latency_ms = latency;
    }

    if (parse_sequence(line, &sequence)) {
        reply->has_sequence = 1;
        reply->sequence = sequence;
    }

    return reply->has_latency;
}
