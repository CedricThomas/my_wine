# Build container for cross-compiling samples to PE (32-bit and 64-bit Windows)
#
# Usage:
#   DOCKER_BUILDKIT=0 docker build -t my_wine-samples .
#

FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-mingw-w64-x86-64 \
        binutils-mingw-w64-x86-64 \
        gcc-mingw-w64-i686 \
        binutils-mingw-w64-i686 && \
    rm -rf /var/lib/apt/lists/*
