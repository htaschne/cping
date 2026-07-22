#include "parser.h"
#include "ping_process.h"
#include "stats.h"
#include "terminal.h"

#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CPING_VERSION "0.1.1"

typedef struct {
    double interval;
    long count;
    double max_latency;
    double timeout;
    int force_ipv4;
    int force_ipv6;
    int ascii;
    int no_color;
    const char *host;
} Options;

typedef struct {
    long received;
    long transmitted;
    int saw_sequence;
    long first_sequence;
    long max_sequence;
} ProbeCounts;

static volatile sig_atomic_t g_stop_requested = 0;
static volatile sig_atomic_t g_resize_requested = 0;

static void handle_signal(int signal_number) {
    if (signal_number == SIGWINCH) {
        g_resize_requested = 1;
    } else {
        g_stop_requested = signal_number;
    }
}

static void usage(FILE *stream) {
    fprintf(stream,
            "Usage: cping [options] <host>\n"
            "Options:\n"
            "  -i, --interval <seconds>   Delay between probes, default 1.0\n"
            "  -c, --count <number>       Stop after this many probes\n"
            "  -m, --max-latency <ms>     Full-bar latency threshold, default 200\n"
            "  -W, --timeout <seconds>    Per-probe timeout\n"
            "      --ascii                Use ASCII bar characters\n"
            "      --no-color             Disable terminal colors\n"
            "  -4                         Force IPv4\n"
            "  -6                         Force IPv6\n"
            "  -h, --help                 Show help\n"
            "  -v, --version              Show version\n");
}

static int parse_double_arg(const char *text, const char *name, double *value, int allow_zero) {
    char *end = NULL;
    errno = 0;
    double parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "cping: invalid %s: %s\n", name, text);
        return 0;
    }
    if (parsed < 0.0 || (!allow_zero && parsed == 0.0)) {
        fprintf(stderr, "cping: %s must be %s\n", name, allow_zero ? "non-negative" : "greater than zero");
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_long_arg(const char *text, const char *name, long *value) {
    char *end = NULL;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        fprintf(stderr, "cping: invalid %s: %s\n", name, text);
        return 0;
    }
    if (parsed <= 0) {
        fprintf(stderr, "cping: %s must be greater than zero\n", name);
        return 0;
    }
    *value = parsed;
    return 1;
}

static int parse_options(int argc, char **argv, Options *options) {
    static const struct option long_options[] = {
        {"interval", required_argument, NULL, 'i'},
        {"count", required_argument, NULL, 'c'},
        {"max-latency", required_argument, NULL, 'm'},
        {"timeout", required_argument, NULL, 'W'},
        {"ascii", no_argument, NULL, 1000},
        {"no-color", no_argument, NULL, 1001},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };
    int ch;

    options->interval = 1.0;
    options->count = 0;
    options->max_latency = 200.0;
    options->timeout = 0.0;
    options->force_ipv4 = 0;
    options->force_ipv6 = 0;
    options->ascii = 0;
    options->no_color = 0;
    options->host = NULL;

    while ((ch = getopt_long(argc, argv, "i:c:m:W:46hv", long_options, NULL)) != -1) {
        switch (ch) {
        case 'i':
            if (!parse_double_arg(optarg, "interval", &options->interval, 0)) {
                return 0;
            }
            break;
        case 'c':
            if (!parse_long_arg(optarg, "count", &options->count)) {
                return 0;
            }
            break;
        case 'm':
            if (!parse_double_arg(optarg, "max latency", &options->max_latency, 0)) {
                return 0;
            }
            break;
        case 'W':
            if (!parse_double_arg(optarg, "timeout", &options->timeout, 0)) {
                return 0;
            }
            break;
        case '4':
            options->force_ipv4 = 1;
            break;
        case '6':
            options->force_ipv6 = 1;
            break;
        case 'h':
            usage(stdout);
            exit(0);
        case 'v':
            printf("cping %s\n", CPING_VERSION);
            exit(0);
        case 1000:
            options->ascii = 1;
            break;
        case 1001:
            options->no_color = 1;
            break;
        default:
            usage(stderr);
            return 0;
        }
    }

    if (options->force_ipv4 && options->force_ipv6) {
        fprintf(stderr, "cping: choose only one of -4 or -6\n");
        return 0;
    }
    if (optind >= argc) {
        fprintf(stderr, "cping: missing host\n");
        usage(stderr);
        return 0;
    }
    if (optind + 1 != argc) {
        fprintf(stderr, "cping: too many operands\n");
        return 0;
    }

    options->host = argv[optind];
    return 1;
}

