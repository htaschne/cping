#include "parser.h"
#include "line_reader.h"
#include "ping_process.h"
#include "stats.h"
#include "terminal.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CPING_VERSION "0.1.1"
#define CPING_MAX_HOST_DISPLAY 256U
#define CPING_MAX_HOST_LEN 1024U

typedef struct {
    double interval;
    unsigned long count;
    double max_latency;
    double timeout;
    int force_ipv4;
    int force_ipv6;
    int ascii;
    int no_color;
    const char *host;
    char display_host[CPING_MAX_HOST_DISPLAY];
} Options;

typedef struct {
    unsigned long received;
    unsigned long transmitted;
    int saw_sequence;
    unsigned long first_sequence;
    unsigned long max_sequence;
} ProbeCounts;

typedef struct {
    const Terminal *term;
    const Options *options;
    Stats *stats;
    ProbeCounts *counts;
} ProcessLineContext;

static volatile sig_atomic_t g_stop_requested = 0;
static volatile sig_atomic_t g_resize_requested = 0;
static volatile sig_atomic_t g_output_closed = 0;

static void handle_signal(int signal_number) {
    if (signal_number == SIGWINCH) {
        g_resize_requested = 1;
    } else if (signal_number == SIGPIPE) {
        g_output_closed = 1;
        g_stop_requested = signal_number;
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
    if (errno == ERANGE || end == text || *end != '\0' || !isfinite(parsed)) {
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

static int parse_count_arg(const char *text, const char *name, unsigned long *value) {
    char *end = NULL;
    unsigned long parsed;

    if (text[0] == '-') {
        fprintf(stderr, "cping: %s must be greater than zero\n", name);
        return 0;
    }
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0') {
        fprintf(stderr, "cping: invalid %s: %s\n", name, text);
        return 0;
    }
    if (parsed == 0) {
        fprintf(stderr, "cping: %s must be greater than zero\n", name);
        return 0;
    }
    *value = parsed;
    return 1;
}

static int host_is_safe_operand(const char *host) {
    size_t i;
    size_t len = strlen(host);

    if (len == 0 || len > CPING_MAX_HOST_LEN || host[0] == '-') {
        return 0;
    }
    for (i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)host[i];
        if (iscntrl(ch) || isspace(ch)) {
            return 0;
        }
    }
    return 1;
}

static void sanitize_display_string(const char *src, char *dst, size_t dst_len) {
    size_t used = 0;
    size_t i;

    if (dst_len == 0) {
        return;
    }
    for (i = 0; src[i] != '\0' && used + 1 < dst_len; i++) {
        unsigned char ch = (unsigned char)src[i];
        if (ch >= 0x20U && ch != 0x7FU) {
            dst[used++] = (char)ch;
        } else if (used + 2 < dst_len) {
            dst[used++] = '?';
        }
    }
    dst[used] = '\0';
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
            if (!parse_count_arg(optarg, "count", &options->count)) {
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
    if (!host_is_safe_operand(options->host)) {
        fprintf(stderr, "cping: unsafe host operand\n");
        return 0;
    }
    sanitize_display_string(options->host, options->display_host, sizeof(options->display_host));
    return 1;
}

static int choose_bar_width(const Terminal *term, const char *host) {
    int preferred = 30;
    int minimum = 10;
    size_t host_len = strlen(host);
    int reserved;
    int available;

    if (host_len > 200U) {
        host_len = 200U;
    }
    reserved = (int)host_len + 54;
    available = term->width > reserved ? term->width - reserved : 0;

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

static unsigned long counts_transmitted(const ProbeCounts *counts) {
    if (counts->saw_sequence) {
        unsigned long from_sequence = counts->max_sequence >= counts->first_sequence
                                          ? counts->max_sequence - counts->first_sequence + 1UL
                                          : counts->transmitted;
        return from_sequence > counts->transmitted ? from_sequence : counts->transmitted;
    }
    return counts->transmitted > 0 ? counts->transmitted : counts->received;
}

static double packet_loss_percent(const ProbeCounts *counts) {
    unsigned long transmitted = counts_transmitted(counts);
    if (transmitted == 0) {
        return 0.0;
    }
    if (counts->received >= transmitted) {
        return 0.0;
    }
    return 100.0 * (double)(transmitted - counts->received) / (double)transmitted;
}

static void observe_sequence(ProbeCounts *counts, unsigned long sequence) {
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
    if (counts->transmitted != ULONG_MAX) {
        counts->transmitted++;
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
        printf("%s %s%s%s %.2f ms  sigma %.2f ms  n=%lu  %s%.1f%%%s loss",
               options->display_host,
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
    } else {
        printf("%s rtt=%.3f ms stddev=%.3f ms samples=%lu loss=%.1f%%",
               options->display_host,
               latest_ms,
               stats_stddev(stats),
               counts->received,
               loss);
        if (stats->count > 0) {
            printf(" min=%.3f avg=%.3f max=%.3f", stats->min, stats->mean, stats->max);
        }
        fputc('\n', stdout);
    }
    if (fflush(stdout) == EOF || ferror(stdout)) {
        g_output_closed = 1;
        g_stop_requested = SIGPIPE;
    }
}

static void bump_received(ProbeCounts *counts) {
    if (counts->received != ULONG_MAX) {
        counts->received++;
    }
}

static void print_summary(const Options *options, const Stats *stats, const ProbeCounts *counts) {
    unsigned long transmitted = counts_transmitted(counts);
    double loss = packet_loss_percent(counts);

    printf("--- %s latency statistics ---\n", options->display_host);
    printf("%lu probes, %lu replies, %.1f%% packet loss\n", transmitted, counts->received, loss);
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

static void process_line(const char *line,
                         size_t len,
                         int truncated,
                         const Terminal *term,
                         const Options *options,
                         Stats *stats,
                         ProbeCounts *counts) {
    PingReply reply;

    if (truncated) {
        return;
    }

    if (parse_ping_reply_len(line, len, &reply)) {
        if (reply.has_sequence) {
            observe_sequence(counts, reply.sequence);
        } else if (counts->transmitted != ULONG_MAX) {
            counts->transmitted++;
        }
        if (stats_add(stats, reply.latency_ms)) {
            bump_received(counts);
            render_sample(term, options, stats, counts, reply.latency_ms);
        }
    } else if (reply.has_sequence) {
        observe_sequence(counts, reply.sequence);
    }
}

static void process_line_callback(const char *line, size_t len, int truncated, void *context) {
    ProcessLineContext *ctx = (ProcessLineContext *)context;
    process_line(line, len, truncated, ctx->term, ctx->options, ctx->stats, ctx->counts);
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

static int wait_for_child_exit(pid_t pid, int *status, int terminate, int *forced_kill) {
    int attempts;

    *forced_kill = 0;
    for (attempts = 0; attempts < 50; attempts++) {
        pid_t waited = waitpid(pid, status, WNOHANG);
        if (waited == pid) {
            return 0;
        }
        if (waited < 0) {
            return errno == ECHILD ? 0 : -1;
        }
        if (attempts == 0 && terminate) {
            if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
                return -1;
            }
        }
        sleep_milliseconds(20L);
    }

    if (terminate) {
        if (kill(pid, SIGKILL) != 0 && errno != ESRCH) {
            return -1;
        }
        *forced_kill = 1;
        while (waitpid(pid, status, 0) < 0) {
            if (errno != EINTR) {
                return errno == ECHILD ? 0 : -1;
            }
        }
        return 0;
    }
    return 1;
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
    if (sigaction(SIGPIPE, &action, NULL) != 0) {
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
    LineReader reader;
    ProcessLineContext line_context;
    char errbuf[256];
    unsigned char readbuf[1024];
    int child_status = 0;
    int exit_code = 0;

    if (!parse_options(argc, argv, &options)) {
        return 2;
    }

    terminal_init(&term, options.ascii, options.no_color);
    stats_init(&stats);
    line_reader_init(&reader);
    memset(&counts, 0, sizeof(counts));
    line_context.term = &term;
    line_context.options = &options;
    line_context.stats = &stats;
    line_context.counts = &counts;

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

    while (!g_output_closed) {
        fd_set rfds;
        int ready;

        if (g_resize_requested) {
            terminal_refresh_size(&term);
            g_resize_requested = 0;
        }

        if (g_stop_requested && process.pid > 0) {
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
                break;
            }

            line_reader_feed(&reader, readbuf, (size_t)nread, process_line_callback, &line_context);
        }
    }

    if (!g_output_closed) {
        line_reader_finish(&reader, process_line_callback, &line_context);
    }

    if (process.fd >= 0) {
        close(process.fd);
        process.fd = -1;
    }
    if (process.pid > 0) {
        int status = 0;
        int forced_kill = 0;
        int wait_result = wait_for_child_exit(process.pid, &status, 1, &forced_kill);
        if (wait_result != 0) {
            fprintf(stderr, "cping: failed to reap ping: %s\n", strerror(errno));
            exit_code = 1;
        }
        if (forced_kill && exit_code == 0) {
            exit_code = 1;
        }
        child_status = status;
        process.pid = -1;
    }

    if (!g_output_closed) {
        terminal_show_cursor(&term);
        terminal_finish_line(&term);
        print_summary(&options, &stats, &counts);
    }

    if (g_output_closed) {
        return 0;
    }
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
