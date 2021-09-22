FROM 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base as base

# Fetch libddprof does not behave the same way to download from repo (to be adapted to github releases)
ARG CI
ENV CI=${CI:-false}
ADD . /app/

# C++ containers (header only, avoid pulling in boost)
RUN git clone --depth 1 --branch 2.0 https://github.com/AmadeusITGroup/amc.git \
    && cd amc \
    && mkdir build \
    && cd build \
    && cmake -DAMC_ENABLE_TESTS=0 -DAMC_ENABLE_BENCHMARKS=0 ../ \
    && cmake --build .  \
    && make install

RUN cd /app && mkdir build_Release && \
    cd build_Release && \
    cmake -DCMAKE_BUILD_TYPE=Release ../ && \
    make deps

WORKDIR /app
