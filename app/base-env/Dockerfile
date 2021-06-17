# I'd much rather use a newer compiler with better tooling than an old version
# We'll implement libc version sanitization in the code itself rather than
# just building on an old, crusty distro
FROM ubuntu:20.04 as base

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  awscli \
  binutils-dev \
  clang-11 \
  cmake \
  gcc-10 \
  gcc \
  g++ \
  git \
  curl \
  make \
  m4 \
  libprotobuf-c-dev \
  liblzma-dev \
  pkg-config \
  ssh-client \
  zlib1g-dev

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-11/bin/:$PATH"
