#include "stats.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void check(int condition, const char *message) {
    if (!condition) {
        fprintf(stderr, "test_stats: %s\n", message);
        exit(1);
    }
}

static void check_close(double actual, double expected, const char *message) {
    if (fabs(actual - expected) > 0.0001) {
        fprintf(stderr, "test_stats: %s: got %.6f expected %.6f\n", message, actual, expected);
        exit(1);
    }
}

int main(void) {
    Stats stats;
    stats_init(&stats);

    check(stats.count == 0, "initial count");
    check_close(stats_stddev(&stats), 0.0, "empty stddev");

    stats_add(&stats, 10.0);
    check(stats.count == 1, "single count");
    check_close(stats.mean, 10.0, "single mean");
    check_close(stats_stddev(&stats), 0.0, "single stddev");

    stats_add(&stats, 20.0);
    stats_add(&stats, 30.0);
    check(stats.count == 3, "three count");
    check_close(stats.mean, 20.0, "mean");
    check_close(stats.min, 10.0, "min");
    check_close(stats.max, 30.0, "max");
    check_close(stats_stddev(&stats), 10.0, "sample stddev");

    puts("test_stats: ok");
    return 0;
}
