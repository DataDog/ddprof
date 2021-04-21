FROM 486234852809.dkr.ecr.us-east-1.amazonaws.com/ci/ddprof:base as base

ADD . /app/
RUN cd /app && make deps
WORKDIR /app
