FROM 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base as base

ARG CI
ENV CI=${CI:-false}
ADD . /app/

RUN cd /app && mkdir build_Release && \
    cd build_Release && \
    cmake -DCMAKE_BUILD_TYPE=Release ../ && \
    make deps

RUN cd /app && make deps

WORKDIR /app
