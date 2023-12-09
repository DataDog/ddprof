#!/bin/bash

#todo plug this somewhere in the build
git clone https://github.com/libbpf/libbpf.git
git clone https://github.com/libbpf/bpftool.git
cd bpftool && git submodule update --init --recursive
