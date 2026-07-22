# cping

`cping` is a portable C latency monitor that wraps the system `ping` command and renders a continuously updating terminal dashboard.

![Animated terminal demo of cping google.com showing the latency bar, packet loss, and final statistics](docs/demo.svg)

## Features

- Single-line interactive display for terminals, with a latency bar, current RTT, standard deviation, sample count, packet loss, and optional min/avg/max values on wide screens.
- Plain line-oriented output when stdout is redirected or piped.
- macOS and Linux support through the platform `ping` executable.
- No root requirement and no raw ICMP sockets in the initial backend.
- Online statistics with no stored sample history.
- UTF-8 block bar with ASCII fallback.
- Restrained terminal colors that respect `NO_COLOR`, `TERM=dumb`, and `--no-color`.
- Clean signal handling: cursor restoration, child termination, `waitpid`, final newline, and summary output.

## Installation

```sh
make
make install PREFIX=$HOME/.local
```

This installs `cping` to `$HOME/.local/bin/cping`. Use another `PREFIX` if you prefer a different installation root.

## Quick Start

```sh
./cping google.com
```

Press `Ctrl+C` to stop an unlimited run. `cping` restores the terminal cursor and prints a final summary.

## Examples

Normal execution:

```sh
./cping google.com
```

ASCII mode:

```sh
./cping --ascii google.com
```

Finite probe count:

```sh
./cping -c 4 127.0.0.1
```

Redirected output:

```sh
./cping -c 2 127.0.0.1 > samples.txt
```

Example redirected sample lines:

```text
127.0.0.1 rtt=0.068 ms stddev=0.000 ms samples=1 loss=0.0% min=0.068 avg=0.068 max=0.068
127.0.0.1 rtt=0.102 ms stddev=0.024 ms samples=2 loss=0.0% min=0.068 avg=0.085 max=0.102

--- 127.0.0.1 latency statistics ---
2 probes, 2 replies, 0.0% packet loss
rtt min/avg/max/stddev = 0.07/0.08/0.10/0.02 ms
```

## Command-Line Flags

```text
Usage: cping [options] <host>
Options:
  -i, --interval <seconds>   Delay between probes, default 1.0
  -c, --count <number>       Stop after this many probes
  -m, --max-latency <ms>     Full-bar latency threshold, default 200
  -W, --timeout <seconds>    Per-probe timeout
      --ascii                Use ASCII bar characters
      --no-color             Disable terminal colors
  -4                         Force IPv4
  -6                         Force IPv6
  -h, --help                 Show help
  -v, --version              Show version
```

Numeric arguments are validated. Counts must be positive integers. Intervals, timeouts, and max-latency ceilings must be greater than zero.

## How it Works

`cping` starts the operating system `ping` executable with `fork`, `execvp`, and a pipe. It does not invoke a shell. The child process runs with `LC_ALL=C`, which keeps `ping` output predictable for parsing.

The first backend wraps system `ping` instead of implementing raw ICMP because raw sockets usually require elevated privileges or platform-specific capabilities. Delegating probe transmission to the OS command keeps the program usable as a normal user and lets macOS and Linux handle their own ICMP details.

The parser reads `ping` output incrementally and extracts latency from the `time=` field without depending on the full sentence around it. It handles common forms such as:

```text
64 bytes from 172.217.162.174: icmp_seq=0 ttl=111 time=36.518 ms
64 bytes from 142.250.72.14: icmp_seq=1 ttl=115 time=12.7 ms
64 bytes from host: icmp_seq=4 ttl=64 time<1 ms
```

Localized decimal commas are not parsed.

## Statistics

Successful replies update a Welford accumulator containing count, mean, minimum, maximum, and `M2`, the sum of squared differences from the current mean. Sample standard deviation is computed as:

```text
sqrt(M2 / (count - 1))
```

The first successful sample reports `0.00 ms` standard deviation. Timed-out probes are excluded from latency statistics.

Packet loss is inferred from ICMP sequence numbers when `ping` prints them. `cping` tracks the first and largest observed sequence number, including timeout lines such as `Request timeout for icmp_seq 3` when available. If sequence numbers are unavailable, it falls back to counting observed replies, which can underreport loss for unusual `ping` output formats.

## Latency Bar

The bar is not progress toward completion. It is the current latency scaled against a configurable ceiling.

```text
filled_columns = round(current_rtt_ms / max_latency_ms * bar_width)
```

Values equal to or above the ceiling fill the entire bar. The default ceiling is `200 ms`. The preferred bar width is `30` columns, with a minimum of `10`; interactive terminals use `ioctl(TIOCGWINSZ)` and `SIGWINCH` to adapt to the current terminal width.

UTF-8 terminals render:

```text
[███████████░░░░░░░░░░░░░░░░░░░]
```

ASCII mode renders:

```text
[###########-------------------]
```

## Architecture

- `src/main.c`: option parsing, event loop, signal handling, rendering flow, final summary.
- `src/ping_process.c`: `fork`/`execvp` child process setup and `ping` argument construction.
- `src/parser.c`: line parser for latency and ICMP sequence numbers.
- `src/stats.c`: Welford online statistics.
- `src/terminal.c`: TTY detection, terminal width, cursor control, colors, and bar rendering.
- `tests/`: focused unit tests for parser and statistics behavior.

## Portability

macOS and Linux both expose a `ping` command, but some flags differ. `cping` maps its own interface to the platform command at compile time:

- `-i` is passed through for probe interval. Fractional intervals depend on platform `ping` support and local permissions.
- `-c` is passed through for finite probe count.
- `-W` is seconds on Linux `ping`; on macOS, `cping` converts seconds to milliseconds for the platform timeout flag.
- `-4` and `-6` are passed through. Availability depends on the installed `ping`.
- `--` is passed before the host so hostnames beginning with `-` cannot be interpreted as additional options.

## Building

```sh
make
make test
make install PREFIX=$HOME/.local
```

The default build uses:

```text
-std=c99 -Wall -Wextra -pedantic -O2
```

## Running Tests

```sh
make test
```

The test suite covers parser behavior for representative macOS/Linux `ping` output and Welford statistics.

## Roadmap

The current release is intentionally small: one backend, one display mode, and focused tests for the most failure-prone parsing and math paths.

## Future Ideas

- Native raw ICMP backend.
- Sparkline history mode.
- Jitter graph.
- IPv6 improvements.
- JSON output.
- Configurable themes.

## Contributing

Keep changes portable C99 unless a platform-specific section is necessary. Prefer small modules with focused tests. If a change depends on a particular `ping` output variant, add a parser test that captures that variant.

Before opening a pull request:

```sh
make clean
make
make test
```

## License

MIT. See [LICENSE](LICENSE).
