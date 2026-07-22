#ifndef CPING_PING_PROCESS_H
#define CPING_PING_PROCESS_H

#include <sys/types.h>

typedef struct {
    double interval;
    long count;
    double timeout;
    int force_ipv4;
    int force_ipv6;
    const char *host;
} PingOptions;

typedef struct {
    pid_t pid;
    int fd;
} PingProcess;

int ping_process_start(const PingOptions *options, PingProcess *process, char *errbuf, int errbuf_len);
void ping_process_terminate(PingProcess *process);

#endif