static int choose_bar_width(const Terminal *term, const char *host) {
    int preferred = 30;
    int minimum = 10;
    int reserved = (int)strlen(host) + 54;
    int available = term->width - reserved;

    if (!term->is_tty) {
        return preferred;
    }
    if (available >= preferred) {
        return preferred;
    }
    if (available >= minimum) {
        return available;
    }
    return minimum;
}

static long counts_transmitted(const ProbeCounts *counts) {
    if (counts->saw_sequence) {
        long from_sequence = counts->max_sequence - counts->first_sequence + 1;
        return from_sequence > counts->transmitted ? from_sequence : counts->transmitted;
    }
    return counts->transmitted > 0 ? counts->transmitted : counts->received;
}

static double packet_loss_percent(const ProbeCounts *counts) {
    long transmitted = counts_transmitted(counts);
    if (transmitted <= 0) {
        return 0.0;
    }
    return 100.0 * (double)(transmitted - counts->received) / (double)transmitted;
}

static void observe_sequence(ProbeCounts *counts, long sequence) {
    if (!counts->saw_sequence) {
        counts->saw_sequence = 1;
        counts->first_sequence = sequence;
        counts->max_sequence = sequence;
        counts->transmitted = 1;
        return;
    }
    if (sequence > counts->max_sequence) {
        counts->max_sequence = sequence;
    }
    counts->transmitted = counts_transmitted(counts);
}

static void render_sample(const Terminal *term,
                          const Options *options,
                          const Stats *stats,
                          const ProbeCounts *counts,
                          double latest_ms) {
    char bar[256];
    int bar_width = choose_bar_width(term, options->host);
    double loss = packet_loss_percent(counts);
    const char *reset = term->use_color ? "\033[0m" : "";
    const char *latency_color = term->use_color ? terminal_color_for_latency(latest_ms, options->max_latency) : "";
    const char *loss_color = term->use_color && loss > 0.0 ? "\033[31m" : "";

    terminal_make_bar(bar, sizeof(bar), bar_width, latest_ms, options->max_latency, term->use_unicode);

    if (term->is_tty) {
        terminal_clear_line(term);
        printf("%s %s%s%s %.2f ms  sigma %.2f ms  n=%ld  %s%.1f%%%s loss",
               options->host,
               latency_color,
               bar,
               reset,
               latest_ms,
               stats_stddev(stats),
               counts->received,
               loss_color,
               loss,
               reset);
        if (term->width >= 105 && stats->count > 0) {
            printf("  min/avg/max %.2f/%.2f/%.2f ms", stats->min, stats->mean, stats->max);
        }
        fflush(stdout);
    } else {
        printf("%s rtt=%.3f ms stddev=%.3f ms samples=%ld loss=%.1f%%",
               options->host,
               latest_ms,
               stats_stddev(stats),
               counts->received,
               loss);
        if (stats->count > 0) {
            printf(" min=%.3f avg=%.3f max=%.3f", stats->min, stats->mean, stats->max);
        }
        fputc('\n', stdout);
        fflush(stdout);
    }
}

static void print_summary(const Options *options, const Stats *stats, const ProbeCounts *counts) {
    long transmitted = counts_transmitted(counts);
    double loss = packet_loss_percent(counts);

    printf("--- %s latency statistics ---\n", options->host);
    printf("%ld probes, %ld replies, %.1f%% packet loss\n", transmitted, counts->received, loss);
    if (stats->count > 0) {
        printf("rtt min/avg/max/stddev = %.2f/%.2f/%.2f/%.2f ms\n",
               stats->min,
               stats->mean,
               stats->max,
               stats_stddev(stats));
    } else {
        printf("rtt min/avg/max/stddev = n/a\n");
    }
}

static void process_line(char *line,
                         const Terminal *term,
                         const Options *options,
                         Stats *stats,
                         ProbeCounts *counts) {
    PingReply reply;
    if (parse_ping_reply(line, &reply)) {
        if (reply.has_sequence) {
            observe_sequence(counts, reply.sequence);
        } else {
            counts->transmitted++;
        }
        counts->received++;
        stats_add(stats, reply.latency_ms);
        render_sample(term, options, stats, counts, reply.latency_ms);
    } else if (reply.has_sequence) {
        observe_sequence(counts, reply.sequence);
    }
}

