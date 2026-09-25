# PrimeBox build image: soft-float ARM EABI5 cross toolchain + DirectFB build deps.
# Native on arm64 and amd64 hosts (no qemu), so -O2 builds work everywhere.
#   docker build -t primebox-armel:18.04 docker/
FROM ubuntu:18.04
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc-arm-linux-gnueabi libc6-dev-armel-cross binutils-arm-linux-gnueabi \
        make autoconf automake libtool pkg-config git ca-certificates patch file \
    && rm -rf /var/lib/apt/lists/* \
    && ln -sf /bin/sed /usr/bin/sed
