#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DOCS="$ROOT/docs"
CAST="$DOCS/demo.cast"
SVG="$DOCS/demo.svg"
GIF="$DOCS/demo.gif"
TMP_CAST="$DOCS/demo.tmp.cast"

FORCE=0
if [ "${1:-}" = "--force" ]; then
    FORCE=1
elif [ "${1:-}" != "" ]; then
    echo "usage: scripts/record-demo.sh [--force]" >&2
    exit 2
fi

if [ "$FORCE" -ne 1 ] && { [ -e "$CAST" ] || [ -e "$SVG" ] || [ -e "$GIF" ]; }; then
    echo "demo assets already exist; rerun with --force to overwrite" >&2
    exit 1
fi

if ! command -v asciinema >/dev/null 2>&1; then
    echo "asciinema is required to record docs/demo.cast" >&2
    echo "install asciinema, then rerun: scripts/record-demo.sh --force" >&2
    exit 1
fi

if command -v agg >/dev/null 2>&1; then
    RENDERER=agg
elif command -v svg-term >/dev/null 2>&1; then
    RENDERER=svg-term
elif command -v termtosvg >/dev/null 2>&1; then
    RENDERER=termtosvg
elif command -v vhs >/dev/null 2>&1; then
    RENDERER=vhs
else
    echo "no supported renderer found: install agg, svg-term, termtosvg, or vhs" >&2
    echo "docs/demo.cast can still be recorded with asciinema; rendering is required for README preview" >&2
    exit 1
fi

mkdir -p "$DOCS"
cd "$ROOT"
make
rm -f "$TMP_CAST"

echo "Recording: cping google.com"
echo "Use Ctrl+C after approximately 8-12 successful probes."
PATH="$ROOT:$PATH" LC_ALL=C LANG=C.UTF-8 asciinema rec \
    --overwrite \
    --cols 90 \
    --rows 20 \
    --command "sh -c 'printf \"\\$ cping google.com\\n\"; exec cping google.com'" \
    "$TMP_CAST"
mv "$TMP_CAST" "$CAST"

case "$RENDERER" in
    agg)
        echo "Rendering command: agg '$CAST' '$GIF'"
        agg "$CAST" "$GIF"
        ;;
    svg-term)
        echo "Rendering command: svg-term --in '$CAST' --out '$SVG'"
        svg-term --in "$CAST" --out "$SVG"
        ;;
    termtosvg)
        echo "Rendering command: termtosvg render '$CAST' '$SVG'"
        termtosvg render "$CAST" "$SVG"
        ;;
    vhs)
        echo "vhs renderer detected, but this helper records asciinema casts; use agg, svg-term, or termtosvg for direct cast rendering." >&2
        exit 1
        ;;
esac
