FROM 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base as base

ARG CI
ENV CI=${CI:-false}
ADD . /app/

RUN cd /app && mkdir build_Release && \
    cd build_Release \
    cmake -DCMAKE_BUILD_MODE=Release \
    make deps

RUN cd /app && make deps

# A specific user is required to get access to perf event ressources. 
# This enables unit testing using perf-event ressources
RUN useradd -ms /bin/bash ddbuild
USER ddbuild

WORKDIR /app
