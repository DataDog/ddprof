# AddressBitset Evolution: From Collisions to Sharded Hash Tables

This document chronicles the evolution of the `AddressBitset` data structure used for live allocation tracking in ddprof, from a collision-prone bitset to a sharded hash table. It includes production benchmark results comparing the final implementation against alternatives.

## The Problem: Bitset Collisions

The original `AddressBitset` used a simple bitset approach where addresses were hashed to bit positions:

```cpp
// OLD: Simple bitset with hash folding
uint32_t bit_index = (addr >> 4) ^ (addr >> 32);  // XOR fold
_bitset[bit_index / 64] |= (1ULL << (bit_index % 64));
```

**Critical Issue**: Hash collisions caused **~6% of allocations to be lost**:
- Two different addresses hash to same bit: collision
- When freeing, we can't distinguish which address the bit represents
- Result: Inaccurate leak detection, missed deallocations

Tests showed significant impact:
 - 6% loss rate

## Solution Requirements

We need a data structure that:
1. **No collisions** - track actual addresses, not just bits
2. **Thread-safe** - signal-safe, atomic operations only
3. **Fast lookups** - 100× more reads (free) than writes (malloc)
4. **Bounded memory** - fixed size or minimal growth
5. **Production-ready** - handle millions of allocations

## Explored Alternatives

### 1. Header-Based Tracking

Store metadata in a header before each tracked allocation:

```cpp
struct AllocationHeader {
  uint32_t magic;      // Validation marker
  uint32_t user_size;
  uint64_t timestamp;
};

void* tracked_malloc(size_t size) {
  void* raw = malloc(sizeof(Header) + size);
  Header* h = (Header*)raw;
  h->magic = 0xDDPROF01;
  return (char*)raw + sizeof(Header);
}

void tracked_free(void* ptr) {
  Header* h = (Header*)((char*)ptr - sizeof(Header));
  if (h->magic == 0xDDPROF01) {
    send_dealloc_event(ptr);
  }
  free(h);
}
```

**Why difficult to implement**:
- **Requires allocator cooperation** - hard to know if you can read before the pointer
- **Boundary safety** - reading header can segfault at memory boundaries
- **Allocator interference** - system `realloc()` doesn't know about our headers
- **Alignment issues** - if user asks for aligned mem, we need to respect the request

Could work with custom allocator support.

### 2. Stateless Sampling (Alternative Mode)

Pure hash-based deterministic sampling - no state at all:

```cpp
bool should_track(uintptr_t addr) {
  uint32_t hash = hash_address(addr);
  return (hash & _sampling_mask) == 0;  // e.g., track 1 in 256
}
```

**Performance**: ~140M ops/s @ 15 threads (10× faster than hash tables)

**Why not the primary solution**:
- **Introduces sampling bias** - deterministically misses some allocations based on address hash
- **Incomplete coverage** - cannot provide full live heap view
- **Production requirement** - accurate leak detection requires tracking all sampled allocations

**However**: Excellent candidate as an **optional low-overhead mode** for high-frequency profiling or quick leak scans.

## Sharded Open-Addressing Hash Tables (in this branch)

### Architecture

**Two-level structure**:
1. **Level 1**: Fixed redirect table
2. **Level 2**: Lazy-allocated per-chunk hash tables

```cpp
// Each address maps to a chunk based on top bits
chunk_idx = (addr >> 27) & 0x1FFF;  // 128MB chunks

// Each chunk has its own open-addressing hash table
AddressTable* table = _chunk_tables[chunk_idx];
```

### Why This Works

Glibc malloc spaces thread arenas
- Thread 1 allocates from `0x78e3'b800'0000` - `0x78e3'bfff'ffff` (128MB chunk)
- Thread 2 allocates from `0x78e3'c000'0000` - `0x78e3'c7ff'ffff` (different chunk)
- 128MB chunks align with typical allocator arena spacing for natural per-thread sharding

### Benefits

