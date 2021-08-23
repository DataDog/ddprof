# Using a recent compiler version and recent OS (better tooling)
# We'll implement libc version sanitization in the code itself
FROM ubuntu:20.04 as base

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  software-properties-common

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  awscli \
  binutils-dev \
  gcc \
  g++ \
  cppcheck \
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
  gcovr \
  wget

# llvm installer
RUN bash -c "$(wget -O - https://apt.llvm.org/llvm.sh)"

# To install a specific version of LLVM:
RUN wget https://apt.llvm.org/llvm.sh && \
  chmod +x llvm.sh && \
  ./llvm.sh 12

# Clang format not installed by default
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key| apt-key add -
RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  clang-format-12

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-12/bin/:$PATH"

# Newer CMake
FROM base as cmake
RUN CMAKE_VERSION="3.20.1" \
  CMAKE_SHA256="b8c141bd7a6d335600ab0a8a35e75af79f95b837f736456b5532f4d717f20a09" \
  && curl -L --remote-name-all https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
  && (printf "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz" | sha256sum --check --strict --status) \
  && tar --no-same-owner -C /usr/local --strip-components=1 -xf cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
  && rm cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz


# A specific user is required to get access to perf event ressources. 
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
