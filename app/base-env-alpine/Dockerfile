FROM alpine:3.18
ARG COMPILER="gcc"
ENV CC=gcc
ENV CXX=g++

#MISSING
# lcov
# libexecinfo-dev # No static lib
RUN apk update \
    && apk add --no-cache aws-cli \
   bash \
   binutils-dev \
   bison \
   ca-certificates \
   gcovr \
   gdb \
   git \
   flex \
   subversion \
   patch \
   curl \
   wget \
   make \
   cmake \
   m4 \
   autoconf \
   automake \
   unzip \
   gcc \
   g++ \
   clang \
   libcap-static \
   libunwind-dev \
   ninja-build \
   musl-dbg \
   py3-pkgconfig \
   gtest-dev \
   cppcheck \
   openssh \
   zlib-dev \
   zlib-static \
   bzip2-dev \
   bzip2-static \
   xz-dev \
   xz-static \   
   zstd-dev \
   zstd-static \   
   argp-standalone \
   musl-fts-dev \
   musl-obstack-dev \
   musl-libintl \
   musl-legacy-error \
   libcap-dev \
   netcat-openbsd \
   util-linux

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

## Alpine 3.16 has gcc 12 and clang 16

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-${CLANG_VERSION}/bin/:$PATH"

## Alpine has cmake 3.26.5

## Alpine has ninja 1.11.1-r2 Ninja build

## Alpine has gtest 1.13.0-r1

## Alpine has cppcheck 2.10.3

# C++ json library (used for test purpose)
RUN VERSION="3.11.2" \
  && TAR_NAME="json.tar.xz" \
  && curl -fsSLO "https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz" \
  && SHA256="8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f" \
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum -c) \
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
  && (printf "${SHA256} ${TAR_NAME}" | sha256sum -c) \
  && tar xf "${TAR_NAME}" \
  && pushd benchmark-${VERSION} \
  && cmake -GNinja -Bbuild -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_USE_BUNDLED_GTEST=OFF \
  && cmake --build build -t install \
  && popd \
  && rm -rf "benchmark-${VERSION}" "${TAR_NAME}"

# Patch to fix compilation of elfutils
RUN mkdir /patch
ADD ./app/base-env-alpine/*.patch /patch/
ADD ./app/base-env-alpine/error.h /patch/
ADD ./app/base-env-alpine/libintl.h /patch/

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
