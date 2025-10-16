# AddressBitset Evolution: From Collisions to Sharded Hash Tables

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
- 500K unique addresses → ~30K collisions (6% loss rate)
- Production workloads → unreliable live heap tracking

## Solution Requirements

We need a data structure that:
1. **No collisions** - track actual addresses, not just bits
2. **Thread-safe** - signal-safe, atomic operations only
3. **Fast lookups** - 100× more reads (free) than writes (malloc)
4. **Bounded memory** - fixed size or minimal growth
5. **Production-ready** - handle millions of allocations

## Explored Alternatives

### 1. Header-Based Tracking (Rejected)

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

**Why rejected**:
- **Requires allocator cooperation** - it is not easy to know if you can read before the pointer
- **Boundary safety** - reading header can segfault at memory boundaries
- **Allocator interference** - system `realloc()` doesn't know about our headers

Could work with custom allocator support, but too invasive for general use.

### 2. Stateless Sampling (Alternative Mode)

Pure hash-based deterministic sampling - no state at all:

```cpp
bool should_track(uintptr_t addr) {
  uint32_t hash = hash_address(addr);
  return (hash & _sampling_mask) == 0;  // e.g., track 1 in 256
}
```

**Performance**: ~140M ops/s @ 15 threads (10× faster than hash tables!)

**Why not the main solution**:
- **Introduces sampling bias** - not all allocations tracked
- **Can't track everything** - misses allocations deterministically
- **Production requirement** - we need accurate live heap view

**However**: Could be valuable as a **low-overhead mode** for high-frequency profiling or initial leak detection.

## Chosen Solution: Sharded Open-Addressing Hash Tables

### Architecture

**Two-level structure**:
1. **Level 1**: Fixed redirect table (256 entries, 2KB)
2. **Level 2**: Lazy-allocated per-chunk hash tables (8MB each)

```cpp
// Each address maps to a chunk based on top bits
chunk_idx = (addr >> 32) & 0xFF;  // 4GB chunks

// Each chunk has its own open-addressing hash table
AddressTable* table = _chunk_tables[chunk_idx];
```

### Why This Works

**Key insight**: Real allocators use per-thread arenas with clustered addresses:
- Thread 1 allocates from `0x7f00'0000'0000` - `0x7f00'ffff'ffff` (chunk 0x7f00)
- Thread 2 allocates from `0x7f01'0000'0000` - `0x7f01'ffff'ffff` (chunk 0x7f01)
- **Different threads → different chunks → different hash tables → no contention!**

### Benefits

**No collisions** - stores actual addresses with linear probing  
**Thread separation** - different threads use different tables  
**Lazy allocation** - only allocate tables for active chunks  
**Bounded memory** - max 256 chunks × 8MB = 2GB (but only allocate what's used)  
**Fast lookups** - O(1) with minimal probing  
**Signal-safe** - atomic operations only after initialization  

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

## Conclusion

**Chosen**: Sharded open-addressing hash tables for production live heap tracking.

**Why**: 
- No collisions (accurate tracking)
- No sampling bias (full coverage when capacity allows)
- Thread-friendly (natural separation via address ranges)
- Signal-safe (atomic operations only)

**Future work**:
- Add stateless sampling as alternative "low-overhead" mode
- Consider header-based approach if custom allocator integration becomes available
