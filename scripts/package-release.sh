#!/bin/sh
set -eu

if [ "$#" -ne 5 ]; then
    echo "usage: scripts/package-release.sh <version> <platform> <architecture> <binary-path> <output-dir>" >&2
    exit 2
fi

VERSION=$1
PLATFORM=$2
ARCHITECTURE=$3
BINARY_PATH=$4
OUTPUT_DIR=$5

case "$VERSION:$PLATFORM:$ARCHITECTURE:$BINARY_PATH:$OUTPUT_DIR" in
    *::*)
        echo "package-release: arguments must not be empty" >&2
        exit 2
        ;;
esac

case "$VERSION" in
    v[0-9]*.[0-9]*.[0-9]*)
        ;;
    *)
        echo "package-release: version must look like vX.Y.Z" >&2
        exit 2
        ;;
esac

if [ ! -x "$BINARY_PATH" ]; then
    echo "package-release: executable not found: $BINARY_PATH" >&2
    exit 1
fi
if [ ! -f README.md ]; then
    echo "package-release: README.md not found" >&2
    exit 1
fi
if [ ! -f LICENSE ]; then
    echo "package-release: LICENSE not found" >&2
    exit 1
fi

mkdir -p "$OUTPUT_DIR"
STAGING=$(mktemp -d "${TMPDIR:-/tmp}/cping-package.XXXXXX")
trap 'rm -rf "$STAGING"' EXIT INT TERM

cp "$BINARY_PATH" "$STAGING/cping"
cp README.md "$STAGING/README.md"
cp LICENSE "$STAGING/LICENSE"
CHANGELOG_ARG=
if [ -f CHANGELOG.md ]; then
    cp CHANGELOG.md "$STAGING/CHANGELOG.md"
    CHANGELOG_ARG=CHANGELOG.md
fi
chmod 755 "$STAGING/cping"

ARCHIVE="$OUTPUT_DIR/cping-$VERSION-$PLATFORM-$ARCHITECTURE.tar.gz"
tar -C "$STAGING" -czf "$ARCHIVE" cping README.md LICENSE $CHANGELOG_ARG
printf '%s\n' "$ARCHIVE"
