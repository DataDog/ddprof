# Plan: Replace AddressBitset with Architecture-Specific Sampled Allocation Markers

## Context

The `AllocationTracker` uses an `AddressBitset` (2-level sharded hashtable, ~4-32 MB memory) to track sampled allocation addresses for live heap profiling. On every `free`, it looks up the address in this hashtable to determine if a deallocation event should be emitted.

**Goal**: Replace this with cheaper, architecture-specific mechanisms:
- **ARM64**: Tagged pointers (TBI - Top Byte Ignore) to mark sampled allocations in the pointer itself
- **AMD64 non-mmap**: A hidden prefix before the returned pointer containing a magic signature
- **AMD64 mmap/pvalloc/valloc**: Keep the existing AddressBitset

## Files to Create

### `include/lib/sampled_allocation_marker.hpp` (new)

Architecture-specific marker abstraction:

**ARM64 (TBI):**
- `tag(void *ptr)` - Set bit 60 in the pointer (avoids MTE range bits 59:56)
- `is_tagged(void *ptr)` - Check bit 60
- `untag(void *ptr)` - Clear bit 60
- No memory overhead, no false positives, works for ALL allocation types including mmap

**AMD64 (prefix):**
- `Prefix` struct: `{ uint64_t magic; uint64_t offset; }` (16 bytes, offset = bytes from original to user ptr)
- `prefix_size_for_alignment(size_t alignment)` - Returns `max(alignment, 16)`
- `write_prefix(void *raw_ptr, size_t alignment)` - Writes magic+offset at `user_ptr - 16`, returns user_ptr
- `read_prefix(void *ptr)` - Checks magic at `ptr - 16`, returns `{found, original_ptr}` (computed via offset)
- `is_page_aligned(void *ptr)` - Check `(ptr & 0xFFF) == 0`
- Magic: `0xDD9F0F5A3901E001ULL` (64-bit, false positive probability ~2^-64)

## Files to Modify

### `include/lib/allocation_tracker.hpp`

1. **Conditional AddressBitset member**: Wrap `_allocated_address_set` in `#ifdef __x86_64__` (removed on ARM64)
2. **New static methods**:
   - `will_sample(size_t size, TrackerThreadLocalState &tl_state)` - Pre-check for AMD64: returns true when `track_deallocations` is active, `remaining_bytes_initialized` is true, and `remaining_bytes + size >= 0`. Returns false conservatively when uninitialized (misses at most 1 sample per thread).
   - `track_allocation_s_sampled(...)` - Same as `track_allocation_s` but returns bool. Used by ARM64 to know if allocation was sampled (so it can tag the pointer AFTER tracking).
   - `track_deallocation_direct_s(uintptr_t addr, TrackerThreadLocalState &tl_state)` - Push dealloc event WITHOUT bitset lookup. Called when marker (tag/prefix) already confirmed this was sampled.
3. **New private method**: `track_deallocation_direct(uintptr_t addr, TrackerThreadLocalState &tl_state)` - Calls `push_dealloc_sample` directly.

### `src/lib/allocation_tracker.cc`

1. **`track_allocation`**: On AMD64, only call `_allocated_address_set.add()` when `is_large_alloc == true` (mmap/pvalloc/valloc). On ARM64, no bitset operations at all.
2. **`track_deallocation`**: On AMD64, bitset remove only for mmap. On ARM64, this becomes dead code (all deallocs go through `track_deallocation_direct`).
3. **`init()`**: On ARM64, skip `_allocated_address_set.init()`.
4. **New `track_deallocation_direct()`**: Just calls `push_dealloc_sample` + `free_on_consecutive_failures`.
5. **`push_allocation_tracker_state()`**: On ARM64, report 0 for `tracked_address_count`/`active_shards`.

### `src/lib/symbol_overrides.cc` (largest change)

Add `#include "sampled_allocation_marker.hpp"`.

#### Helper class changes

**`AllocTrackerHelperImpl`**: Add arch-specific methods:
- ARM64: `track_sampled(void *ptr, size_t size)` - calls `track_allocation_s_sampled`, returns bool
- AMD64: `will_sample(size_t size)` - calls `AllocationTracker::will_sample`

**`DeallocTrackerHelperImpl`**: Add `track_direct(void *ptr)` - calls `track_deallocation_direct_s` (bypasses bitset).

#### ARM64 hook pattern (all allocation types)

```cpp
// Alloc hooks: track, then tag if sampled
auto *ptr = ref(size);
if (helper.track_sampled(ptr, size)) {
    ptr = marker::tag(ptr);
}
return ptr;

// Free hooks: check tag, untag, track direct
if (marker::is_tagged(ptr)) {
    ptr = marker::untag(ptr);
    DeallocTrackerHelper helper;
    helper.track_direct(ptr);
}
ref(ptr);  // free untagged
```

#### AMD64 non-mmap hook pattern

```cpp
// Alloc hooks: pre-check sampling, allocate with/without prefix
bool ws = helper.will_sample(size);
void *ptr;
if (ws) {
    ptr = ref(size + marker::kMinPrefixSize);
    if (ptr) ptr = marker::write_prefix(ptr, 16);
} else {
    ptr = ref(size);
}
helper.track(ptr, size);
return ptr;

// Free hooks: check prefix (non-page-aligned only), then free original
if (!marker::is_page_aligned(ptr)) {
    auto [found, original] = marker::read_prefix(ptr);
    if (found) {
        DeallocTrackerHelper helper;
        helper.track_direct(ptr);
        ref(original);  // free the real allocation
        return;
    }
}
// Page-aligned: use mmap bitset path (pvalloc/valloc/mmap)
if (marker::is_page_aligned(ptr)) {
    DeallocTrackerHelperMmap helper;
    helper.track(ptr);
} else {
    DeallocTrackerHelper helper;
    helper.track(ptr);
}
ref(ptr);
```

