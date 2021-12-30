ARG BASE_IMAGE=486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base_18
FROM ${BASE_IMAGE} as base

# Fetch libddprof does not behave the same way to download from repo (to be adapted to github releases)
ARG CI
ENV CI=${CI:-false}
ADD . /app/

RUN cd /app && mkdir build_Release && \
    cd build_Release && \
    cmake -DCMAKE_CXX_COMPILER=${CXX} -DCMAKE_C_COMPILER=${CC} -DCMAKE_BUILD_TYPE=Release ../ && \
    make deps

WORKDIR /app
