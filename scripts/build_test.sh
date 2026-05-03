#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
HELLO_C="${SCRIPT_DIR}/hello.c"
HELLO_EXE="${SCRIPT_DIR}/hello.exe"

if [ ! -f "$HELLO_C" ]; then
    echo "ERROR: hello.c not found at $HELLO_C" >&2
    exit 1
fi

echo "Building hello.exe with mingw-w64 via Docker..."

docker run --rm \
    -v "${SCRIPT_DIR}:/work" \
    ubuntu:22.04 \
    bash -euo pipefail -c '
        apt-get update && \
        apt-get install -y --no-install-recommends gcc-mingw-w64-x86-64-posix && \
        x86_64-w64-mingw32-gcc /work/hello.c -o /work/hello.exe -lkernel32 && \
        rm -rf /var/lib/apt/lists/*
    '

if [ ! -f "$HELLO_EXE" ]; then
    echo "ERROR: Build failed — hello.exe not produced" >&2
    exit 1
fi

FILE_TYPE=$(file -b "${HELLO_EXE}")
if echo "$FILE_TYPE" | grep -qi "PE32"; then
    echo "SUCCESS: hello.exe is a PE32+ executable"
    file "$HELLO_EXE"
else
    echo "ERROR: hello.exe is not a PE32+ executable" >&2
    echo "Got: $FILE_TYPE" >&2
    exit 1
fi