#### Special cases

- **`calloc`** (AMD64): When sampling, call `ref(1, total + kMinPrefixSize)` (calloc zeroes memory), then overwrite prefix area with magic. User data area remains properly zeroed.
- **`realloc`/`rallocx`** (both arches): Detect tag/prefix on old pointer, untag/unprefix before passing to real realloc. Possibly tag/prefix the new pointer.
- **`aligned_alloc`/`posix_memalign`/`memalign`** (AMD64): Prefix size = `max(alignment, 16)` to maintain alignment. Call `ref(alignment, size + prefix_size)`.
- **`pvalloc`/`valloc`** (AMD64): **Cannot use prefix** - they return page-aligned pointers, and adding a page-sized prefix produces another page-aligned user pointer that would be skipped by the `is_page_aligned` safety check. Use `AllocTrackerHelperMmap` (bitset with `is_large_alloc=true`) instead.
- **`mallocx`** (AMD64 jemalloc): Extract alignment from flags `(1 << (flags & 0x3f))`, apply prefix accordingly.
- **`xallocx`** (AMD64 jemalloc): In-place resize; prefix already in place, just adjust size accounting. Use `track_deallocation_direct_s` when prefix is found, `track_deallocation_s` otherwise.
- **`free_sized`/`free_aligned_sized`** (AMD64): For prefixed allocs, fall back to regular `free(original)` since exact size is unknown.
- **Delete hooks** (AMD64): For prefixed allocs, always call `FreeHook::ref(original)` since the real allocation was done via the matched allocator path.
- **mmap/munmap** (AMD64): Unchanged - continue using bitset path via `AllocTrackerHelperMmap`/`DeallocTrackerHelperMmap`.

### `test/allocation_tracker-ut.cc`

1. **`my_free()`**: Use `track_deallocation_direct_s` instead of `track_deallocation_s`. The direct test API doesn't go through hooks (no prefix/tag marker), so the bitset check must be bypassed.
2. **`0xcafebabe` "not tracked" test**: Removed. With marker-based detection, the "not tracked" filtering happens at the hook level (prefix/tag check), not in the direct API.
3. **`test_allocation_functions` teardown**: Call `allocation_tracking_free()` BEFORE removing hooks (do NOT call `restore_overrides()`). With the prefix approach, any allocation prefixed during the test must be freed through hooks that detect and handle the prefix. After `allocation_tracking_free()`, hooks become harmless pass-throughs (no tracking, no prefixing, but prefix detection in free still works).
4. On ARM64, `check_alloc()`/`check_dealloc()` should untag pointers before comparing with event addresses.

### `test/allocation_tracker-bench.cc`

Same `my_free` fix as unit test (`track_deallocation_direct_s`).

## Key Design Properties

- **`will_sample` correctness (AMD64)**: When `remaining_bytes_initialized == true`, `will_sample` returning true guarantees `track_allocation_s` will sample (no false positives). The only gap is `remaining_bytes_initialized == false` (first alloc per thread) where we conservatively skip prefix.
- **No bitset on ARM64**: `_allocated_address_set` member is `#ifdef`'d out entirely. ALL allocation types (including mmap) use tagged pointers.
- **Bitset only for mmap/pvalloc/valloc on AMD64**: Non-mmap non-page-aligned deallocs detected via prefix magic. Page-aligned deallocs detected via bitset (mmap helper with `is_large_alloc=true`).
- **Prefix for jemalloc on AMD64**: `mallocx`/`rallocx`/`dallocx`/`sdallocx` use the prefix approach. Alignment extracted from jemalloc flags.
- **Thread safety**: Marker operations are either pure pointer arithmetic (ARM64) or write/read memory owned by the current allocation (AMD64). No shared state concerns.
- **`is_page_aligned` safety (AMD64)**: Reading `ptr - 16` is NOT safe for all pointers. Page-aligned pointers may be at the start of mmap'd regions where the previous page is unmapped. The `is_page_aligned` check prevents segfaults. This is why pvalloc/valloc use the bitset path instead of the prefix approach.
- **False positive risk (AMD64)**: 64-bit magic at a fixed offset has ~2^-64 chance of accidental match in allocator metadata.
- **Hooks must outlive prefixed pointers**: After `restore_overrides()` removes hooks, any prefixed pointer freed through the original `free` will cause heap corruption (wrong pointer passed to allocator). In production this is not an issue (profiler runs for process lifetime). In tests, disable the tracker (`allocation_tracking_free`) but keep hooks active.

## Implementation Sequence

1. Create `sampled_allocation_marker.hpp` (no behavioral change)
2. Add new methods to `AllocationTracker` (no behavioral change)
3. ARM64: ifdef out bitset, modify hooks to use tags, update tests
4. AMD64: modify non-mmap hooks to use prefix, modify free hooks to check prefix, update `track_allocation` to skip bitset for non-mmap, route pvalloc/valloc through mmap/bitset path
5. Update tests and benchmarks

## Verification

1. Run existing test suite (`allocation_tracker-ut`, `allocation_tracker_jemalloc-ut`, `address_bitset-ut`, `allocation_tracker_fork_test`)
2. Run benchmarks (`allocation_tracker-bench`) to verify performance improvement
3. Manual test with `simple_malloc` + `--preset cpu_live_heap` to verify end-to-end live heap profiling