static int install_signal_handlers(void) {
    struct sigaction action = {0};
    action.sa_handler = handle_signal;
    if (sigemptyset(&action.sa_mask) != 0) {
        return -1;
    }
    if (sigaction(SIGINT, &action, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGTERM, &action, NULL) != 0) {
        return -1;
    }
    if (sigaction(SIGWINCH, &action, NULL) != 0) {
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    Options options;
    PingOptions ping_options;
    PingProcess process;
    Terminal term;
    Stats stats;
    ProbeCounts counts;
    char errbuf[256];
    char readbuf[1024];
    char linebuf[4096];
    size_t line_len = 0;
    int child_status = 0;
    int child_done = 0;
    int exit_code = 0;

    if (!parse_options(argc, argv, &options)) {
        return 2;
    }

    terminal_init(&term, options.ascii, options.no_color);
    stats_init(&stats);
    memset(&counts, 0, sizeof(counts));

    ping_options.interval = options.interval;
    ping_options.count = options.count;
    ping_options.timeout = options.timeout;
    ping_options.force_ipv4 = options.force_ipv4;
    ping_options.force_ipv6 = options.force_ipv6;
    ping_options.host = options.host;

    process.pid = -1;
    process.fd = -1;
    if (ping_process_start(&ping_options, &process, errbuf, sizeof(errbuf)) != 0) {
        fprintf(stderr, "cping: %s\n", errbuf);
        return 1;
    }

    if (install_signal_handlers() != 0) {
        fprintf(stderr, "cping: failed to install signal handlers: %s\n", strerror(errno));
        ping_process_terminate(&process);
        return 1;
    }
    terminal_hide_cursor(&term);

    while (!child_done) {
        fd_set rfds;
        int ready;

        if (g_resize_requested) {
            terminal_refresh_size(&term);
            g_resize_requested = 0;
        }

        if (g_stop_requested) {
            if (kill(process.pid, SIGTERM) != 0 && errno != ESRCH) {
                fprintf(stderr, "cping: failed to terminate ping: %s\n", strerror(errno));
                exit_code = 1;
                break;
            }
        }

        FD_ZERO(&rfds);
        FD_SET(process.fd, &rfds);
        ready = select(process.fd + 1, &rfds, NULL, NULL, NULL);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            fprintf(stderr, "cping: select failed: %s\n", strerror(errno));
            exit_code = 1;
            break;
        }

        if (FD_ISSET(process.fd, &rfds)) {
            ssize_t nread = read(process.fd, readbuf, sizeof(readbuf));
            if (nread < 0) {
                if (errno == EINTR) {
                    continue;
                }
                fprintf(stderr, "cping: read failed: %s\n", strerror(errno));
                exit_code = 1;
                break;
            }
            if (nread == 0) {
                child_done = 1;
                break;
            }

            for (ssize_t i = 0; i < nread; i++) {
                if (readbuf[i] == '\n') {
                    linebuf[line_len] = '\0';
                    process_line(linebuf, &term, &options, &stats, &counts);
                    line_len = 0;
                } else if (line_len + 1 < sizeof(linebuf)) {
                    linebuf[line_len++] = readbuf[i];
                }
            }
        }
    }

    if (line_len > 0) {
        linebuf[line_len] = '\0';
        process_line(linebuf, &term, &options, &stats, &counts);
    }

    if (process.fd >= 0) {
        close(process.fd);
        process.fd = -1;
    }
    if (process.pid > 0) {
        int status = 0;
        pid_t waited = waitpid(process.pid, &status, WNOHANG);
        if (waited == 0) {
            if (kill(process.pid, SIGTERM) != 0 && errno != ESRCH) {
                fprintf(stderr, "cping: failed to terminate ping: %s\n", strerror(errno));
                exit_code = 1;
            }
            while (waitpid(process.pid, &status, 0) < 0 && errno == EINTR) {
            }
        } else if (waited < 0 && errno != ECHILD) {
            status = 0;
        }
        child_status = status;
        process.pid = -1;
    }

    terminal_show_cursor(&term);
    terminal_finish_line(&term);
    print_summary(&options, &stats, &counts);

    if (g_stop_requested == SIGINT) {
        return 130;
    }
    if (g_stop_requested == SIGTERM) {
        return 143;
    }
    if (exit_code != 0) {
        return exit_code;
    }
    if (WIFEXITED(child_status) && WEXITSTATUS(child_status) != 0 && stats.count == 0) {
        return WEXITSTATUS(child_status);
    }
    if (WIFSIGNALED(child_status)) {
        return 128 + WTERMSIG(child_status);
    }
    return 0;
}
