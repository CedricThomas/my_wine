# Build container for cross-compiling samples to PE (32-bit and 64-bit Windows)
#
# Usage:
#   DOCKER_BUILDKIT=0 docker build -t my_wine-samples .
#

FROM ubuntu:22.04

RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        gcc-mingw-w64-x86-64 \
        binutils-mingw-w64-x86-64 \
        gcc-mingw-w64-i686 \
        binutils-mingw-w64-i686 \
        ca-certificates \
        libc6:i386 \
        libgcc-s1:i386 \
        libstdc++6:i386 \
        libsdl2-2.0-0 \
        libsdl2-2.0-0:i386 \
        procps \
        x11-apps \
        x11-utils \
        xauth \
        xdotool \
        xvfb && \
    rm -rf /var/lib/apt/lists/*
