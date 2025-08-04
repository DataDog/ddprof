# Using a recent compiler version and recent OS (better tooling)
# We'll implement libc version sanitization in the code itself
ARG UBUNTU_VERSION=22
ARG COMPILER="gcc"
ARG INSTALL_VALGRIND="true"
ARG VALGRIND_VERSION="3.21.0"
ARG VALGRIND_SHA1="817d769743d278b5d07526e85115082054e9bf9c"

FROM ubuntu:${UBUNTU_VERSION}.04 as base
ARG UBUNTU_VERSION
ENV OS_IDENTIFIER="UB${UBUNTU_VERSION}"

FROM base AS base-24
ENV GCC_VERSION=14
ENV CLANG_VERSION=20

FROM base AS base-22
ENV GCC_VERSION=13
ENV CLANG_VERSION=17

FROM base AS base-20
ENV GCC_VERSION=11
ENV CLANG_VERSION=17

FROM base AS base-18
ENV GCC_VERSION=11
ENV CLANG_VERSION=17

FROM base AS base-16
ENV GCC_VERSION=9
ENV CLANG_VERSION=12

FROM base-${UBUNTU_VERSION} AS base-gcc
ENV CC=gcc-${GCC_VERSION}
ENV CXX=g++-${GCC_VERSION}

FROM base-${UBUNTU_VERSION} AS base-clang
ENV CC=clang-${CLANG_VERSION}
ENV CXX=clang++-${CLANG_VERSION}

FROM base-${COMPILER} AS final

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

# Fix issues when uid/gid are stored in tarball and tar tries to preserve them because it thinks its running as root
# (https://github.com/habitat-sh/builder/issues/365#issuecomment-382862233)
ENV TAR_OPTIONS="--no-same-owner"

RUN apt-get update \
  && DEBIAN_FRONTEND=noninteractive apt-get install -y \
  apt-transport-https \
  binutils-dev \
  bison \
  ca-certificates \
  curl \
  flex \
  gcovr \
  gdb \
  git \
  jq \
  lcov \
  libbz2-dev \
  libcap-dev \
  liblzma-dev \
  libzstd-dev \
  libunwind-dev \
  m4 \
  make \
  netcat-openbsd \
  pkg-config \
  python3-pip \
  software-properties-common \
  ssh-client \
  subversion \
  unzip \
  wget \
  zlib1g-dev

# Download and install AWS CLI v2
# Set up environment variables for architecture
RUN ARCH=$(uname -m) && \
    if [ "$ARCH" = "x86_64" ]; then \
        AWS_CLI_URL="https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip"; \
    elif [ "$ARCH" = "aarch64" ]; then \
        AWS_CLI_URL="https://awscli.amazonaws.com/awscli-exe-linux-aarch64.zip"; \
    else \
        echo "Unsupported architecture: $ARCH"; exit 1; \
    fi && \
    curl "$AWS_CLI_URL" -o "awscliv2.zip" && \
    unzip awscliv2.zip && \
    ./aws/install && \
    rm -rf awscliv2.zip aws

# Verify installation
RUN aws --version

# Codeql : static analysis tooling
RUN curl -L https://github.com/github/codeql-action/releases/download/codeql-bundle-20230304/codeql-bundle-linux64.tar.gz -o - | tar -xz -C /usr/local

####################
## LLVM/GCC SETUP ##
####################
ADD ./app/base-env/llvm.sh ./app/base-env/gcc.sh /
ADD ./tools /app/tools
RUN /llvm.sh ${CLANG_VERSION} all
RUN /gcc.sh ${GCC_VERSION}

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-${CLANG_VERSION}/bin/:$PATH"

