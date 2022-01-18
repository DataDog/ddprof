# Using a recent compiler version and recent OS (better tooling)
# We'll implement libc version sanitization in the code itself
ARG UBUNTU_VERSION=20
FROM ubuntu:${UBUNTU_VERSION}.04 as base

ARG UBUNTU_VERSION
ENV OS_IDENTIFIER="UB${UBUNTU_VERSION}"
ARG CXX_COMPILER="clang++"
ARG C_COMPILER="clang"

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  software-properties-common

RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  awscli \
  binutils-dev \
  gcovr \
  git \
  curl \
  lcov \
  make \
  m4 \
  liblzma-dev \
  libunwind-dev \
  pkg-config \
  ssh-client \
  subversion \
  zlib1g-dev \
  libjemalloc-dev \
  libcap-dev \
  netcat-openbsd \
  wget

# Only necessary for ubuntu 16
RUN DEBIAN_FRONTEND=noninteractive apt install -y apt-transport-https ca-certificates

# Codeql : static analysis tooling
RUN curl -L https://github.com/github/codeql-action/releases/download/codeql-bundle-20210622/codeql-bundle-linux64.tar.gz -o - | tar -xz -C /usr/local

RUN if [[ "${CXX_COMPILER}" == "g++" ]] ; then apt install -y gcc g++ ; else echo "Skip install of g++" ; fi

################
## LLVM SETUP ##
################
ADD ./app/base-env/llvm.sh /
RUN /llvm.sh 12

# Clang format not installed by default
RUN wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key| apt-key add -
RUN apt update \
  && DEBIAN_FRONTEND=noninteractive apt install -y \
  clang-format-12

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-12/bin/:$PATH"

ENV CC=${C_COMPILER}
ENV CXX=${CXX_COMPILER}

# Newer CMake
FROM base as cmake
RUN CMAKE_VERSION="3.20.1" \
  && CMAKE_SHA256="b8c141bd7a6d335600ab0a8a35e75af79f95b837f736456b5532f4d717f20a09" \
  && curl -L --remote-name-all https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
  && (printf "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz" | sha256sum --check --strict --status) \
  && tar --no-same-owner -C /usr/local --strip-components=1 -xf cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
  && rm cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz

# google test / google mock
RUN git clone --depth 1 --branch release-1.11.0 https://github.com/google/googletest.git \
  && cd googletest \
  && mkdir build \
  && cd build \
  && cmake -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" -DCMAKE_CXX_FLAGS="-std=c++14" ../ \
  && cmake --build .  \
  && make install

# Cpp check 2.5 (ubuntu's 1.8 has some bugs on double free)
RUN wget https://github.com/danmar/cppcheck/archive/refs/tags/2.5.tar.gz \
  && CPPCHECK_SHA256="dc27154d799935c96903dcc46653c526c6f4148a6912b77d3a50cb35dabd82e1" \
  && (printf "${CPPCHECK_SHA256}  2.5.tar.gz" | sha256sum --check --strict --status) \
  && tar xvfz 2.5.tar.gz \
  && cd cppcheck-2.5 \
  && mkdir build \
  && cd build \
  && cmake ../ \
  && cmake --build .  \
  && make install

# C++ json library (used for test purpose)
RUN git clone --depth 1 --branch v3.10.2 https://github.com/nlohmann/json.git \
  && cd json \
  && mkdir build && cd build \
  && cmake -DJSON_BuildTests=Off -DCMAKE_CXX_COMPILER="${CXX_COMPILER}" -DCMAKE_CXX_FLAGS="-std=c++14" ../  \
  && make install

# A specific user is required to get access to perf event ressources. 
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