**No collisions** - stores actual addresses with linear probing  
**Thread separation** - different threads use different tables (not always true...)
**Lazy allocation** - only allocate tables for active chunks  
**Bounded memory** - max 128 chunks × 32K slots × 8 bytes = 32 MB worst case (typically much less)
**Fast lookups** - O(1) with minimal probing
**Signal-safe** - atomic operations only after initialization
**Hash-based sharding** - distributes addresses across 128 shards using hash function

## Future: Stateless Sampling as Alternative Mode

While not suitable as the primary solution due to sampling bias, stateless sampling could be offered as an **optional high-performance mode**:

**Use cases for sampling mode**:
- **Cheap leak detection** - quick scan for memory leaks with minimal overhead
- **High-frequency profiling** - when sampling overhead must be minimal
- **Complementary tool** - run alongside full tracking for different insights

**Benchmark results** (stateless sampling):
```
BM_AddressSampler_SingleThreaded                 7.11 ns    items_per_second=140.669M/s
BM_AddressSampler_MultiThreaded/threads:15      0.541 ns   items_per_second=123.227M/s
```

**10× faster than sharded hash tables** - zero contention, no memory overhead.

### Alignment-Aware Sampling Enhancement

Bias sampling towards larger allocations (which are more likely to be leaks):

```cpp
// Higher alignment → more likely large allocation → higher sample rate
bool should_track_with_alignment_bias(uintptr_t addr) {
  int alignment_bits = __builtin_ctzl(addr | 1);
  
  if (alignment_bits >= 12) {
    return true;  // Page-aligned: always track (likely mmap/large alloc)
  } else if (alignment_bits >= 10) {
    // 1KB-aligned: 4× more likely
    return (hash(addr) & (_sampling_mask >> 2)) == 0;
  } else if (alignment_bits >= 8) {
    // 256B-aligned: 2× more likely
    return (hash(addr) & (_sampling_mask >> 1)) == 0;
  } else {
    // Small: normal sampling
    return (hash(addr) & _sampling_mask) == 0;
  }
}
```

This biases detection towards large allocations (which are more impactful leaks) while maintaining determinism.
TODO: can we have meaningful numbers with this strategy ?

## Performance Comparison: Production Benchmarks

### Benchmark Setup

Test configuration:
- Workload: 8 threads doing malloc/free operations for 30 seconds
- Allocation size: 1000 bytes per allocation
- Sampling rate: Every 1MB allocated (p=1048576)
- Test includes 10µs spin between allocations to simulate real work

### Results Summary

**Test Configuration**: 1024-byte allocations, 8 threads, 120s runtime, 128MB chunk size

| Implementation | Memory Overhead | Out-of-Order Events | Lost Events | Unmatched Deallocations | Active Shards |
|----------------|-----------------|---------------------|-------------|-------------------------|---------------|
| Baseline (no profiling) | 4.0 MB | - | - | - | - |
| Sharded (128MB chunks) | 8.3 MB | 469,930 | 63 | 0 | 6 |
| Absl flat_hash_set | 6.4 MB | 469,659 | 75 | 0 | N/A |
| Global original | 5.9 MB | 521,311 | 39 | 36,509 | N/A |


#### Throughput Analysis

Configuration               Overhead
------------------------- ----------
Baseline (no profiling)        0.00%
Local (sharded)                1,01%
Absl (hashmap)                 0,84%
Global (original)              0,96%


**Observations:**
- All implementations show <1% throughput difference from baseline
- Performance differences are within measurement noise
- Allocation tracking overhead is minimal for all implementations

Cache misses are dominated by profiling infrastructure (unwinding, ring buffer):
- Baseline: ~7M cache misses
- All profiled versions: ~176-201M cache misses (25× increase from profiling overhead, not tracking structure)

### Memory Usage Analysis

~18 MB of overhead on simple malloc (in process).

### Current bench results

