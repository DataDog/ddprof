# Using a recent compiler version and recent OS (better tooling)
# We'll implement libc version sanitization in the code itself
ARG UBUNTU_VERSION=22
ARG COMPILER="gcc"

FROM ubuntu:${UBUNTU_VERSION}.04 as base
ARG UBUNTU_VERSION
ENV OS_IDENTIFIER="UB${UBUNTU_VERSION}"

FROM base AS base-24
ENV GCC_VERSION=14
ENV CLANG_VERSION=18

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
  awscli \
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

# Codeql : static analysis tooling
RUN curl -L https://github.com/github/codeql-action/releases/download/codeql-bundle-20230304/codeql-bundle-linux64.tar.gz -o - | tar -xz -C /usr/local

####################
## LLVM/GCC SETUP ##
####################
ADD ./app/base-env/llvm.sh ./app/base-env/gcc.sh /
RUN /llvm.sh ${CLANG_VERSION} all
RUN /gcc.sh ${GCC_VERSION}

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-${CLANG_VERSION}/bin/:$PATH"

# Newer CMake
RUN VERSION="3.27.5" \
  && MARCH=$(uname -m) \
  && SHA256_ARM="2ffaf176d0f93c332abaffbf3ce82fc8c90e49e0fcee8dc16338bcfbb150ead7" \
  && SHA256_X86="138c68addae825b16ed78d792dafef5e0960194833f48bd77e7e0429c6bc081c" \
  && if [ "$MARCH" = aarch64 ]; then SHA256=$SHA256_ARM; else SHA256=$SHA256_X86; fi \
  && TAR_NAME="cmake-${VERSION}-Linux-${MARCH}.tar.gz" \
  && curl -fsSLO "https://github.com/Kitware/CMake/releases/download/v${VERSION}/${TAR_NAME}" \
  && (printf "${SHA256}  ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar --no-same-owner -C /usr/local --strip-components=1 -xf "${TAR_NAME}" \
  && rm "${TAR_NAME}"

# Ninja build
RUN VERSION="1.11.1" \
  && SHA256="31747ae633213f1eda3842686f83c2aa1412e0f5691d1c14dbbcc67fe7400cea" \
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
RUN VERSION="1.14.0" \
  && TAR_NAME="v${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/google/googletest/archive/refs/tags/${TAR_NAME}" \
  && SHA256="8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd "googletest-${VERSION}" \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -t install \
  && popd \
  && rm -rf "googletest-${VERSION}" "${TAR_NAME}"

# More recent Cppcheck (ubuntu defaults to a 1.8 version)
RUN VERSION="2.12.1" \
  && TAR_NAME="${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/danmar/cppcheck/archive/refs/tags/${TAR_NAME}" \
  && SHA256="2a3d4ba1179419612183ab3d6aed6d3b18be75e98cd6f138ea8e2020905dced2" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd "cppcheck-${VERSION}" \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release \
  && cmake --build build -t install \
  && popd \
  && rm -rf "cppcheck-${VERSION}" "${TAR_NAME}"

# C++ json library (used for test purpose)
RUN VERSION="3.11.2" \
  && TAR_NAME="json.tar.xz" \
  && curl -fsSLO "https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz" \
  && SHA256="8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd json \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release -DJSON_BuildTests=Off \
  && cmake --build build -t install \
  && popd \
  && rm -rf json "${TAR_NAME}"

# Google benchmark
RUN VERSION="1.8.3" \
  && TAR_NAME="v${VERSION}.tar.gz" \
  && curl -fsSLO "https://github.com/google/benchmark/archive/refs/tags/v${VERSION}.tar.gz" \
  && SHA256="6bc180a57d23d4d9515519f92b0c83d61b05b5bab188961f36ac7b06b0d9e9ce" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum --check --strict --status) \
  && tar xf "${TAR_NAME}" \
  && pushd benchmark-${VERSION} \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_USE_BUNDLED_GTEST=OFF \
  && cmake --build build -t install \
  && popd \
  && rm -rf "benchmark-${VERSION}" "${TAR_NAME}"

# Valgrind with dwarf 5 support
RUN VERSION="3.21.0" \
    && TAR_NAME="valgrind-${VERSION}.tar.bz2" \
    && curl -fsSLO "https://sourceware.org/pub/valgrind/${TAR_NAME}" \
    && SHA1="817d769743d278b5d07526e85115082054e9bf9c" \
    && (printf "${SHA1} ${TAR_NAME}" | sha1sum --check --strict --status) \
    && tar xvf "${TAR_NAME}" \
    && pushd "valgrind-${VERSION}" \
    && ./configure --prefix /usr/local \
    && make -j $(nproc) \
    && make install \
    && valgrind --version \
    && popd \
    && rm -rf "valgrind-${VERSION}" "${TAR_NAME}"

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

# Install cmake_format and sarif-tools (for codeql filtering)
RUN pip3 install cmake_format && ([ "${UBUNTU_VERSION}" -ge 22 ] && pip3 install sarif-tools || true)

# A specific user is required to get access to perf event ressources.
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
