FROM ubuntu:20.04 as base

RUN apt update \
    && DEBIAN_FRONTEND=noninteractive apt install -y \
    awscli \
    cmake \
    git \
    gcc \
    g++ \
    flex \
    bison \
    strace

#Install perf tool (check kernel version and remove trailing 0 that do not exist in git branch)
RUN KERNEL_VERSION=$(uname -r | awk -F '-' '{print $1}' | sed 's/\.0//') && \
    echo "Kernel version : ${KERNEL_VERSION}" && \
    git clone --depth 1 --branch "v${KERNEL_VERSION}" git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git && \
    cd linux-stable/tools/perf/ && \
    make && \
    cp perf /usr/local/bin

#Retrieve a program to run
RUN git clone --depth 1 --branch ddprof_ci https://github.com/DataDog/bad-boggle-solver.git

#Build the runtime that we will profile
RUN cd bad-boggle-solver \ 
    && mkdir build \
    && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release ../ \
    && make -j 4 \
    && make install
