# I'd much rather use a newer compiler with better tooling than an old version
# We'll implement libc version sanitization in the code itself rather than
# just building on an old, crusty distro
FROM ubuntu:20.04 as base

RUN apt update \
  && apt install -y \
    make \
    clang-11 \
    gcc-10 \
    git \
    curl \
    m4 \
    binutils-dev \
    libprotobuf-c-dev \
    liblzma-dev \
    zlib1g-dev
