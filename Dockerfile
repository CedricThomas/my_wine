# Build container for cross-compiling samples to PE (32-bit and 64-bit Windows)
#
# Usage:
#   DOCKER_BUILDKIT=0 docker build -t my_wine-samples .
#

FROM ubuntu:24.04

RUN dpkg --add-architecture i386 && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        build-essential \
        gcc-multilib \
        make \
        gcc-mingw-w64-x86-64 \
        binutils-mingw-w64-x86-64 \
        gcc-mingw-w64-i686 \
        binutils-mingw-w64-i686 \
        ca-certificates \
        libc6:i386 \
        libgcc-s1:i386 \
        libstdc++6:i386 \
        libsdl2-dev \
        libsdl2-dev:i386 \
        libsdl2-2.0-0 \
        libsdl2-2.0-0:i386 \
        libfluidsynth3 \
        libfluidsynth3:i386 \
        gdb \
        gdb-multiarch \
        imagemagick \
        procps \
        wine64 \
        wine32 \
        openbox \
        wmctrl \
        x11-apps \
        x11-utils \
        xauth \
        xdotool \
        xvfb && \
    rm -rf /var/lib/apt/lists/*

# Create symlinks so host-compiled binaries can find libraries in multiarch paths
RUN ln -sf /usr/lib/x86_64-linux-gnu/libSDL2-2.0.so.0 /usr/lib/libSDL2-2.0.so.0 && \
    ln -sf /usr/lib/x86_64-linux-gnu/libfluidsynth.so.3 /usr/lib/libfluidsynth.so.3 && \
    ln -sf /usr/lib/x86_64-linux-gnu/libfluidsynth.so.3 /lib/libfluidsynth.so.3 && \
    ln -sf /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 /usr/lib/libgcc_s.so.1 && \
    ln -sf /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/libstdc++.so.6 && \
    ln -sf /usr/lib/x86_64-linux-gnu/libm.so.6 /usr/lib/libm.so.6 && \
    ldconfig
