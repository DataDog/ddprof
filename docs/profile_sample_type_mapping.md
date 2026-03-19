# Profile Sample Type Mapping

## Context

ddprof instruments the kernel via perf events and reports profiling data as
pprof profiles. Each pprof profile has a fixed set of **value columns**
(sample types), each identified by a `(type, unit)` string pair that the
Datadog backend uses to interpret the data (e.g. `cpu-time/nanoseconds`,
`alloc-space/bytes`).

libdatadog v29+ represents these as a `ddog_prof_SampleType` enum rather than
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
`k_stype_val_none` (`UINT32_MAX`) in either `sample_types[]` or `count_types[]`
signals "no type for this mode".

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
| Live (`kLiveSumPos`) | *(none)* | *(none)* |

CPU profiling is sum-mode only. Live mode is not a meaningful concept for CPU
time; both live slots are set to `k_stype_val_none` and no columns are
registered for that mode.

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
| Live (`kLiveSumPos`) | *(none)* | *(none)* |

Used by hardware performance counters (`hCPU`, `hREF`, `hINST`, …), software
events (`sPF`, `sCS`, …), and named kernel tracepoints (e.g.
`event=tlb:tlb_flush`). Each sample represents one event occurrence; there is
no count companion and no live mode.

> **Backend note**: prior to libdatadog v29, the `(tracepoint, events)` strings
> were passed as free-form values. In v29+, `DDOG_PROF_SAMPLE_TYPE_TRACEPOINT`
> is the canonical enum value. The integer value of this enum is verified at
> compile time in `ddprof_pprof.cc` via `static_assert`.

---

## Sentinel and Safety

`k_stype_val_none` (`UINT32_MAX`) is the sentinel for "no type for this
aggregation mode". It appears in both `sample_types[]` and `count_types[]`.
The profile creation code in `pprof_create_profile()` skips the entire mode
when `sample_types[m]` is the sentinel, and skips registering a count column
when `count_types[m]` is the sentinel:

```cpp
const uint32_t sample_t = w.sample_type_info.sample_types[m];
if (sample_t == k_stype_val_none) {
    continue;
}
w.pprof_indices[m].pprof_index = slots.ensure(sample_t);
const uint32_t count_t = w.sample_type_info.count_types[m];
if (count_t != k_stype_val_none) {
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
