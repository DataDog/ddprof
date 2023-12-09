#include "ebpf_sample_handler.h"

#ifdef IFIXEDTHIS
// we should extract the sample handler
#include <stdio.h>

#include <vmlinux.h>
#include "bpf/sample_processor.h"
//#  include <bpf/bpf.h>
//#  include <bpf/libbpf.h>
//
//#include <bpf/bpf_helpers.h>
//#include <bpf/bpf_tracing.h>

int sample_handler(void *_ctx, void *data, size_t size) {
  stacktrace_event *event = reinterpret_cast<stacktrace_event *>(data);

  if (event->kstack_sz <= 0 && event->ustack_sz <= 0)
    return 1;

  fprintf(stderr, "COMM: %s (pid=%d) @ CPU %d\n",
          event->comm,
          event->pid,
          event->cpu_id);

  if (event->kstack_sz > 0) {
    fprintf(stderr, "Kernel:\n");
  } else {
    fprintf(stderr, "No Kernel Stack\n");
  }

  if (event->ustack_sz > 0) {
    fprintf(stderr, "Userspace:\n");
  } else {
    fprintf(stderr, "No Userspace Stack\n");
  }

  fprintf(stderr, "\n");
  return 0;
}
#endif