```
---------------------------------------------------------------------------------------------------------------------------------------
Benchmark                                                                             Time             CPU   Iterations UserCounters...
---------------------------------------------------------------------------------------------------------------------------------------
BM_AddressBitset_RealAddresses                                                     38.0 ns         37.9 ns     18771471 items_per_second=52.7248M/s
BM_AddressBitset_RealAddresses_MT<ContentionMode::kHighContention>/threads:1       37.4 ns         37.4 ns     18050112 items_per_second=53.4826M/s
BM_AddressBitset_RealAddresses_MT<ContentionMode::kHighContention>/threads:4       23.4 ns         93.6 ns      8318084 items_per_second=21.3648M/s
BM_AddressBitset_RealAddresses_MT<ContentionMode::kHighContention>/threads:8       17.2 ns          138 ns      5189760 items_per_second=14.5383M/s
BM_AddressBitset_RealAddresses_MT<ContentionMode::kLowContention>/threads:1        36.7 ns         36.7 ns     19634620 items_per_second=54.5395M/s
BM_AddressBitset_RealAddresses_MT<ContentionMode::kLowContention>/threads:4        29.2 ns          116 ns      9871880 items_per_second=17.17M/s
BM_AddressBitset_RealAddresses_MT<ContentionMode::kLowContention>/threads:8        16.5 ns          132 ns      4187944 items_per_second=15.1239M/s
BM_AddressBitset_HighLoadFactor                                                    66.3 ns         66.3 ns      8040949 items_per_second=15.0838M/s
BM_AddressBitset_LiveTracking<ContentionMode::kHighContention>/threads:1           41.7 ns         41.7 ns     17390221 items_per_second=23.9959M/s
BM_AddressBitset_LiveTracking<ContentionMode::kHighContention>/threads:4           3.09 ns         12.4 ns     64748460 items_per_second=80.9149M/s
BM_AddressBitset_LiveTracking<ContentionMode::kHighContention>/threads:8           1.75 ns         14.0 ns     48877848 items_per_second=71.5288M/s
BM_AddressBitset_LiveTracking<ContentionMode::kLowContention>/threads:1            39.0 ns         39.0 ns     17565617 items_per_second=25.6674M/s
BM_AddressBitset_LiveTracking<ContentionMode::kLowContention>/threads:4            30.6 ns          122 ns      4992880 items_per_second=8.17822M/s
BM_AddressBitset_LiveTracking<ContentionMode::kLowContention>/threads:8            24.3 ns          195 ns      3670720 items_per_second=5.14027M/s
BM_AddressBitset_FreeLookupMiss<ContentionMode::kHighContention>/threads:1         10.3 ns         10.2 ns     69208211 items_per_second=97.6305M/s
BM_AddressBitset_FreeLookupMiss<ContentionMode::kHighContention>/threads:4         3.21 ns         12.9 ns     55299296 items_per_second=77.8015M/s
BM_AddressBitset_FreeLookupMiss<ContentionMode::kHighContention>/threads:8         2.05 ns         16.4 ns     34074824 items_per_second=60.9842M/s
BM_AddressBitset_FreeLookupMiss<ContentionMode::kLowContention>/threads:1          10.4 ns         10.4 ns     68615918 items_per_second=96.3036M/s
BM_AddressBitset_FreeLookupMiss<ContentionMode::kLowContention>/threads:4          2.15 ns         8.60 ns     68968204 items_per_second=116.28M/s
BM_AddressBitset_FreeLookupMiss<ContentionMode::kLowContention>/threads:8          1.34 ns         10.7 ns     74355952 items_per_second=93.2849M/s
BM_AddressBitset_FreeLookupMiss_HighLoad                                           11.5 ns         11.4 ns     66347069 items_per_second=87.3973M/s
```


## Conclusion

**Implementation Status**:
Absl seems to crash (within the benchmark in this branch). This is expected considering it is not thread safe.
The sharded fixed hashmap solution seems to have acceptable overhead.

**Open questions**:
- Memory vs accuracy trade-offs in production environments with diverse workloads

**Future work**:
- Add stateless sampling as alternative "low-overhead" mode
- Consider header-based approach if custom allocator integration becomes available
- Monitor memory usage in production environments with diverse allocation patterns
- Bench under heavier load (with many tracked addresses).
