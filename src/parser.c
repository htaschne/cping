#include "parser.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CPING_MAX_RTT_MS 86400000.0

static int byte_is_space(unsigned char ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\v' || ch == '\f';
}

static int byte_is_delim(unsigned char ch) {
    return ch == '\0' || byte_is_space(ch) || ch == ',';
}

static const char *find_token(const char *line, size_t len, const char *token) {
    size_t token_len = strlen(token);
    size_t i;

    if (token_len == 0 || len < token_len) {
        return NULL;
    }

    for (i = 0; i <= len - token_len; i++) {
        if (memcmp(line + i, token, token_len) == 0) {
            return line + i;
        }
    }
    return NULL;
}

static const char *find_field_token(const char *line, size_t len, const char *token) {
    const char *p = line;

    while ((p = find_token(p, (size_t)((line + len) - p), token)) != NULL) {
        if (p == line || !isalnum((unsigned char)p[-1])) {
            return p;
        }
        p++;
    }
    return NULL;
}

static int copy_token(const char *start, const char *end, char *buf, size_t buflen) {
    size_t len;

    while (start < end && byte_is_space((unsigned char)*start)) {
        start++;
    }
    len = (size_t)(end - start);
    if (len == 0 || len >= buflen) {
        return 0;
    }
    memcpy(buf, start, len);
    buf[len] = '\0';
    return 1;
}

static int parse_unsigned_token(const char *start, const char *line_end, unsigned long *value) {
    char token[32];
    char *end = NULL;
    unsigned long parsed;

    while (start < line_end && byte_is_space((unsigned char)*start)) {
        start++;
    }
    if (start == line_end || !isdigit((unsigned char)*start)) {
        return 0;
    }

    const char *p = start;
    while (p < line_end && isdigit((unsigned char)*p)) {
        p++;
    }
    if (p < line_end && !byte_is_delim((unsigned char)*p)) {
        return 0;
    }
    if (!copy_token(start, p, token, sizeof(token))) {
        return 0;
    }

    errno = 0;
    parsed = strtoul(token, &end, 10);
    if (errno == ERANGE || end == token || *end != '\0') {
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_latency_value(const char *start, const char *line_end, double *latency_ms) {
    char token[64];
    char *end = NULL;
    double parsed;
    const char *p = start;
    int saw_digit = 0;

    while (p < line_end && byte_is_space((unsigned char)*p)) {
        p++;
    }
    if (p == line_end) {
        return 0;
    }

    while (p < line_end && !byte_is_delim((unsigned char)*p)) {
        if (isdigit((unsigned char)*p)) {
            saw_digit = 1;
        }
        p++;
    }
    if (!saw_digit || !copy_token(start, p, token, sizeof(token))) {
        return 0;
    }

    errno = 0;
    parsed = strtod(token, &end);
    if (errno == ERANGE || end == token || *end != '\0') {
        return 0;
    }
    if (!isfinite(parsed) || parsed < 0.0 || parsed > CPING_MAX_RTT_MS) {
        return 0;
    }
    *latency_ms = parsed;
    return 1;
}

static int parse_latency(const char *line, size_t len, double *latency_ms) {
    const char *line_end = line + len;
    const char *time_equal = find_field_token(line, len, "time=");
    const char *time_less = find_field_token(line, len, "time<");

    if (time_equal != NULL && (time_less == NULL || time_equal < time_less)) {
        return parse_latency_value(time_equal + 5, line_end, latency_ms);
    }

    if (time_less != NULL) {
        const char *p = time_less + 5;
        while (p < line_end && byte_is_space((unsigned char)*p)) {
            p++;
        }
        if (p < line_end && isdigit((unsigned char)*p)) {
            *latency_ms = 0.5;
            return 1;
        }
    }

    return 0;
}

static int parse_sequence(const char *line, size_t len, unsigned long *sequence) {
    const char *fields[] = {"icmp_seq=", "icmp_seq ", "seq="};
    const char *line_end = line + len;
    size_t i;

    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        const char *p = find_token(line, len, fields[i]);
        if (p == NULL) {
            continue;
        }
        p += strlen(fields[i]);
        return parse_unsigned_token(p, line_end, sequence);
    }

    return 0;
}

int parse_ping_reply_len(const char *line, size_t len, PingReply *reply) {
    double latency = 0.0;
    unsigned long sequence = 0;

    reply->has_latency = 0;
    reply->latency_ms = 0.0;
    reply->has_sequence = 0;
    reply->sequence = 0;

    if (memchr(line, '\0', len) != NULL) {
        return 0;
    }

    if (parse_latency(line, len, &latency)) {
        reply->has_latency = 1;
        reply->latency_ms = latency;
    }

    if (parse_sequence(line, len, &sequence)) {
        reply->has_sequence = 1;
        reply->sequence = sequence;
    }

    return reply->has_latency;
}

int parse_ping_reply(const char *line, PingReply *reply) {
    return parse_ping_reply_len(line, strlen(line), reply);
}
