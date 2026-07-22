#ifndef CPING_STATS_H
#define CPING_STATS_H

#include <stddef.h>

typedef struct {
    size_t count;
    double mean;
    double m2;
    double min;
    double max;
} Stats;

void stats_init(Stats *stats);
int stats_add(Stats *stats, double value);
double stats_stddev(const Stats *stats);

#endif
