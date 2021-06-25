FROM 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base as base

ARG CI
ENV CI=${CI:-false}
ADD . /app/
RUN cd /app && make deps
WORKDIR /app
