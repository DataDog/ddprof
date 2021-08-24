FROM ubuntu:20.04 as base

# Tell docker to use bash as the default
SHELL ["/bin/bash", "-c"]

RUN apt update \
    && DEBIAN_FRONTEND=noninteractive apt install -y \
    awscli \
    cmake \
    git \
    gcc \
    g++ \
    flex \
    bison \
    strace \
    libjemalloc-dev \
    graphviz \
    valgrind \
    subversion \
    netcat

#Install perf tool (check kernel version and remove minor revision part), use svn as it is faster than git (with partial checkout)
RUN KERNEL_VERSION=$(uname -r | awk -F '[-\.]' '{print "v"$1"."$2}') && \
    echo "Kernel version : ${KERNEL_VERSION}" && \
    svn export --quiet  https://github.com/torvalds/linux/tags/${KERNEL_VERSION}/tools && \
    cd tools/perf && \
    make && \
    cp perf /usr/local/bin

#Retrieve a program to run
RUN git clone --depth 1 --branch ddprof_ci https://github.com/DataDog/bad-boggle-solver.git

#Build and install toy project
RUN cd bad-boggle-solver \ 
    && mkdir build \
    && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release ../ \
    && make -j 4 \
    && make install

# Attachement behaviour is not the same with a non root user 
RUN useradd -ms /bin/bash ddbuild
