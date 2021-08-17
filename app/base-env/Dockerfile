# I'd much rather use a newer compiler with better tooling than an old version
# We'll implement libc version sanitization in the code itself rather than
# just building on an old, crusty distro
FROM ubuntu:20.04 as base

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  awscli \
  binutils-dev \
  clang-12 \
  cmake \
  cppcheck \
  gcc-11 \
  g++-11 \
  git \
  curl \
  make \
  m4 \
  libprotobuf-c-dev \
  liblzma-dev \
  pkg-config \
  ssh-client \
  subversion \
  zlib1g-dev \
  libgtest-dev \
  libgmock-dev \
  libjemalloc-dev \
  libcap-dev \
  gcovr

# Set the default gcc/g++/clang to the specified version
RUN update-alternatives --install /usr/bin/gcc gcc $(which gcc-11) 11 &&
    update-alternatives --install /usr/bin/g++ g++ $(which g++-11) 11 &&
    update-alternatives --install /usr/bin/clang clang $(which clang-12) 12

# Newer CMake
FROM base as cmake
RUN CMAKE_VERSION="3.20.1" \
  CMAKE_SHA256="b8c141bd7a6d335600ab0a8a35e75af79f95b837f736456b5532f4d717f20a09" \
  && curl -L --remote-name-all https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
  && (printf "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz" | sha256sum --check --strict --status) \
  && tar --no-same-owner -C /usr/local --strip-components=1 -xf cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
  && rm cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-11/bin/:$PATH"

# A specific user is required to get access to perf event ressources. 
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
