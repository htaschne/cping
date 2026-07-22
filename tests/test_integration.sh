#!/bin/sh
set -eu

CPING_BIN=$1
TMPDIR=${TMPDIR:-/tmp}
WORK=$(mktemp -d "$TMPDIR/cping-test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT INT TERM

make_fake_ping() {
    mode=$1
    cat > "$WORK/ping" <<EOF
#!/bin/sh
mode=$mode
case "\$mode" in
normal)
  printf '%s\n' '64 bytes from 127.0.0.1: icmp_seq=0 ttl=64 time=1.250 ms'
  printf '%s\n' '64 bytes from 127.0.0.1: icmp_seq=1 ttl=64 time=2.500 ms'
  ;;
malformed)
  printf '\033[31mtime=nan\033[0m\n'
  printf '%s\n' '64 bytes from 127.0.0.1: icmp_seq=9 ttl=64 time=3.000 ms'
  ;;
nonewline)
  printf '%s' '64 bytes from 127.0.0.1: icmp_seq=4 ttl=64 time=4.000 ms'
  ;;
long)
  perl -e 'print "x" x 5000, "\n"; print "64 bytes from 127.0.0.1: icmp_seq=5 ttl=64 time=5.000 ms\n"'
  ;;
fail)
  printf '%s\n' 'bad output'
  exit 7
  ;;
esac
EOF
    chmod +x "$WORK/ping"
}

run_case() {
    mode=$1
    make_fake_ping "$mode"
    CPING_TEST_PING_PATH="$WORK/ping" "$CPING_BIN" -c 2 127.0.0.1
}

normal_output=$(run_case normal)
printf '%s\n' "$normal_output" | grep '2 probes, 2 replies, 0.0% packet loss' >/dev/null

malformed_output=$(run_case malformed)
printf '%s\n' "$malformed_output" | grep '1 replies' >/dev/null
if printf '%s\n' "$malformed_output" | grep "$(printf '\033')" >/dev/null; then
    echo "test_integration: terminal escape leaked" >&2
    exit 1
fi

nonewline_output=$(run_case nonewline)
printf '%s\n' "$nonewline_output" | grep '4.000' >/dev/null

long_output=$(run_case long)
printf '%s\n' "$long_output" | grep '5.000' >/dev/null

make_fake_ping fail
if CPING_TEST_PING_PATH="$WORK/ping" "$CPING_BIN" -c 1 127.0.0.1 >/dev/null 2>&1; then
    echo "test_integration: child failure unexpectedly succeeded" >&2
    exit 1
fi

echo "test_integration: ok"
