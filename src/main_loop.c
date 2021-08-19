#include "main_loop.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ddres.h"
#include "logger.h"
#include "pevent_lib.h"
#include "unwind.h"

#define rmb() __asm__ volatile("lfence" ::: "memory")

#define WORKER_SHUTDOWN() exit(0)

void ddres_check_or_shutdown(DDRes res) {
  if (IsDDResNotOK(res)) {
    LG_WRN("[PERF] Shut down worker (error=%d).", res._what);
    WORKER_SHUTDOWN();
  }
}

void ddres_graceful_shutdown(void) {
  LG_NTC("Shutting down worker gracefully");
  WORKER_SHUTDOWN();
}

static DDRes worker_init(PEventHdr *pevent_hdr, UnwindState *us) {
  // If we're here, then we are a child spawned during the previous operation.
  // That means we need to iterate through the perf_event_open() handles and
  // get the mmaps
  DDRES_CHECK_FWD(pevent_mmap(pevent_hdr));
  return ddres_init();
}

static DDRes worker_free(PEventHdr *pevent_hdr, UnwindState *us) {
  unwind_free(us);
  DDRES_CHECK_FWD(pevent_munmap(pevent_hdr));
  return ddres_init();
}

void main_loop(PEventHdr *pevent_hdr, perfopen_attr *attr, DDProfContext *arg) {
  int pe_len = pevent_hdr->size;
  struct pollfd pfd[MAX_NB_WATCHERS];
  PEvent *pes = pevent_hdr->pes;
  assert(attr->msg_fun);
  UnwindState *us = arg->us;

  // Setup poll() to watch perf_event file descriptors
  for (int i = 0; i < pe_len; i++) {
    // NOTE: if fd is negative, it will be ignored
    pfd[i].fd = pes[i].fd;
    pfd[i].events = POLLIN | POLLERR | POLLHUP;
  }

  // Handle the processing in a fork, so we can clean up unfreeable state.
  // TODO we probably lose events when we switch workers.  It's only a blip,
  //      but it's still slightly annoying...
  pid_t child_pid;
  volatile bool *continue_profiling =
      mmap(0, sizeof(bool), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED,
           -1, 0);

  // If the malloc fails, then try to profile without resetting the worker
  if (!continue_profiling) {
    LG_ERR("[PERF] Could not initialize worker process coordinator, profiling "
           "will probably fail");
  } else {
    // ## Respawn point for workers ##
    while ((child_pid = fork())) {
      LG_WRN("[PERF] Created child %d", child_pid);
      waitpid(child_pid, NULL, 0);

      // Harvest the exit state of the child process.  We will always reset it
      // to false so that a child who segfaults or exits erroneously does not
      // cause a pointless loop of spawning
      if (!*continue_profiling) {
        LG_WRN("[PERF] Stop profiling!");
        attr->finish_fun(arg, true);
        return;
      } else {
        attr->finish_fun(arg, false);
        *continue_profiling = false;
      }
      LG_NTC("[PERF] Refreshing worker process");
    }
  }

  // Init new worker objects
  ddres_check_or_shutdown(worker_init(pevent_hdr, us));

  // Perform user-provided initialization
  ddres_check_or_shutdown(attr->init_fun(arg));

  // Worker poll loop
  while (1) {
    int n = poll(pfd, pe_len, PSAMPLE_DEFAULT_WAKEUP);

    // If there was an issue, return and let the caller check errno
    if (-1 == n && errno == EINTR)
      continue;
    else if (-1 == n)
      ddres_check_or_shutdown(ddres_error(DD_WHAT_UKNW));

    // If no file descriptors, call time-out
    if (0 == n && attr->timeout_fun) {

      // We don't return from here, only exit, since we don't want to log
      // shutdown messages (if we're shutting down, the outer process will
      // emit those loglines)
      DDRes res = attr->timeout_fun(continue_profiling, arg);
      if (IsDDResNotOK(res)) {
        worker_free(pevent_hdr, us);
        ddres_check_or_shutdown(res);
      }

      // If we didn't have to shut down, then go back to poll()
      continue;
    }

    for (int i = 0; i < pe_len; i++) {
      if (!pfd[i].revents)
        continue;
      if (pfd[i].revents & POLLHUP) {
        worker_free(pevent_hdr, us);
        ddres_graceful_shutdown();
      }
      // Drain the ringbuffer and dispatch to callback, as needed
      // The head and tail are taken literally (without wraparound), since they
      // don't wrap in the underlying object.  Instead, the rb_* interfaces
      // wrap when accessing.
      uint64_t head = pes[i].region->data_head;
      rmb();
      uint64_t tail = pes[i].region->data_tail;
      RingBuffer *rb = &(RingBuffer){0};
      rb_init(rb, pes[i].region, pes[i].reg_size);

      while (head > tail) {
        struct perf_event_header *hdr = rb_seek(rb, tail);
        if ((char *)pes[i].region + pes[i].reg_size < (char *)hdr + hdr->size) {
          // LG_WRN("[UNWIND] OUT OF BOUNDS");
        } else {
          // Same deal as the call to timeout_fun
          DDRes res = attr->msg_fun(hdr, pes[i].pos, continue_profiling, arg);
          if (IsDDResNotOK(res)) {
            worker_free(pevent_hdr, us);
            ddres_check_or_shutdown(res);
          }
        }
        tail += hdr->size;
      }

      // We tell the kernel how much we read.  This *should* be the same as
      // the current tail, but in the case of an error head will be a safe
      // restart position.
      pes[i].region->data_tail = head;

      if (head != tail)
        LG_NTC("Head/tail buffer mismatch");
    }
  }
}
