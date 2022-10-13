// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h> // size_t
#include <stdint.h> // uint64_t

typedef struct PerfWatcher PerfWatcher;

/**************************** Cmdline Helpers *********************************/
// Helper functions for processing commandline arguments.
//
// Note that `arg_yesno(,1)` is not the same as `!arg(,0)` or vice-versa.  This
// is mostly because a parameter whose default value is true needs to check
// very specifically for disablement, but the failover is to retain enable
//
// That said, it might be better to be more correct and only accept input of
// the specified form, returning error otherwise.

/// Returns index to element that compars to str, otherwise -1
int arg_which(const char *str, char const *const *set, int sz_set);

bool arg_inset(const char *str, char const *const *set, int sz_set);

bool arg_yesno(const char *str, int mode);

bool watcher_from_event(const char *str, PerfWatcher *watcher);
bool watcher_from_tracepoint(const char *str, PerfWatcher *watcher);

long id_from_tracepoint(const char *gname, const char *tname);
