# Using a recent compiler version and recent OS (better tooling)
# We'll implement libc version sanitization in the code itself
ARG UBUNTU_VERSION=20
ARG COMPILER="gcc"

FROM ubuntu:${UBUNTU_VERSION}.04 as base
ARG UBUNTU_VERSION
ENV OS_IDENTIFIER="UB${UBUNTU_VERSION}"

FROM base AS base-20
ENV GCC_VERSION=11
ENV CLANG_VERSION=13

FROM base AS base-18
ENV GCC_VERSION=11
ENV CLANG_VERSION=13

FROM base AS base-16
ENV GCC_VERSION=9
ENV CLANG_VERSION=12

FROM base-${UBUNTU_VERSION} AS base-gcc
ENV CC=gcc-${GCC_VERSION}
ENV CXX=g++-${GCC_VERSION}

FROM base-${UBUNTU_VERSION} AS base-clang
ENV CC=clang-${CLANG_VERSION}
ENV CXX=clang++-${CLANG_VERSION}

FROM base-${COMPILER} AS dependencies

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  software-properties-common \
  apt-transport-https \
  awscli \
  binutils-dev \
  ca-certificates \
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
  unzip \
  wget

# Codeql : static analysis tooling
RUN curl -L https://github.com/github/codeql-action/releases/download/codeql-bundle-20210622/codeql-bundle-linux64.tar.gz -o - | tar -xz -C /usr/local

####################
## LLVM/GCC SETUP ##
####################
ADD ./app/base-env/llvm.sh ./app/base-env/gcc.sh /
RUN /llvm.sh ${CLANG_VERSION} && /gcc.sh ${GCC_VERSION}

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-${CLANG_VERSION}/bin/:$PATH"

# Newer CMake
FROM dependencies as cmake
RUN VERSION="3.22.2" \
  && MARCH=$(uname -m) \
  && SHA256_ARM="02b2b36afc932ede2d77ba53456785bae85538d5d4600f87220072b95d926c5b" \
  && SHA256_X86="38b3befdee8fd2bac06954e2a77cb3072e6833c69d8cc013c0a3b26f1cfdfe37" \
  && if [ "$MARCH" = aarch64 ]; then SHA256=$SHA256_ARM; else SHA256=$SHA256_X86; fi \
  && TAR_NAME="cmake-${VERSION}-Linux-${MARCH}.tar.gz" \
  && curl -LO https://github.com/Kitware/CMake/releases/download/v${VERSION}/${TAR_NAME} \
  && (printf "${SHA256}  ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar --no-same-owner -C /usr/local --strip-components=1 -xf ${TAR_NAME} \
  && rm ${TAR_NAME}

FROM cmake as final
# Ninja build 1.10.2
RUN VERSION="1.10.2" \
  && SHA256="763464859c7ef2ea3a0a10f4df40d2025d3bb9438fcb1228404640410c0ec22d" \
  && curl -LO https://github.com/ninja-build/ninja/releases/download/v${VERSION}/ninja-linux.zip \
  && (printf "${SHA256} ninja-linux.zip" | sha256sum --check --strict --status) \
  && unzip -d /usr/local/bin ninja-linux.zip \
  && rm ninja-linux.zip

# google test / google mock
RUN VERSION="1.11.0" \
  && curl -LO https://github.com/google/googletest/archive/refs/tags/release-${VERSION}.tar.gz \
  && SHA256="b4870bf121ff7795ba20d20bcdd8627b8e088f2d1dab299a031c1034eddc93d5" \
  && (printf "${SHA256} release-${VERSION}.tar.gz" | sha256sum --check --strict --status) \
  && tar xf release-${VERSION}.tar.gz \
  && pushd googletest-release-${VERSION} \
  && mkdir build \
  && cd build \
  && cmake -GNinja ../ \
  && cmake --build . -t install \
  && popd \
  && rm -rf googletest-release-${VERSION} release-${VERSION}.tar.gz

# Cpp check 2.7 (ubuntu's 1.8 has some bugs on double free)
RUN VERSION="2.7" \
  && curl -LO https://github.com/danmar/cppcheck/archive/refs/tags/${VERSION}.tar.gz \
  && SHA256="5fd20549bb2fabf9a8026f772779d8cc6a5782c8f17500408529f7747afbc526" \
  && (printf "${SHA256} ${VERSION}.tar.gz" | sha256sum --check --strict --status) \
  && tar xf ${VERSION}.tar.gz \
  && pushd cppcheck-${VERSION} \
  && mkdir build \
  && cd build \
  && cmake -GNinja ../ \
  && cmake --build . -t install  \
  && popd \
  && rm -rf ${VERSION}.tar.gz cppcheck-${VERSION}

# C++ json library (used for test purpose)
RUN VERSION="3.10.5" \
  && curl -LO https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz \
  && SHA256="344be97b757a36c5b180f1c8162f6c5f6ebd760b117f6e64b77866e97b217280" \
  && (printf "${SHA256} json.tar.xz" | sha256sum --check --strict --status) \
  && tar xf json.tar.xz \
  && pushd json \
  && mkdir build && cd build \
  && cmake -GNinja -DJSON_BuildTests=Off ../ \
  && cmake --build . -t install \
  && popd \
  && rm -rf json json.tar.xz 

# A specific user is required to get access to perf event ressources. 
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