# Newer CMake
RUN VERSION="4.0.3" \
  && MARCH=$(uname -m) \
  && SHA256_ARM="391da1544ef50ac31300841caaf11db4de3976cdc4468643272e44b3f4644713" \
  && SHA256_X86="585ae9e013107bc8e7c7c9ce872cbdcbdff569e675b07ef57aacfb88c886faac" \
  && if [ "$MARCH" = aarch64 ]; then SHA256=$SHA256_ARM; else SHA256=$SHA256_X86; fi \
  && TAR_NAME="cmake-${VERSION}-Linux-${MARCH}.tar.gz" \
  && curl -fsSLO "https://github.com/Kitware/CMake/releases/download/v${VERSION}/${TAR_NAME}" \
  && (printf "${SHA256}  ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar --no-same-owner -C /usr/local --strip-components=1 -xf "${TAR_NAME}" \
  && rm "${TAR_NAME}"

# Ninja build
RUN VERSION="1.13.1" \
  && SHA256="f0055ad0369bf2e372955ba55128d000cfcc21777057806015b45e4accbebf23" \
  && TAR_NAME="v${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/ninja-build/ninja/archive/refs/tags/${TAR_NAME}" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd "ninja-${VERSION}" \
  && cmake -Bbuild -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -j $(nproc) -t install \
  && popd \
  && rm -rf "ninja-${VERSION}" "${TAR_NAME}"

# google test / google mock
RUN VERSION="1.17.0" \
  && TAR_NAME="v${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/google/googletest/archive/refs/tags/${TAR_NAME}" \
  && SHA256="65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd "googletest-${VERSION}" \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -t install \
  && popd \
  && rm -rf "googletest-${VERSION}" "${TAR_NAME}"

# More recent Cppcheck (ubuntu defaults to a 1.8 version)
RUN VERSION="2.18.0" \
  && TAR_NAME="${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/danmar/cppcheck/archive/refs/tags/${TAR_NAME}" \
  && SHA256="dc74e300ac59f2ef9f9c05c21d48ae4c8dd1ce17f08914dd30c738ff482e748f" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd "cppcheck-${VERSION}" \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -t install \
  && popd \
  && rm -rf "cppcheck-${VERSION}" "${TAR_NAME}"

# jemalloc
RUN VERSION="5.3.0" \
  && TAR_NAME="jemalloc-${VERSION}.tar.bz2" \
  && curl -fsSLO "https://github.com/jemalloc/jemalloc/releases/download/${VERSION}/jemalloc-${VERSION}.tar.bz2" \
  && SHA256="2db82d1e7119df3e71b7640219b6dfe84789bc0537983c3b7ac4f7189aecfeaa" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum -c) \
  && tar xf "${TAR_NAME}" \
  && pushd "jemalloc-${VERSION}" \
  && ./configure --enable-experimental-smallocx  --disable-doc \
  && make -j$(nproc) \
  && make install \
  && popd \
  && rm -rf "jemalloc-${VERSION}" "${TAR_NAME}"

# C++ json library (used for test purpose)
RUN VERSION="3.12.0" \
  && TAR_NAME="json.tar.xz" \
  && curl -fsSLO "https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz" \
  && SHA256="42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd json \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release -DJSON_BuildTests=Off \
  && cmake --build build -t install \
  && popd \
  && rm -rf json "${TAR_NAME}"

# Google benchmark
RUN VERSION="1.9.4" \
  && TAR_NAME="v${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/google/benchmark/archive/refs/tags/v${VERSION}.tar.gz" \
  && SHA256="b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd benchmark-${VERSION} \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_USE_BUNDLED_GTEST=OFF \
  && cmake --build build -t install \
  && popd \
  && rm -rf "benchmark-${VERSION}" "${TAR_NAME}"

# Install sarif-tools only if UBUNTU_VERSION is 22 or greater
RUN if [ "${UBUNTU_VERSION}" -ge 22 ]; then \
        pip3 install --break-system-packages cmakelang sarif-tools; \
    else \
        pip3 install cmake-format; \
    fi

# A specific user is required to get access to perf event ressources.
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
