# I'd much rather use a newer compiler with better tooling than an old version
# We'll implement libc version sanitization in the code itself rather than
# just building on an old, crusty distro
FROM ubuntu:20.04 as base

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
    make \
    clang-11 \
    gcc-10 \
    git \
    ssh-client \
    curl \
    m4 \
    awscli \
    binutils-dev \
    libprotobuf-c-dev \
    liblzma-dev \
    zlib1g-dev \
    cmake

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-11/bin/:$PATH"
