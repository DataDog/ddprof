// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "procutils.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "logger.h"

static char StatusLine[] =
    "%d %s %c %d %d %d %d %u %lu %lu %lu %lu %lu %ld %ld %ld %ld %ld %ld "
    "%llu %lu %ld %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %d %d %u "
    "%u %llu %lu %ld %lu %lu %lu %lu %lu %lu %d";

DDRes proc_read(ProcStatus *procstat) {
  FILE *ststream = fopen("/proc/self/stat", "r");
  if (!ststream) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PROCSTATE, "Failed to open /proc/self/stat");
  }

  if (0 > fscanf(ststream, StatusLine, &procstat->pid, &procstat->comm,
                 &procstat->state, &procstat->ppid, &procstat->pgrp,
                 &procstat->session, &procstat->tty_nr, &procstat->tpgid,
                 &procstat->flags, &procstat->minflt, &procstat->cminflt,
                 &procstat->majflt, &procstat->cmajflt, &procstat->utime,
                 &procstat->stime, &procstat->cutime, &procstat->cstime,
                 &procstat->priority, &procstat->nice, &procstat->num_threads,
                 &procstat->itrealvalue, &procstat->starttime, &procstat->vsize,
                 &procstat->rss, &procstat->rsslim, &procstat->startcode,
                 &procstat->endcode, &procstat->startstack, &procstat->kstkesp,
                 &procstat->kstkeip, &procstat->signal, &procstat->blocked,
                 &procstat->sigignore, &procstat->sigcatch, &procstat->wchan,
                 &procstat->nswap, &procstat->cnswap, &procstat->exit_signal,
                 &procstat->processor, &procstat->rt_priority,
                 &procstat->policy, &procstat->delayacct_blkio_ticks,
                 &procstat->guest_time, &procstat->cguest_time,
                 &procstat->start_data, &procstat->end_data,
                 &procstat->start_brk, &procstat->arg_start, &procstat->arg_end,
                 &procstat->env_start, &procstat->env_end)) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_PROCSTATE, "Failed to read /proc/self/stat");
  }
  fclose(ststream);
  return ddres_init();
}

bool check_file_type(const char *pathname, int file_type) {
  struct stat info;

  if (stat(pathname, &info) != 0) {
    return false;
  } else if (info.st_mode & file_type) {
    return true;
  }
  return false;
}

bool get_file_inode(const char *pathname, inode_t *inode, int64_t *size) {
  struct stat info;

  if (stat(pathname, &info) != 0) {
    *inode = 0;
    *size = 0;
    return false;
  } else {
    *inode = info.st_ino;
    *size = info.st_size;
    return true;
  }
}
