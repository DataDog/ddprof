# Design

Overview of the `ddprof` architecture

## Architecture

`ddprof` is a sample based profiler. It uses a mix of user space instrumentation and kernel APIs.
`ddprof` runs in a separate process and processes events through shared ring buffers.

![wrapper_architecture.png](wrapper_architecture.png)

## IPC communication

Library communicates with profiler through a unix socket.
Socket creation is the responsibility of the profiler.
By default ddprof creates an random abstract socket, that does not exist on
the filesystem: "\0/tmp/ddprof-<pid>-<random_string>.sock" but unix socket
path can be overridden with profiler --socket input option.
Profiler worker process accepts and handles connections on this socket in a
separate thread and sends ring buffer information upon request.

Overview of the communication process (wrapper mode):
 * Profiler starts, set `DD_PROFILING_NATIVE_LIB_SOCKET` env variable with
   socket path and daemonizes.
 * Target process starts and blocks on intermediate process termination
 * Profiler creates socket and listen on it, and then unblock target
   process by killing intermediate process.
 * Profiler forks into worker process and worker process starts accepting
   connections.
 * Library get socket path from `DD_PROFILING_NATIVE_LIB_SOCKET`, connects
   to it, retrieve ring buffer information and closes connection.
 * Exec'd processes from target process inherit
 `DD_PROFILING_NATIVE_LIB_SOCKET`
   env variable and connect to profiler in the same manner.
Overview of the communication process (library mode):
 * Library starts, checks if `DD_PROFILING_NATIVE_LIB_SOCKET` is set.
 * If `DD_PROFILING_NATIVE_LIB_SOCKET` is set:
   * Library connects to socket, retrieve ring buffer information and
     closes connection.
 * Otherwise:
   * Library daemonizes into the profiler.
   * Profiler blocks on intermediate process termination.
   * Library unblocks profiler by killing intermediate process.
   * Library blocks on reading socket path from pipe created during
    daemonization.
   * Profiler starts, creates socket and listen on socket.
   * Profiler creates socket, listen on it and then sends socket path to
     library through pipe created during daemonization.
     Pipe allows library to retrieve socket path from profiler and to
     ensure that library tries to connect to socket after it has been
     created by profiler.
   * Library connects to socket, retrieve ring buffer information and
     closes connection.
   * Library sets environment variable `DD_PROFILING_NATIVE_LIB_SOCKET`
     with socket path so that socket is used by future exec'd processes.
   * Exec'd processes from target process inherit
     `DD_PROFILING_NATIVE_LIB_SOCKET` env variable and connect to profiler
     in the same manner as in wrapper mod
