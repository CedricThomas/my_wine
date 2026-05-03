# Build container for cross-compiling samples to PE (x86_64 Windows)
#
# Usage:
#   DOCKER_BUILDKIT=0 docker build -t my_wine-samples .
#

FROM ubuntu:22.04

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-mingw-w64-x86-64 \
        binutils-mingw-w64-x86-64 && \
    rm -rf /var/lib/apt/lists/*
