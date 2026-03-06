# Profile Sample Type Mapping

## Context

ddprof instruments the kernel via perf events and reports profiling data as
pprof profiles. Each pprof profile has a fixed set of **value columns**
(sample types), each identified by a `(type, unit)` string pair that the
Datadog backend uses to interpret the data (e.g. `cpu-time/nanoseconds`,
`alloc-space/bytes`).

libdatadog v28+ represents these as a `ddog_prof_SampleType` enum rather than
free-form strings. ddprof maps each watcher to the appropriate enum values via
the `WatcherSampleTypes` struct defined in `include/watcher_sample_types.hpp`.

---

## Two Dimensions

### Primary vs. Count companion

Most profiling events are reported as a pair of columns:

| Role | Meaning | Example |
|---|---|---|
| **Primary** | The measured quantity (bytes, nanoseconds, event count) | `alloc-space/bytes` |
| **Count companion** | Number of samples that contributed to the primary value | `alloc-samples/count` |

When no count companion applies (tracepoints, plain sample counts), the count
slot is left empty. In `WatcherSampleTypes`, the sentinel value
`k_stype_val_sample` in `count_types[]` signals "no count companion".

### Aggregation mode

Each watcher can operate in one or both of two modes, controlled by the `mode=`
event configuration key:

| Mode | Constant | `mode=` flag | Meaning |
|---|---|---|---|
| Sum | `kSumPos` (0) | `s` (default) | Cumulative totals over the collection interval |
| Live | `kLiveSumPos` (1) | `l` | Snapshot of currently live/in-use resources |

The two modes produce **different pprof columns**. For example, allocation
profiling in sum mode reports how much was allocated; in live mode it reports
what is still alive (useful for leak detection).

---

## Watcher → Column Mapping

### `k_stype_cpu` — CPU profiling (`sCPU`)

| Mode | Primary column | Count companion |
|---|---|---|
| Sum (`kSumPos`) | `cpu-time / nanoseconds` | `cpu-samples / count` |
| Live (`kLiveSumPos`) | `cpu-samples / count` | *(none)* |

CPU profiling is almost always used in sum mode. Live mode is not a meaningful
concept for CPU time; the live slot exists for completeness but is unused in
practice.

### `k_stype_alloc` — Allocation profiling (`sALLOC`)

| Mode | Primary column | Count companion |
|---|---|---|
| Sum (`kSumPos`) | `alloc-space / bytes` | `alloc-samples / count` |
| Live (`kLiveSumPos`) | `inuse-space / bytes` | `inuse-objects / count` |

The same `sALLOC` watcher produces different columns depending on the mode:
- `mode=s` (default): total bytes and sample count allocated over the interval.
- `mode=l`: bytes and object count still alive (not yet freed) — the basis for
  heap leak profiles.
- `mode=sl`: all four columns are emitted simultaneously.

### `k_stype_tracepoint` — Hardware counters and perf tracepoints

| Mode | Primary column | Count companion |
|---|---|---|
| Sum (`kSumPos`) | `tracepoint / events` | *(none)* |
| Live (`kLiveSumPos`) | `tracepoint / events` | *(none)* |

Used by hardware performance counters (`hCPU`, `hREF`, `hINST`, …), software
events (`sPF`, `sCS`, …), and named kernel tracepoints (e.g.
`event=tlb:tlb_flush`). Each sample represents one event occurrence; there is
no meaningful count companion.

> **Backend note**: prior to libdatadog v28, the `(tracepoint, events)` strings
> were passed as free-form values. In v28+, `DDOG_PROF_SAMPLE_TYPE_TRACEPOINT`
> is the canonical enum value. The integer value of this enum is verified at
> compile time in `ddprof_pprof.cc` via `static_assert`.

---

## Sentinel and Safety

`k_stype_val_sample` (the enum integer for `DDOG_PROF_SAMPLE_TYPE_SAMPLE`) is
used as a sentinel in `count_types[]` to mean "no count companion". The profile
creation code in `pprof_create_profile()` skips registering a count column when
it sees this value:

```cpp
if (count_t != static_cast<uint32_t>(DDOG_PROF_SAMPLE_TYPE_SAMPLE)) {
    w.pprof_indices[m].pprof_count_index = slots.ensure(count_t);
}
```

The integer values of all `k_stype_val_*` constants are verified against the
libdatadog enum at compile time via `static_assert` in `ddprof_pprof.cc`. If
libdatadog reorders or adds enum variants in a future release, these asserts
will fail and force a deliberate update of the constants and the string mappings
in `sample_type_name()`.

The `sample_type_name()` function in `ddprof_pprof.cc` (and the corresponding
`static_assert`s) cross-checks that the integer-to-string mapping matches the
strings the backend expects in the pprof wire format.
