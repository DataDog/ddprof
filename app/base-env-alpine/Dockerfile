FROM alpine:3.16.0
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
   py3-pkgconfig \
   gtest-dev \
   cppcheck \
   openssh \
   zlib-dev \
   zlib-static \
   bzip2-dev \
   xz-dev \
   argp-standalone \
   fts-dev \
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

### Alpine has cmake 3.23

# Ninja build 1.10.2
RUN VERSION="1.10.2" \
  && SHA256="ce35865411f0490368a8fc383f29071de6690cbadc27704734978221f25e2bed" \
  && curl -LO https://github.com/ninja-build/ninja/archive/refs/tags/v${VERSION}.tar.gz \
  && (printf "${SHA256}  v${VERSION}.tar.gz" | sha256sum -c) \
  && tar xvfz v${VERSION}.tar.gz \
  && cd ninja-${VERSION} \
  && cmake -Bbuild-cmake \
  && cmake --build build-cmake  -j $(nproc) -t install

## Alpine has gtest 1.12

## Alpine has cppcheck 2.8.1-r0

# C++ json library (used for test purpose)
RUN VERSION="3.10.5" \
  && curl -LO https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz \
  && SHA256="344be97b757a36c5b180f1c8162f6c5f6ebd760b117f6e64b77866e97b217280" \
  && (printf "${SHA256}  json.tar.xz" | sha256sum -c) \
  && tar xf json.tar.xz \
  && cd json \
  && mkdir build && cd build \
  && cmake -GNinja -DJSON_BuildTests=Off ../ \
  && cmake --build . -t install \
  && cd ../ \
  && rm -rf json json.tar.xz

# Google benchmark
RUN VERSION="1.6.1" \
  && curl -LO "https://github.com/google/benchmark/archive/refs/tags/v${VERSION}.tar.gz" \
  && SHA256="6132883bc8c9b0df5375b16ab520fac1a85dc9e4cf5be59480448ece74b278d4" \
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
