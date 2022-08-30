
# Troubleshooting

## ddprof errors

### Failures to instrument

```bash
Could not finalize watcher (idx#1): registration (Operation not permitted)
```

In order to instrument the system or target application, ddprof must call `mmap()` on a special file descriptor returned by `perf_event_open()`.  Across versions of the Linux kernel, there are slightly different ways of accounting for pinned memory limits (depending on the current EUID, system configuration, phase of the moon, etc).  Here are some ideas for mitigating that limit:

- adding `IPC_LOCK` capabilities
- `perf_event_paranoid` setting to -1
- increasing the pinned memory limits
- running fewer `ddprof` instances in parallel

## Reaching the agent host

It is useful to verify that the target machine can connect to a Datadog agent.  Follow the Datadog troubleshooting guidelines.

## Dev tools

### Understanding the section layout

To get an overview of the section layout, use readelf. This will help you match what is loaded in proc maps.

```
readelf -lW <binary>
```

### Reading symbols

```
nm ./path_to_binary
```

### Disassembling code

Matching the instruction pointers to assembly code. 

```
gdb --batch  -ex 'disas function_name' ./path_to_binary
```

### Dumping the dwarf information

Getting the offsets from dwarf allows us to understand the unwinding patterns
Add a filter on the instruction pointer that is relevant to the investigation

```
readelf -wF ./path_to_binary
```

example:
```
0002eb58 000000000000001c 0002eb2c FDE cie=00000030 pc=000000000015cbb5..000000000015cd1b
   LOC           CFA      rbp   ra      
000000000015cbb5 rsp+8    u     c-8   
000000000015cbb6 rsp+16   c-16  c-8   
000000000015cbb9 rbp+16   c-16  c-8   
000000000015cd1a rsp+8    c-16  c-8   
```
