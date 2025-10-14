FROM randomdude/gcc-cross-x86_64-elf

SHELL ["/bin/bash", "-lc"]

# Install build tools: nasm, grub utils, ISO tooling, and basics
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      nasm xorriso mtools \
      grub-pc-bin grub-efi-amd64-bin grub-common \
      make ca-certificates git curl coreutils tar && \
    rm -rf /var/lib/apt/lists/*

# Install Zig from official tarball
ARG ZIG_VERSION=0.13.0
RUN set -euxo pipefail; \
    cd /tmp; \
    curl -fsSL -o zig.tar.xz https://ziglang.org/download/${ZIG_VERSION}/zig-linux-x86_64-${ZIG_VERSION}.tar.xz || \
      curl -fsSL -o zig.tar.xz https://ziglang.org/builds/zig-linux-x86_64-${ZIG_VERSION}.tar.xz; \
    mkdir -p /opt; \
    tar -C /opt -xf zig.tar.xz; \
    ln -sfn /opt/zig-linux-x86_64-${ZIG_VERSION} /opt/zig
ENV PATH=/opt/zig:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

WORKDIR /work
