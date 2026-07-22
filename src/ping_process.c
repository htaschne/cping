#include "ping_process.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
#define CPING_PLATFORM_MACOS 1
#else
#define CPING_PLATFORM_MACOS 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CPING_ARG_BUFSIZE 32U
#define CPING_HOST_BUFSIZE 1025U

static void child_write_bytes(const char *message, size_t len) {
    while (len > 0) {
        ssize_t written = write(STDERR_FILENO, message, len);
        if (written <= 0) {
            break;
        }
        message += written;
        len -= (size_t)written;
    }
}

static int add_arg(char **argv, size_t argv_len, int *argc, char *arg, char *errbuf, size_t errbuf_len) {
    if ((size_t)*argc + 1U >= argv_len) {
        (void)snprintf(errbuf, errbuf_len, "internal argument vector overflow");
        return -1;
    }
    argv[*argc] = arg;
    (*argc)++;
    argv[*argc] = NULL;
    return 0;
}

static int checked_snprintf_double(char *buf, size_t len, char *errbuf, size_t errbuf_len, double value) {
    int written = snprintf(buf, len, "%.3f", value);
    if (written < 0 || (size_t)written >= len) {
        (void)snprintf(errbuf, errbuf_len, "formatted numeric option was truncated");
        return -1;
    }
    return 0;
}

static int checked_snprintf_ulong(char *buf, size_t len, char *errbuf, size_t errbuf_len, unsigned long value) {
    int written = snprintf(buf, len, "%lu", value);
    if (written < 0 || (size_t)written >= len) {
        (void)snprintf(errbuf, errbuf_len, "formatted numeric option was truncated");
        return -1;
    }
    return 0;
}

static int set_close_on_exec(int fd, char *errbuf, size_t errbuf_len) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        (void)snprintf(errbuf, errbuf_len, "fcntl(F_GETFD) failed: %s", strerror(errno));
        return -1;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        (void)snprintf(errbuf, errbuf_len, "fcntl(F_SETFD) failed: %s", strerror(errno));
        return -1;
    }
    return 0;
}

static int close_checked(int fd, char *errbuf, size_t errbuf_len) {
    int saved_errno;

    while (close(fd) != 0) {
        saved_errno = errno;
        if (saved_errno == EINTR) {
            continue;
        }
        (void)snprintf(errbuf, errbuf_len, "close failed: %s", strerror(saved_errno));
        return -1;
    }
    return 0;
}

static void sleep_milliseconds(long milliseconds) {
    struct timespec requested;
    struct timespec remaining;

    requested.tv_sec = milliseconds / 1000L;
    requested.tv_nsec = (milliseconds % 1000L) * 1000000L;
    while (nanosleep(&requested, &remaining) != 0 && errno == EINTR) {
        requested = remaining;
    }
}

static void terminate_and_reap(pid_t pid) {
    int status;
    int attempts;

    (void)kill(pid, SIGTERM);
    for (attempts = 0; attempts < 50; attempts++) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid || (waited < 0 && errno == ECHILD)) {
            return;
        }
        sleep_milliseconds(20L);
    }
    (void)kill(pid, SIGKILL);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
}

static int resolve_ping_path(char *path, size_t path_len, char *errbuf, size_t errbuf_len) {
    const char *candidates[] = {"/sbin/ping", "/usr/sbin/ping", "/bin/ping", "/usr/bin/ping"};
    size_t i;

#if defined(CPING_ENABLE_TEST_HOOKS)
    const char *test_path = getenv("CPING_TEST_PING_PATH");
    if (test_path != NULL && test_path[0] == '/') {
        int written = snprintf(path, path_len, "%s", test_path);
        if (written < 0 || (size_t)written >= path_len) {
            (void)snprintf(errbuf, errbuf_len, "test ping path was truncated");
            return -1;
        }
        return 0;
    }
#endif

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        if (access(candidates[i], X_OK) == 0) {
            int written = snprintf(path, path_len, "%s", candidates[i]);
            if (written < 0 || (size_t)written >= path_len) {
                (void)snprintf(errbuf, errbuf_len, "ping path was truncated");
                return -1;
            }
            return 0;
        }
    }

    (void)snprintf(errbuf, errbuf_len, "could not find executable system ping in trusted paths");
    return -1;
}

