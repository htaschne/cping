#include "ping_process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(__APPLE__)
#define CPING_PLATFORM_MACOS 1
#else
#define CPING_PLATFORM_MACOS 0
#endif

static void add_arg(char **argv, int *argc, const char *arg) {
    argv[(*argc)++] = (char *)arg;
}

static void format_double(char *buf, size_t len, double value) {
    snprintf(buf, len, "%.3f", value);
}

static void format_long(char *buf, size_t len, long value) {
    snprintf(buf, len, "%ld", value);
}

static int set_close_on_exec(int fd, char *errbuf, int errbuf_len) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        snprintf(errbuf, (size_t)errbuf_len, "fcntl(F_GETFD) failed: %s", strerror(errno));
        return -1;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        snprintf(errbuf, (size_t)errbuf_len, "fcntl(F_SETFD) failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static void build_ping_argv(const PingOptions *options,
                            char **argv,
                            int *argc,
                            char *interval_buf,
                            char *count_buf,
                            char *timeout_buf) {
    *argc = 0;
    add_arg(argv, argc, "ping");

    if (options->force_ipv4) {
        add_arg(argv, argc, "-4");
    }
    if (options->force_ipv6) {
        add_arg(argv, argc, "-6");
    }

    if (options->interval > 0.0) {
        format_double(interval_buf, 32, options->interval);
        add_arg(argv, argc, "-i");
        add_arg(argv, argc, interval_buf);
    }

    if (options->count > 0) {
        format_long(count_buf, 32, options->count);
        add_arg(argv, argc, "-c");
        add_arg(argv, argc, count_buf);
    }

    if (options->timeout > 0.0) {
#if CPING_PLATFORM_MACOS
        long timeout_ms = (long)(options->timeout * 1000.0 + 0.5);
        if (timeout_ms < 1) {
            timeout_ms = 1;
        }
        snprintf(timeout_buf, 32, "%ld", timeout_ms);
        add_arg(argv, argc, "-W");
        add_arg(argv, argc, timeout_buf);
#else
        format_double(timeout_buf, 32, options->timeout);
        add_arg(argv, argc, "-W");
        add_arg(argv, argc, timeout_buf);
#endif
    }

    add_arg(argv, argc, "--");
    add_arg(argv, argc, options->host);
    argv[*argc] = NULL;
}

int ping_process_start(const PingOptions *options, PingProcess *process, char *errbuf, int errbuf_len) {
    int pipefd[2];
    pid_t pid;
    char *argv[32];
    int argc;
    char interval_buf[32];
    char count_buf[32];
    char timeout_buf[32];

    if (pipe(pipefd) != 0) {
        snprintf(errbuf, (size_t)errbuf_len, "pipe failed: %s", strerror(errno));
        return -1;
    }
    if (set_close_on_exec(pipefd[0], errbuf, errbuf_len) != 0 ||
        set_close_on_exec(pipefd[1], errbuf, errbuf_len) != 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    build_ping_argv(options, argv, &argc, interval_buf, count_buf, timeout_buf);

    pid = fork();
    if (pid < 0) {
        snprintf(errbuf, (size_t)errbuf_len, "fork failed: %s", strerror(errno));
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
            fprintf(stderr, "dup2 failed: %s\n", strerror(errno));
            _exit(127);
        }
        close(pipefd[1]);
        if (setenv("LC_ALL", "C", 1) != 0) {
            fprintf(stderr, "setenv failed: %s\n", strerror(errno));
            _exit(127);
        }
        execvp("ping", argv);
        fprintf(stderr, "exec ping failed: %s\n", strerror(errno));
        _exit(127);
    }

    close(pipefd[1]);
    process->pid = pid;
    process->fd = pipefd[0];
    return 0;
}

void ping_process_terminate(PingProcess *process) {
    int status;
    if (process->pid > 0) {
        if (kill(process->pid, SIGTERM) == 0 || errno == ESRCH) {
            while (waitpid(process->pid, &status, 0) < 0 && errno == EINTR) {
            }
        }
        process->pid = -1;
    }
    if (process->fd >= 0) {
        close(process->fd);
        process->fd = -1;
    }
}
