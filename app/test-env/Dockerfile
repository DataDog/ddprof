FROM ubuntu:20.04 as base

RUN apt update \
    && DEBIAN_FRONTEND=noninteractive apt install -y \
    awscli \
    cmake \
    git \
    gcc \
    g++

#Retrieve a program to run
RUN git clone --depth 1 --branch ddprof_ci https://github.com/DataDog/bad-boggle-solver.git

#Build the runtime that we will profile
RUN cd bad-boggle-solver \ 
    && mkdir build \
    && cd build \
    && cmake -DCMAKE_BUILD_TYPE=Release ../ \
    && make -j 4 \
    && make install
