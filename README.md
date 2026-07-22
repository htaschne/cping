# cping

[![Build Status](https://github.com/htaschne/cping/actions/workflows/ci.yml/badge.svg)](https://github.com/htaschne/cping/actions/workflows/ci.yml)
[![Latest Release](https://img.shields.io/github/v/release/htaschne/cping?label=release)](https://github.com/htaschne/cping/releases/latest)

`cping` is `ping`, but cuter: one live terminal line, a little latency bar, and stats that update while you watch.

![Animated terminal demo of cping google.com showing the latency bar, packet loss, and final statistics](docs/demo.svg)

## Install

```sh
make
make install PREFIX=$HOME/.local
```

Or just run it:

```sh
make
./cping google.com
```

## What it Shows

```text
google.com [███████████░░░░░░░░░░░░░░░░░░░] 36.52 ms  sigma 2.71 ms  n=24  0.0% loss
```

- target host
- live latency bar
- latest round-trip time
- standard deviation
- successful sample count
- packet loss
- min/avg/max when your terminal is wide enough

## Cute Little Commands

Normal:

```sh
./cping google.com
```

ASCII-only, for terminals that are feeling shy:

```sh
./cping --ascii google.com
```

Stop after a few probes:

```sh
./cping -c 4 127.0.0.1
```

Save plain output:

```sh
./cping -c 2 127.0.0.1 > samples.txt
```

## Options

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

## How it Works

`cping` wraps your system `ping` command instead of doing raw ICMP itself. That keeps it portable across macOS and Linux, and it means you do not need to run it as root.

It reads `ping` output through a pipe, looks for the `time=` field, and updates the same terminal line. If stdout is redirected, it skips the terminal magic and prints one tidy sample per line.

The stats use Welford's algorithm, so `cping` can track average latency and sample standard deviation without saving every measurement.

The bar is just:

```text
current latency / max latency
```

By default, `200 ms` fills the whole bar. Change that with `--max-latency`.

## Build + Test

```sh
make
make test
make install PREFIX=$HOME/.local
```

Audit-oriented targets are optional and not used for release binaries:

```sh
make strict
make sanitize
make sanitize-undefined
make analyze
make fuzz-smoke
```

On Linux, `make sanitize` uses AddressSanitizer plus UndefinedBehaviorSanitizer. On macOS, the default target uses UndefinedBehaviorSanitizer because Apple Clang's AddressSanitizer runtime can require host support that is not available in restricted runners. `make sanitize-undefined` is explicitly UBSan-only on all platforms.

## Security and Robustness

`cping` treats command-line operands, terminal state, environment, and all child `ping` output as untrusted.

- It starts `ping` without a shell and passes a fixed argument vector.
- Release builds resolve `ping` from trusted system paths: `/sbin/ping`, `/usr/sbin/ping`, `/bin/ping`, then `/usr/bin/ping`. The current directory and arbitrary `PATH` entries are not searched.
- Host operands must not be empty, start with `-`, contain whitespace/control bytes, or exceed the bounded operand length. Displayed host text is sanitized before terminal output.
- Child output is line-assembled with a fixed 4096-byte logical-line limit. Overlong lines are discarded until the next newline and are not parsed as replies.
- The parser rejects embedded NUL bytes, malformed RTT fields, non-finite values, negative values, and implausibly large RTT values. It does not print child-controlled output into the dashboard.
- Statistics are maintained online using Welford's algorithm and report sample standard deviation without storing every RTT sample.
- Signal handlers only set flags. Shutdown, terminal restoration, child termination, and reaping happen in normal control flow.
- `SIGPIPE`/`EPIPE` from closed output consumers is treated as expected shutdown.

Remaining limitations: `cping` still delegates network probing to the platform `ping` implementation and only parses common macOS/Linux reply formats. It does not formally verify terminal emulators, platform `ping` behavior, or descendants spawned by a malicious replacement executable.

## Demo

The README preview lives at `docs/demo.svg`. To record a fresh one:

```sh
make demo
```

You will need `asciinema` plus a renderer such as `agg`, `svg-term`, or `termtosvg`.

## License

MIT, babe. See [LICENSE](LICENSE).