static int build_ping_argv(const PingOptions *options,
                           char **argv,
                           size_t argv_len,
                           int *argc,
                           char *ping_path,
                           char *host_buf,
                           char *interval_buf,
                           char *count_buf,
                           char *timeout_buf,
                           char *errbuf,
                           size_t errbuf_len) {
    static char arg_4[] = "-4";
    static char arg_6[] = "-6";
    static char arg_i[] = "-i";
    static char arg_c[] = "-c";
    static char arg_w[] = "-W";
    int written;

    *argc = 0;
    argv[0] = NULL;

    written = snprintf(host_buf, CPING_HOST_BUFSIZE, "%s", options->host);
    if (written < 0 || (size_t)written >= CPING_HOST_BUFSIZE) {
        (void)snprintf(errbuf, errbuf_len, "host operand is too long");
        return -1;
    }

    if (add_arg(argv, argv_len, argc, ping_path, errbuf, errbuf_len) != 0) {
        return -1;
    }
    if (options->force_ipv4 && add_arg(argv, argv_len, argc, arg_4, errbuf, errbuf_len) != 0) {
        return -1;
    }
    if (options->force_ipv6 && add_arg(argv, argv_len, argc, arg_6, errbuf, errbuf_len) != 0) {
        return -1;
    }

    if (options->interval > 0.0) {
        if (checked_snprintf_double(interval_buf, CPING_ARG_BUFSIZE, errbuf, errbuf_len, options->interval) != 0 ||
            add_arg(argv, argv_len, argc, arg_i, errbuf, errbuf_len) != 0 ||
            add_arg(argv, argv_len, argc, interval_buf, errbuf, errbuf_len) != 0) {
            return -1;
        }
    }

    if (options->count > 0) {
        if (checked_snprintf_ulong(count_buf, CPING_ARG_BUFSIZE, errbuf, errbuf_len, options->count) != 0 ||
            add_arg(argv, argv_len, argc, arg_c, errbuf, errbuf_len) != 0 ||
            add_arg(argv, argv_len, argc, count_buf, errbuf, errbuf_len) != 0) {
            return -1;
        }
    }

    if (options->timeout > 0.0) {
#if CPING_PLATFORM_MACOS
        double timeout_ms_double = options->timeout * 1000.0 + 0.5;
        unsigned long timeout_ms = timeout_ms_double >= (double)ULONG_MAX ? ULONG_MAX : (unsigned long)timeout_ms_double;
        if (timeout_ms < 1UL) {
            timeout_ms = 1UL;
        }
        if (checked_snprintf_ulong(timeout_buf, CPING_ARG_BUFSIZE, errbuf, errbuf_len, timeout_ms) != 0 ||
            add_arg(argv, argv_len, argc, arg_w, errbuf, errbuf_len) != 0 ||
            add_arg(argv, argv_len, argc, timeout_buf, errbuf, errbuf_len) != 0) {
            return -1;
        }
#else
        if (checked_snprintf_double(timeout_buf, CPING_ARG_BUFSIZE, errbuf, errbuf_len, options->timeout) != 0 ||
            add_arg(argv, argv_len, argc, arg_w, errbuf, errbuf_len) != 0 ||
            add_arg(argv, argv_len, argc, timeout_buf, errbuf, errbuf_len) != 0) {
            return -1;
        }
#endif
    }

    return add_arg(argv, argv_len, argc, host_buf, errbuf, errbuf_len);
}

int ping_process_start(const PingOptions *options, PingProcess *process, char *errbuf, int errbuf_len) {
    int pipefd[2] = {-1, -1};
    pid_t pid;
    char *argv[32];
    static char env_lc_all[] = "LC_ALL=C";
    static char env_path[] = "PATH=/usr/bin:/bin:/usr/sbin:/sbin";
    char *envp[] = {env_lc_all, env_path, NULL};
    int argc;
    char ping_path[PATH_MAX];
    char host_buf[CPING_HOST_BUFSIZE];
    char interval_buf[CPING_ARG_BUFSIZE];
    char count_buf[CPING_ARG_BUFSIZE];
    char timeout_buf[CPING_ARG_BUFSIZE];
    size_t err_len = errbuf_len > 0 ? (size_t)errbuf_len : 0U;

    if (err_len > 0) {
        errbuf[0] = '\0';
    }

    if (resolve_ping_path(ping_path, sizeof(ping_path), errbuf, err_len) != 0) {
        return -1;
    }
    if (build_ping_argv(options,
                        argv,
                        sizeof(argv) / sizeof(argv[0]),
                        &argc,
                        ping_path,
                        host_buf,
                        interval_buf,
                        count_buf,
                        timeout_buf,
                        errbuf,
                        err_len) != 0) {
        return -1;
    }
    (void)argc;

    if (pipe(pipefd) != 0) {
        (void)snprintf(errbuf, err_len, "pipe failed: %s", strerror(errno));
        return -1;
    }
    if (set_close_on_exec(pipefd[0], errbuf, err_len) != 0 ||
        set_close_on_exec(pipefd[1], errbuf, err_len) != 0) {
        (void)close_checked(pipefd[0], errbuf, err_len);
        (void)close_checked(pipefd[1], errbuf, err_len);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        (void)snprintf(errbuf, err_len, "fork failed: %s", strerror(errno));
        (void)close_checked(pipefd[0], errbuf, err_len);
        (void)close_checked(pipefd[1], errbuf, err_len);
        return -1;
    }

    if (pid == 0) {
        (void)close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0 || dup2(pipefd[1], STDERR_FILENO) < 0) {
            child_write_bytes("cping child: dup2 failed\n", sizeof("cping child: dup2 failed\n") - 1U);
            _exit(127);
        }
        if (pipefd[1] != STDOUT_FILENO && pipefd[1] != STDERR_FILENO) {
            (void)close(pipefd[1]);
        }
        execve(ping_path, argv, envp);
        child_write_bytes("cping child: exec ping failed\n", sizeof("cping child: exec ping failed\n") - 1U);
        _exit(127);
    }

    if (close_checked(pipefd[1], errbuf, err_len) != 0) {
        terminate_and_reap(pid);
        (void)close_checked(pipefd[0], errbuf, err_len);
        return -1;
    }
    process->pid = pid;
    process->fd = pipefd[0];
    return 0;
}

void ping_process_terminate(PingProcess *process) {
    if (process->pid > 0) {
        terminate_and_reap(process->pid);
        process->pid = -1;
    }
    if (process->fd >= 0) {
        (void)close(process->fd);
        process->fd = -1;
    }
}
