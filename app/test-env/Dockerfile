ARG UBUNTU_VERSION=20
FROM ubuntu:${UBUNTU_VERSION}.04 as base

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

# Some of these are just for investigations (as this image is also used to test locally)
RUN apt update \
    && DEBIAN_FRONTEND=noninteractive apt install -y \
    awscli \
    git \
    curl \
    gcc \
    g++ \
    make \
    flex \
    bison \
    strace \
    libjemalloc-dev \
    graphviz \
    valgrind \
    subversion \
    netcat-openbsd \
    vim \
    bsdmainutils

# Newer CMake
FROM base as cmake
RUN CMAKE_VERSION="3.20.1" \
    && CMAKE_SHA256="b8c141bd7a6d335600ab0a8a35e75af79f95b837f736456b5532f4d717f20a09" \
    && curl -L --remote-name-all https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
    && (printf "${CMAKE_SHA256}  cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz" | sha256sum --check --strict --status) \
    && tar --no-same-owner -C /usr/local --strip-components=1 -xf cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz \
    && rm cmake-${CMAKE_VERSION}-Linux-x86_64.tar.gz

# Install perf tool (check kernel version and remove minor revision part), use svn as it is faster than git (with partial checkout)
RUN KERNEL_VERSION=$(uname -r | awk -F '[-\.]' '{print "v"$1"."$2}') && \
    echo "Kernel version : ${KERNEL_VERSION}" && \
    svn export --quiet  https://github.com/torvalds/linux/tags/${KERNEL_VERSION}/tools && \
    cd tools/perf && \
    make && \
    cp perf /usr/local/bin

# Retrieve a program to run
RUN git clone --depth 1 --branch ddprof_ci_v3 https://github.com/DataDog/bad-boggle-solver.git

# Build and install toy project
RUN cd bad-boggle-solver \
    && mkdir build \
    && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release ../ \
    && make -j 4 \
    && make install

# Attachement behaviour is not the same with a non root user 
RUN useradd -ms /bin/bash ddbuild
