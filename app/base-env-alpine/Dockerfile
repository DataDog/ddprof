FROM alpine:3.18.0
ARG COMPILER="gcc"
ENV CC=gcc
ENV CXX=g++

#MISSING
# lcov
# liblzma
# libexecinfo-dev # No static lib
RUN apk update \
    && apk add --no-cache aws-cli \
   bash \
   binutils-dev \
   bison \
   ca-certificates \
   gcovr \
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
   musl-dbg \
   py3-pkgconfig \
   gtest-dev \
   cppcheck \
   openssh \
   zlib-dev \
   zlib-static \
   bzip2-dev \
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

## Alpine 3.16 has gcc 11 and clang 14

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-${CLANG_VERSION}/bin/:$PATH"

### Alpine has newer cmake

# Ninja build 1.11.1
RUN VERSION="1.11.1" \
  && SHA256="31747ae633213f1eda3842686f83c2aa1412e0f5691d1c14dbbcc67fe7400cea" \
  && curl -LO https://github.com/ninja-build/ninja/archive/refs/tags/v${VERSION}.tar.gz \
  && (printf "${SHA256}  v${VERSION}.tar.gz" | sha256sum -c) \
  && tar xvfz v${VERSION}.tar.gz \
  && cd ninja-${VERSION} \
  && cmake -Bbuild-cmake \
  && cmake --build build-cmake  -j $(nproc) -t install

## Alpine has gtest 1.12

## Alpine has cppcheck 2.8.1-r0

# C++ json library (used for test purpose)
RUN VERSION="3.11.2" \
  && curl -LO https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz \
  && SHA256="8c4b26bf4b422252e13f332bc5e388ec0ab5c3443d24399acb675e68278d341f" \
  && (printf "${SHA256}  json.tar.xz" | sha256sum -c) \
  && tar xf json.tar.xz \
  && cd json \
  && mkdir build && cd build \
  && cmake -GNinja -DJSON_BuildTests=Off ../ \
  && cmake --build . -t install \
  && cd ../ \
  && rm -rf json json.tar.xz

# Google benchmark
RUN VERSION="1.8.2" \
  && curl -LO "https://github.com/google/benchmark/archive/refs/tags/v${VERSION}.tar.gz" \
  && SHA256="2aab2980d0376137f969d92848fbb68216abb07633034534fc8c65cc4e7a0e93" \
  && (printf "${SHA256}  v${VERSION}.tar.gz" | sha256sum -c) \
  && tar xf v${VERSION}.tar.gz \
  && cd benchmark-${VERSION} \
  # python is missing on ubuntu 20.04, use python 3 instead
  && { command -v python > /dev/null || { ln -s /usr/bin/python3 python && export PATH=$PWD:$PATH; }; } \
  && cmake -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_USE_BUNDLED_GTEST=OFF -GNinja -S . -B "build" \
  && cmake --build "build" --target install \
  && cd ../ \
  && rm -rf benchmark-${VERSION} v${VERSION}.tar.gz

# Patch to fix compilation of elfutils
RUN mkdir /patch
ADD ./app/base-env-alpine/*.patch /patch/
ADD ./app/base-env-alpine/error.h /patch/
ADD ./app/base-env-alpine/libintl.h /patch/

# jemalloc
RUN git clone --branch 5.3.0 https://github.com/jemalloc/jemalloc.git && \
    cd jemalloc && \
    ./autogen.sh && \
    make && \
    make install

# lzma
# The static version of lzma is no longer available in package xz-dev
RUN git clone --branch v5.4 https://github.com/tukaani-project/xz.git && \
    cd xz && \
    mkdir Build && \
    cd Build && \
    cmake -DCMAKE_BUILD_TYPE=Release ../ && \
    make -j && \
    make install
