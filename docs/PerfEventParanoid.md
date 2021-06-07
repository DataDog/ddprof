# perf_event_paranoid == 3

This is a brief discussion on the `perf_event_paranoid == 3` setting--where did it come from and where do we go from here.

## Context

CPU profiling is available even with the strictest `perf_event_paranoid` mode offered by the Linux kernel.  Unfortunately, some distros (notably, Ubuntu) take it a step further.

With `perf_event_paranoid == 3`, *seccomp* disables the `perf_event_open()` API.

## History

In 2016, Debian and Android began running a kernel [patch](https://patchwork.kernel.org/project/kernel-hardening/patch/1469630746-32279-1-git-send-email-jeffv@google.com/) which implemented a new `perf_event_paranoid == 3` sysctl setting, intended to disable the `perf_event_open()` syscall for processes without `CAP_SYS_ADMIN`.  By comparison, the strictest kernel default, `perf_event_paranoid == 2` disallows access to CPU (hardware), ftrace, and kernel events.   In the patch notes, the author cites five CVEs (contemporary to the patch, in mid-2016) stemming from `perf_event_open()` and observes that although `perf_event_open()` is useful for development, it is not valuable for production systems.  In a subsequent patch, the same author proposed making this new setting the system default.  To this day, Debian builds (including Ubuntu) and Android run this patch.  In the case of Debian, it may be meaningful to note that the aforementioned CVEs were specific to Android on Qualcomm ARM CPUs; the nature of all five CVEs were dependent upon the underlying device.

This patch was proposed for integration into the mainline kernel, but rejected in fairly strong terms.  There were a few different themes in the thread.  Locking system-wide disablement behind `CAP_SYS_ADMIN` was seen as too big a hammer; on a system with the interface disabled, the only way to enable it is via a capability which widens system-wide attack surface substantially (a quick grep of kernel 5.4 reveals hundreds of individual checks for `CAP_SYS_ADMIN`).  Moreover, from the perspective of kernel development, it's problematic to admit that an interface is unsafe and should ever be disabled when the underlying subsystem can't be removed.

Note that, possibly as a result of the widespread adoption of the `perf_event_paranoid == 3` default (or perhaps, due to the same or similar considerations), Docker disables the syscall in seccomp, as does other tools in their own ways.  Merely addressing `perf_event_paranoid == 3` may not be sufficient to enable perf events in an arbitrary setting, but it is at least one of the most durable such settings with the most unfortunate side-effects in attempting to undo at the container level.

## Discussion

Personally, I can recognize the value of disabling `perf_event_open()` on Android, given the nature of the distribution.  The interesting question is, what are the tradeoffs on contemporary server-type systems?

### Utility

The subsystem accessed by `perf_event_open()` has a wide variety of uses in harvesting process- and system-level statistics, sampling processes, and originating system side-effects to userspace code.  Many of these uses are entirely unique to `perf_event_open()` (well, on Linux [at least](https://www.amazon.com/Solaris-Performance-Tools-Techniques-OpenSolaris/dp/0131568191)) and when not unique, they are faster or more feature-rich through this interface than on others (for instance, time-based sampling as per `setitimer()`).  Moreover, `perf_event_open()` is a convenient hook for event-type eBPF programs.  The most popular frontend to this interface is the (perf CLI)[https://perf.wiki.kernel.org/index.php/Main_Page] (which is, at the time of writing, the only userspace application shipped as part of the Linux kernel source tree), which has been given [excellent](http://www.brendangregg.com/perf.html), if not [exhaustive](http://www.brendangregg.com/systems-performance-2nd-edition-book.html), treatment by [Brendan Gregg](http://www.brendangregg.com/).  Although a great deal of the discussion on `perf` centers on accessing hardware counters (and those uses *are* incredibly compelling), there remain an enormous number of other uses, even when `perf_event_paranoid == 2` is in place and elevation to `CAP_SYS_ADMIN` is unavailable.

#### ddprof

By way of example, consider what `ddprof` is capable of offering.

* wrapper-type invocation, `ddprof my_executable`, allows instrumentation without code modification
* returns the PID of the instrumented process, rather than the wrapper--extremally, `gdb --args ddprof my_executable` instruments `my_executable` and *just works*
* no signals, modification of signal disposition, or libc overrides in user code
* supports extremely high sampling rates compared to `itimer`-based profilers

### Safety

If the default stance is to disable the interface entirely, it must be pretty unsafe, right?  In particular, it must imply `perf_event_paranoid == 2` is unsafe to run in production settings.  Maybe not.  Let's discuss.

#### Documentation

Documentation available [here](https://www.kernel.org/doc/html/latest/admin-guide/perf-security.html).  Note that the discussion on safety implications is largely focused on the exposure of PMUs/MSR.  These are CPU hardware which (rather famously) are typically not exposed to end-users by vendors of multi-tenant VM products (i.e., cloud vendors) except when a full CPU socket is isolated to a single instance (and even then, not on many public clouds).

#### Fuzzing

A standard tool in ensuring the safety and reliability of kernel interfaces is to subject subsystems to long-running fuzzing campaigns.  perf events has been challenged with such campaigns since 2016, and some excellent work has come from the adoption of this discipline.  The rate of defects discovered from long-running fuzzing has [decreased](http://web.eece.maine.edu/~vweaver/projects/perf_events/fuzzer/bugs_found.html) over time.  See also [here](http://web.eece.maine.edu/~vweaver/projects/perf_events/fuzzer/2019_perf_fuzzer_tr.pdf) for framing and analysis of the fuzzing strategy.

#### Defect Rate

CVEs are still published against `perf_event_open()`, but they are increasingly low- or [medium-severity](https://nvd.nist.gov/vuln/detail/CVE-2020-25704) vulnerabilities concerned with DoSing the subsystem itself (and even then, only in uncommon modes).  We do still see [high-severity](https://nvd.nist.gov/vuln/detail/CVE-2020-14351) CVEs emerge, but are *use-after-free* defects which would not grant an attacker any marginal leverage when running `perf_event_paranoid == 2` (and perhaps nothing of value at even lower modes; but this is unsubstantiated speculation).  Fortunately, defects have been quickly observed and repaired

## Improvement ideas

### PROF_PERFMON

With the v5.8 kernel, a [new capability](https://lwn.net/Articles/812719/]=) was added for granting heightened access to `perf_event_open()`, despite the underlying `perf_event_paranoid` level.

### AppArmor/LSM

[hooks](https://github.com/torvalds/linux/commit/da97e18458fb42d7c00fac5fd1c56a3896ec666e) for LSM have been added in v5.12, which will allow administrators even more granular control over the consumption of the subsystem.  Unfortunately, mainline LTS distros are barely using v5.4 at the time of writing, so it will be some time before either of these improvements has been fully standardized.

### Capability Downgrade

One idea would be to offer a new commandline argument to *ddprof*.  When this argument is set, detect `CAP_SYS_ADMIN` on startup, perform the instrumentation, then *drop* the capability in both the profiling daemon and the target application.  This will ensure that the instrumentation can be enabled, but denies attackers the benefit of using it directly.

### Sidecar Mode

In this distribution, the customer would install `ddprof` as a daemon in a sidecar container.  We'd have to implement a new mode which will watch all processes on all CPUs, which has some issues for finding mappings and debug symbols (but nothing major).  I don't anticipate this to add more latency, but it may erode performance by adding a lot of noise, which will add garbage to our unwinding caches.
