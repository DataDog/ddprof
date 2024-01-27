# eBPF unwinding

## Motivation 

The native profiler was initially designed on top of perf event open.
The capture of every sample causes 32kB of memory to be copied at every sample.

What would it mean to have an eBPF unwinding instead ?

## Build 

Thie build does not work well
Manual target generations are still necessary
```bash
make libbpf-build
make bpftool-build
make generate_vmlinux_h
```

## Current state

The proposal is to attach a BPF sample processor 

## Acknowledgements

This mostly reuses the great tools provided by libbpf.
