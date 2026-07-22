#include "stats.h"

#include <stddef.h>
#include <math.h>

void stats_init(Stats *stats) {
    stats->count = 0;
    stats->mean = 0.0;
    stats->m2 = 0.0;
    stats->min = 0.0;
    stats->max = 0.0;
}

int stats_add(Stats *stats, double value) {
    if (!isfinite(value) || stats->count == (size_t)-1) {
        return 0;
    }
    stats->count++;
    if (stats->count == 1) {
        stats->mean = value;
        stats->m2 = 0.0;
        stats->min = value;
        stats->max = value;
        return 1;
    }

    if (value < stats->min) {
        stats->min = value;
    }
    if (value > stats->max) {
        stats->max = value;
    }

    double delta = value - stats->mean;
    stats->mean += delta / (double)stats->count;
    double delta2 = value - stats->mean;
    stats->m2 += delta * delta2;
    if (!isfinite(stats->mean) || !isfinite(stats->m2)) {
        stats->count--;
        return 0;
    }
    return 1;
}

double stats_stddev(const Stats *stats) {
    if (stats->count < 2) {
        return 0.0;
    }
    return sqrt(stats->m2 / (double)(stats->count - 1));
}
