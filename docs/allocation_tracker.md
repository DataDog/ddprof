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
1. **Level 1**: Fixed redirect table (8192 entries, 64KB)
2. **Level 2**: Lazy-allocated per-chunk hash tables (4MB each)

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
**Thread separation** - different threads use different tables (not totally true...)
**Lazy allocation** - only allocate tables for active chunks  
**Bounded memory** - max 128 chunks × 32K slots × 8 bytes = 32 MB worst case (typically much less)
**Fast lookups** - O(1) with minimal probing
**Signal-safe** - atomic operations only after initialization
**Hash-based sharding** - distributes addresses across 128 shards using hash function

### Performance

```
Single-threaded:  26M ops/s
Multi-threaded:   17M ops/s @ 15 threads (with proper per-thread addresses)
High contention:  1.2M ops/s @ 15 threads (all threads in same chunk)
```

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

Allocations per thread over 102 seconds (8 threads total, 1KB allocations):

| Implementation | Total Allocations | Allocations/sec | Throughput vs Baseline |
|----------------|-------------------|-----------------|------------------------|
| Baseline | 20,000,000 | 195,115/s | 100% |
| Sharded | 20,000,000 | 194,216/s | 99.5% |
| Absl | 20,000,000 | 193,614/s | 99.2% |
| Previous | 20,000,000 | 194,754/s | 99.8% |

**Observations:**
- All implementations show <1% throughput difference from baseline
- Performance differences are within measurement noise
- Allocation tracking overhead is minimal for all implementations

Cache misses are dominated by profiling infrastructure (unwinding, ring buffer):
- Baseline: ~7M cache misses
- All profiled versions: ~176-201M cache misses (25× increase from profiling overhead, not tracking structure)

### Analysis: Absl flat_hash_set vs Sharded Implementation

**Key findings**:
- With proper chunk sizing (128MB), the sharded implementation achieves good thread distribution
- Out-of-order events are now comparable between all implementations (~470K)
- Lost events are minimal for both new implementations (<100 in initial period, 0 subsequently)
- Memory overhead difference is modest (1.9 MB = ~30% difference)
- The critical advantage of sharded implementation is guaranteed thread safety and explicit design for concurrent access

### Memory Usage Analysis

The sharded implementation uses 8.3 MB (4.3 MB overhead vs baseline) with 128MB chunks:
- **Level 1 redirect table**: 64 KB (8192 pointers × 8 bytes)
- **Level 2 hash tables**: 4 MB per active shard (512K slots × 8 bytes)
- **Active shards**: 6 shards allocated (out of 8 threads)
- **Total**: 64 KB + (6 × 4 MB) = ~24 MB allocated, 8.3 MB RSS
- **Lazy allocation**: Only allocates for chunks with active allocations


### Unmatched Deallocation Note

The new implementations (sharded and Absl) eliminate unmatched deallocations by storing actual addresses instead of using a bitset. The global original implementation still shows 36,509 unmatched deallocations in the test due to hash collisions in the bitset - when multiple addresses hash to the same bit, deallocations may incorrectly match the wrong allocation or fail to find any match.

## Conclusion

**Implementation Status**:
Absl seems to crash (within the benchmark in this branch).
So we have to go for our own implementation.

**Open questions**:
- Memory vs accuracy trade-offs in production environments with diverse workloads

**Future work**:
- Add stateless sampling as alternative "low-overhead" mode
- Consider header-based approach if custom allocator integration becomes available
- Monitor memory usage in production environments with diverse allocation patterns
- Bench under heavier load (with many tracked addresses).
