# ddprof

Native profiler from Datadog

# Quick Start

We only start slowly around here.  Section pending :)

# Overview

*ddprof* is a commandline utility for engaging kernel-mediated telemetry of an application and forwarding the resulting information to the Datadog backend.  In several ways, it's similar to the `perf record` tool.  Currently, *ddprof* is limited to CPU profiling of ELF binaries equipped with debuginfo.


# Key Features

## Overhead

Full analysis of *ddprof* overhead is pending; but users of CPU-time profiling can rely on the following observations.  When there is sufficient computational headroom on the instance for *ddprof* to remain uncompetetive with the target workload (in other words, if the kernel CPU scheduler doesn't need to time-slice the target application against *ddprof*), expect *ddprof* to add less than 1% latency--usually less than 0.1%.  The precise definition of "enough" is workload dependent, but effort will be made to provide rule-of-thumb estimates once analysis has completed.


## Safety

Unlike runtime profilers, the native profiler requires no code modifications of the target service.  It doesn't direct signals at the target, use any `LD_PRELOAD` tricks, replace shared objects, or otherwise interfere with program execution at the process level once the target application has been launched. 

In particular:
* While segfaults and deadlocks can interrupt profiling, they do not propagate to the target application.  A future commit will offer auto-restart options for such cases.
* PID wrapper returns the PID of the target, rather than the PID of `ddprof`.  This is great when you are already running your target under a wrapper or if you're trying to wrap the init process of a PID namespace (as might be the case for containers).


# Docs

Architectural showpieces and such will always be available in the `docs/` folder.


# Prerequisites

In order to take advantage of *ddprof*, you need a few things

* Linux kernel 4.17 or later (if you need to support an earlier kernel, create an issue outlining your need!)
* Your desired application or libraries must have debuginfo.  This means they either have a `.eh_frame` or `.debug_info`.  *ddprof* will, but does not currently, support split debuginfo.
* Access to `perf events`.  See below.


## seccomp

By default, *seccomp* disables the `perf_event_open()` API.  You'll need to make sure you can access it.

## perf_event_paranoid

CPU profiling is available even with the strictest `perf_event_paranoid` mode offered by the Linux kernel--*ddprof* registers self-instrumentation for a process, which is always allowed (a process can look at its own stack and registers), then it steps out of the way.  However, it is possible that something like SELinux or AppArmor implements a further line of defense.  Detecting such configurations is currently outside of the scope of this document, but will be provided eventually.

Unfortunately, due to the rich and storied history of the perf events subsystem, (read:  it's been the originator of security bugs), some distros are shipped with a kernel patch that offers a `perf_event_paranoid == 3` configuration, which totally shuts down the entire interface (unless a process has `CAP_SYS_ADMIN` or possibly `CAP_PERFMON` on suitably recent kernels).  To use *ddprof* in such a scenario, you're going to have to run at a higher level of permission--either `CAP_SYS_ADMIN` (you can set a capability directly in Docker or through the container`securityContext`/`capabilities` in K8s) will have to be set or you'll need to figure out how to lower the `perf_event_paranoid` sysctl.


### Forward Considerations

In 2016, Debian and Android began running a kernel [patch](https://patchwork.kernel.org/project/kernel-hardening/patch/1469630746-32279-1-git-send-email-jeffv@google.com/) which implemented a new `perf_event_paranoid == 3` sysctl setting, which would totally disable the `perf_event_open()` syscall for processes without `CAP_SYS_ADMIN`.  This patch was contemporary with the emergence of at least four CVE (security issues) stemming from the `perf_event_open()` syscall (mostly around privileged data access).

This patch was rejected in fairly strong terms.  There were a few different themes in the thread.  Locking system-wide disablement behind `CAP_SYS_ADMIN` was seen as too big a hammer; on a system with the interface disabled, the only way to enable it is via a capability which widens the attack surface substantially (in kernel 5.4, there are hundreds of checks for `CAP_SYS_ADMIN`--which is also known as "root 2.0").  Moreover, from the perspective of kernel development, it's problematic to admit that an interface is unsafe and should ever be totally disabled; it's more scalable to either remove such features entirely or subject them to the testing necessary to ensure they are safe.

Given the historical status of `perf_event_open()`, one valid concern is, how safe is it to run `perf_event_paranoid == 2` in prod?  This is difficult to qualify fully, but one category of insights comes from the rate at which long-running targeted fuzzing campaigns succeed in finding new bugs.  There has been some [excellent work](http://web.eece.maine.edu/~vweaver/projects/perf_events/fuzzer/bugs_found.html) in this regard (see [here](http://web.eece.maine.edu/~vweaver/projects/perf_events/fuzzer/2019_perf_fuzzer_tr.pdf) as well).  The rate of serious issues and security-related bugs has decreased substantially during the v4 kernel series, and those issues have become increasingly specific.

With the v5.8 kernel, a [new capability](https://lwn.net/Articles/812719/]=) was added for granting heightened access to `perf_event_open()`.  Morevoer, [hooks](https://github.com/torvalds/linux/commit/da97e18458fb42d7c00fac5fd1c56a3896ec666e) for LSM have been added in v5.12, which will allow administrators even more granular control over the consumption of the subsystem.  Unfortunately, mainline LTS distros are barely using v5.4 at the time of writing, so it will be some time before either of these improvements has been fully standardized.  The question remains--in the tradeoff between security and observability, on a contemporary distro, when does it make sense to run `perf_event_paranoid < 3`?

My opinion?  If you're running a kernel dated from late 2017 or later, just enable `perf_events`.


#### Can we do better?

Possibly.

One idea would be to offer a new commandline argument to *ddprof*.  When this argument is set, detect `CAP_SYS_ADMIN` on startup, perform the instrumentation, then *drop* the capability in both the profiling daemon and the target application.  This will ensure that the instrumentation can be enabled, but denies attackers the benefit of actually using it.


# Things we don't know

* For a machine with many active processes, is there more overhead when we are instrumenting a minority or when we instrument a majority (from a single invocation)?
* When the machine is non-saturated, is the distribution of latency uniform over time (or perhaps some other measurable)?
