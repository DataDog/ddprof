FROM alpine:3.16.0
ARG COMPILER="gcc"
ENV CC=gcc
ENV CXX=g++

# unused
#ENV GCC_VERSION=11
#ENV CLANG_VERSION=14


#MISSING
# lcov
# liblzma
# libjemalloc
RUN apk update \
    && apk add --no-cache aws-cli \
	 bash \
	 binutils-dev \
	 ca-certificates \
	 gcovr \
	 git \
	 subversion \
	 curl \
	 wget \
	 make \
	 cmake \
	 m4 \
	 unzip \
	 gcc \
	 g++ \
	 clang \
	 libunwind-dev \
	 py3-pkgconfig \
	 gtest-dev \
	 cppcheck \
	 openssh \
	 zlib-dev \
	 libcap-dev \
	 netcat-openbsd

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

# Codeql : static analysis tooling
RUN curl -L https://github.com/github/codeql-action/releases/download/codeql-bundle-20210622/codeql-bundle-linux64.tar.gz -o - | tar -xz -C /usr/local

## Alpine 3.16 has gcc 11 and clang 14
####################
## LLVM/GCC SETUP ##
####################
#ADD llvm.sh gcc.sh /
#ARG PREVENT_LLVM_INSTALL="OFF"
#RUN if [[ ${PREVENT_LLVM_INSTALL} == "OFF" ]]; then /llvm.sh ${CLANG_VERSION}; fi
#RUN /gcc.sh ${GCC_VERSION}

# Provides the llvm-symbolizer (better debug information in case of sanitizer issue)
ENV PATH="/usr/lib/llvm-${CLANG_VERSION}/bin/:$PATH"

### Alpine has cmake 3.23
## Newer CMake
#RUN VERSION="3.22.2" \
#  && MARCH=$(uname -m) \
#  && SHA256_ARM="02b2b36afc932ede2d77ba53456785bae85538d5d4600f87220072b95d926c5b" \
#  && SHA256_X86="38b3befdee8fd2bac06954e2a77cb3072e6833c69d8cc013c0a3b26f1cfdfe37" \
#  && if [ "$MARCH" = aarch64 ]; then SHA256=$SHA256_ARM; else SHA256=$SHA256_X86; fi \
#  && TAR_NAME="cmake-${VERSION}-Linux-${MARCH}.tar.gz" \
#  && curl -LO https://github.com/Kitware/CMake/releases/download/v${VERSION}/${TAR_NAME} \
#  && (printf "${SHA256}  ${TAR_NAME}" | sha256sum -c -s) \
#  && tar --no-same-owner -C /usr/local --strip-components=1 -xf ${TAR_NAME} \
#  && rm ${TAR_NAME}

# Ninja build 1.10.2
#RUN VERSION="1.10.2" \
#  && SHA256="ce35865411f0490368a8fc383f29071de6690cbadc27704734978221f25e2bed" \
#  && curl -LO https://github.com/ninja-build/ninja/archive/refs/tags/v${VERSION}.tar.gz \
#  && (printf "${SHA256} v${VERSION}.tar.gz" | sha256sum -c -s) \
#  && tar xvfz v${VERSION}.tar.gz \
#  && cd ninja-${VERSION} \
#  && cmake -Bbuild-cmake \
#  && cmake --build build-cmake  -j $(nproc) -t install
RUN VERSION="1.10.2" && curl -LO https://github.com/ninja-build/ninja/archive/refs/tags/v${VERSION}.tar.gz
RUN VERSION="1.10.2" && tar xvfz v${VERSION}.tar.gz
RUN VERSION="1.10.2" && cd ninja-${VERSION} && cmake -Bbuild-cmake
RUN VERSION="1.10.2" && cd ninja-${VERSION} && cmake --build build-cmake -j 4 -t install

## Alpine has gtest 1.12
## google test / google mock
#RUN VERSION="1.11.0" \
#  && curl -LO https://github.com/google/googletest/archive/refs/tags/release-${VERSION}.tar.gz \
#  && SHA256="b4870bf121ff7795ba20d20bcdd8627b8e088f2d1dab299a031c1034eddc93d5" \
#  && (printf "${SHA256} release-${VERSION}.tar.gz" | sha256sum -c -s) \
#  && tar xf release-${VERSION}.tar.gz \
#  && pushd googletest-release-${VERSION} \
#  && mkdir build \
#  && cd build \
#  && cmake -GNinja ../ \
#  && cmake --build . -t install \
#  && popd \
#  && rm -rf googletest-release-${VERSION} release-${VERSION}.tar.gz

## Alpine has cppcheck 2.8.1-r0
## More recent Cpp check (ubuntu defaults to a 1.8 version)
#RUN VERSION="2.8" \
#  && curl -LO https://github.com/danmar/cppcheck/archive/refs/tags/${VERSION}.tar.gz \
#  && SHA256="57298f3b805f0eb816a04115fbc70e701f75083cfb0305a44246e365cf27606a" \
#  && (printf "${SHA256} ${VERSION}.tar.gz" | sha256sum -c -s) \
#  && tar xf ${VERSION}.tar.gz \
#  && pushd cppcheck-${VERSION} \
#  && mkdir build \
#  && cd build \
#  && cmake -GNinja ../ \
#  && cmake --build . -t install  \
#  && popd \
#  && rm -rf ${VERSION}.tar.gz cppcheck-${VERSION}

# C++ json library (used for test purpose)
#RUN VERSION="3.10.5" \
#  && curl -LO https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz \
#  && SHA256="344be97b757a36c5b180f1c8162f6c5f6ebd760b117f6e64b77866e97b217280" \
#  && (printf "${SHA256} json.tar.xz" | sha256sum -c -s) \
#  && tar xf json.tar.xz \
#  && pushd json \
#  && mkdir build && cd build \
#  && cmake -GNinja -DJSON_BuildTests=Off ../ \
#  && cmake --build . -t install \
#  && popd \
#  && rm -rf json json.tar.xz

RUN VERSION="3.10.5" && curl -LO https://github.com/nlohmann/json/releases/download/v${VERSION}/json.tar.xz
RUN VERSION="3.10.5" && tar xf json.tar.xz
RUN VERSION="3.10.5" && mkdir -p json/build
RUN VERSION="3.10.5" && cd json/build && cmake -GNinja -DJSON_BuildTests=Off ../
RUN VERSION="3.10.5" && cd json/build && cmake --build . -t install
RUN VERSION="3.10.5" && rm -rf json json.tar.

# Google benchmark
RUN VERSION="1.6.1" \
  && curl -LO "https://github.com/google/benchmark/archive/refs/tags/v${VERSION}.tar.gz" \
  && tar xf v${VERSION}.tar.gz \
  && pushd benchmark-${VERSION} \
  && { command -v python > /dev/null || { ln -s /usr/bin/python3 python && export PATH=$PWD:$PATH; }; } \
  && cmake -DCMAKE_BUILD_TYPE=Release -DBENCHMARK_USE_BUNDLED_GTEST=OFF -GNinja -S . -B "build" \
  && cmake --build "build" --target install \
  && popd \
  && rm -rf benchmark-${VERSION} v${VERSION}.tar.gz

# UNUSED
# A specific user is required to get access to perf event ressources.
# This enables unit testing using perf-event ressources
#RUN useradd -ms /bin/bash ddbuild
