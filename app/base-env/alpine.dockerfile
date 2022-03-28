ARG COMPILER="gcc"
FROM alpine:edge as base

FROM base AS base-gcc
ENV CC=gcc
ENV CXX=g++

FROM base AS base-clang
ENV CC=clang
ENV CXX=clang++

FROM base-${COMPILER} AS final

RUN apk update \
  && apk add --no-cache \
    bash \
    build-base \
    bzip2-dev \
    bzip2-static \
    clang \
    clang-analyzer \
    clang-extra-tools \    
    cmake \
    curl \
    elfutils-dev \    
    git \
    libcap-dev \
    libcap-static \
    libelf-static \
    make \
    pkgconfig \
    unzip \
    zlib-dev \
    zlib-static \
    xz-dev

SHELL ["/bin/bash", "-c"]

# Ninja build 1.10.2
RUN VERSION="1.10.2" \
  && SHA256="ce35865411f0490368a8fc383f29071de6690cbadc27704734978221f25e2bed" \
  && curl -LO https://github.com/ninja-build/ninja/archive/refs/tags/v${VERSION}.tar.gz \
  && (printf "${SHA256}  v${VERSION}.tar.gz" | sha256sum -c) \
  && tar xvfz v${VERSION}.tar.gz \
  && cd ninja-${VERSION} \
  && cmake -Bbuild-cmake \
  && cmake --build build-cmake  -j $(nproc) -t install

# google test / google mock
RUN VERSION="1.11.0" \
  && curl -LO https://github.com/google/googletest/archive/refs/tags/release-${VERSION}.tar.gz \
  && SHA256="b4870bf121ff7795ba20d20bcdd8627b8e088f2d1dab299a031c1034eddc93d5" \
  && (printf "${SHA256}  release-${VERSION}.tar.gz" | sha256sum -c) \
  && tar xf release-${VERSION}.tar.gz \
  && pushd googletest-release-${VERSION} \
  && mkdir build \
  && cd build \
  && cmake -Bbuild -GNinja ../ \
  && cmake --build build -t install \
  && popd \
  && rm -rf googletest-release-${VERSION} release-${VERSION}.tar.gz

# Cpp check 2.7 (ubuntu's 1.8 has some bugs on double free)
RUN VERSION="2.7" \
  && curl -LO https://github.com/danmar/cppcheck/archive/refs/tags/${VERSION}.tar.gz \
  && SHA256="5fd20549bb2fabf9a8026f772779d8cc6a5782c8f17500408529f7747afbc526" \
  && (printf "${SHA256}  ${VERSION}.tar.gz" | sha256sum -c) \
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
  && (printf "${SHA256}  json.tar.xz" | sha256sum -c) \
  && tar xf json.tar.xz \
  && pushd json \
  && mkdir build && cd build \
  && cmake -GNinja -DJSON_BuildTests=Off ../ \
  && cmake --build . -t install \
  && popd \
  && rm -rf json json.tar.xz

# Google benchmark
RUN VERSION="1.6.1" \
  && curl -LO "https://github.com/google/benchmark/archive/refs/tags/v${VERSION}.tar.gz" \
  && SHA256="6132883bc8c9b0df5375b16ab520fac1a85dc9e4cf5be59480448ece74b278d4" \
  && (printf "${SHA256}  v${VERSION}.tar.gz" | sha256sum -c) \
  && tar xf v${VERSION}.tar.gz \
  && pushd benchmark-${VERSION} \
  # python is missing on ubuntu 20.04, use python 3 instead
  && { command -v python > /dev/null || { ln -s /usr/bin/python3 python && export PATH=$PWD:$PATH; }; } \
  && cmake -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_USE_BUNDLED_GTEST=OFF -GNinja -S . -B "build" \
  && cmake --build "build" --target install \
  && popd \
  && rm -rf benchmark-${VERSION} v${VERSION}.tar.gz

# A specific user is required to get access to perf event ressources.
# This enables unit testing using perf-event ressources
RUN adduser -D -s /bin/bash ddbuild
