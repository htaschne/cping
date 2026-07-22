#include "parser.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_parser: %s\n", message);
        exit(1);
    }
}

static void check_close(double actual, double expected, const char *message) {
    if (fabs(actual - expected) > 0.0001) {
        fprintf(stderr, "test_parser: %s: got %.6f expected %.6f\n", message, actual, expected);
        exit(1);
    }
}

int main(void) {
    PingReply reply;

    check(parse_ping_reply("64 bytes from 172.217.162.174: icmp_seq=0 ttl=111 time=36.518 ms", &reply), "macOS line parses");
    check(reply.has_sequence && reply.sequence == 0, "macOS sequence parses");
    check_close(reply.latency_ms, 36.518, "macOS latency parses");

    check(parse_ping_reply("64 bytes from 142.250.72.14: icmp_seq=1 ttl=115 time=12.7 ms", &reply), "Linux line parses");
    check(reply.has_sequence && reply.sequence == 1, "Linux sequence parses");
    check_close(reply.latency_ms, 12.7, "Linux latency parses");

    check(parse_ping_reply("64 bytes from host: icmp_seq=4 ttl=64 time<1 ms", &reply), "less-than latency parses");
    check_close(reply.latency_ms, 0.5, "less-than latency uses midpoint");

    check(!parse_ping_reply("Request timeout for icmp_seq 3", &reply), "timeout is not a sample");
    check(reply.has_sequence && reply.sequence == 3UL, "timeout sequence parses");

    check(!parse_ping_reply("", &reply), "empty line rejected");
    check(!parse_ping_reply("64 bytes from host time=", &reply), "empty time rejected");
    check(!parse_ping_reply("64 bytes from host time=.", &reply), "dot time rejected");
    check(!parse_ping_reply("64 bytes from host time=1.2.3 ms", &reply), "multi-dot time rejected");
    check(!parse_ping_reply("64 bytes from host time=-1 ms", &reply), "negative time rejected");
    check(!parse_ping_reply("64 bytes from host time=nan ms", &reply), "nan rejected");
    check(!parse_ping_reply("64 bytes from host time=inf ms", &reply), "inf rejected");
    check(!parse_ping_reply("64 bytes from host time=1e9999 ms", &reply), "overflowing exponent rejected");
    check(!parse_ping_reply("64 bytes from host time=999999999 ms", &reply), "huge rtt rejected");
    check(!parse_ping_reply("64 bytes from host uptime=1 time=bad ms", &reply), "bad time rejected");

    {
        const char nul_line[] = {'6', '4', ' ', 't', 'i', 'm', 'e', '=', '1', '\0', '2'};
        check(!parse_ping_reply_len(nul_line, sizeof(nul_line), &reply), "embedded nul rejected");
    }
    check(parse_ping_reply("64 bytes from host: icmp_seq=4294967295 ttl=64 time=1 ms", &reply), "large sequence parses");
    check(reply.has_sequence && reply.sequence == 4294967295UL, "large sequence value");

    puts("test_parser: ok");
    return 0;
}
