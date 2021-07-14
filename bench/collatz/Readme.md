# Collatz

*Collatz* is a benchmarking tool for native profilers.  It spawns a configurable number of processes which traverse a data-driven callgraph consisting of 1000 native functions.  Call depth can vary, with some modes exceeding 1000 calls.  Each function consists of small timing code and not much else.

## Scope

I expect the primary use of *collatz* will be performance tuning the *dd-prof* native profiler.  That said, one of the main questions fielded by folks selling software is, "how does it compare to \[competing product\]?"  As such, I tried to come up with a framing that would allow me to quantify--even roughly--the user experience of running a process under different levels of instrumentation, sources-unseen.

## In practice

From a resource perspective, many important CPU consumables are shared between processes; it's close to impossible to run truly isolated workloads.  Inferring even just the important ones (such as cache hit ratio, branches, instructions per cycle, etc) is beyond the scope of this tool.  Rather, this tool attempts to report two derived quantities--latency and overhead--which can be useful in framing the end-user experience when engaging a profiler.

Note that timing is conducted using the x86 TSC hardware.  On "modern" x86 (TODO: find the microarch where this shifted from real to reference cycles), this counter increments at roughly the rate of non-adjusted (e.g., as would happen via frequency scaling) cycles.  Since this is a virtualized counter, there is a little bit of variability (assumption:  the CPU has the interpolate to a known timescale), but we assume this variability is well-handled over the course of long timing runs.  All times are given without subtracting the overhead of the timer itself; please don't use these results to infer dynamic instruction counts.

### Latency

For a fixed amount of work, latency is the amount of extra time that work takes when profiling is enabled, but there is enough capacity on the CPU for the profiler and *collatz* to get scheduled simultaneously (if possible).  Sources of latency are things like pausing a target process via `ptrace` (as is the case with tools like cachegrind or ltrace), additional time spent in signal-handlers, or context switching.

### Overhead

Roughly speaking, overhead is the total CPU consumption per unit time by the profiler for its own internal requirements.  This can be difficult to ascertain in a vacuum, but it's possible to approximate with a variable workload.  Note that the term "CPU consumption" here is left totally ambiguous and we do not mean anything specific by it (especially not cycles or instructions).

One way of measuring overhead is to run a variable workload with and without profiling enabled.  Without profiling enabled, scale the workload from a single core past the total virtual CPU count of the target system.  For collatz, ticks-per-cycle should remain flat (sans the two elbows, discussed below) until saturation has occurred, at which point ticks-per-cycle should be roughly linear with the number of processes.  For a baremetal system without many administrative tasks running, this saturation point will probably be around the point where `workers > schedulable_cores - 1` or so.  Then enable the profiler and run the workload in the same way.

When a workload hits an inflection point in how it needs to consume the underlying hardware resources (e.g., a change in the behavior of the kernel scheduler or the exhaustion of a limited resource), a graph of `processes x ticks-per-cycle` will show an elbow.  The author expects two elbows to form for collatz--one when the scheduler starts scheduling new work on top of underloaded, but not idle, cores (i.e., when the worker count hits the physical CPU count) and one when the system has hit its limit of schedulable work (after which, the graph should convert to linear from flat).

## Analysis

For a given profiler, the Very Interesting observations would be

- elbow migration; does it move the elbows down the graph?
- asymptotic changes; does the graph become linear after the first elbow?
- fixed changes; is the graph shifted upward?
- class iv paracausal abnormalities; does the profiler parse \[X\]HTML with regex?

## Discussion

*collatz* probably overemphasizes overhead due to instruction pipelining.  Each process:

- Reads and writes its own memory
- Exercises timer code only before/after profiling

so I don't think we're doing something like profiling stalls on loads or witnessing the effects of serialized execution via serializing memory transactions.  That said, I just realized I erroneously emit an atomic increment for aggregating per-process counters--I don't think it matters, but maybe it does.

Anyway, the point is that no analysis was done to infer exactly what *collatz* is spending its time doing, so while this tool is probably useful for analysis-in-the-large, it isn't suitable for figuring out how a given profiler will affect a heavily tuned workload.
