#pragma once

#include "ddres.h"
#include "proc_status.h"

#include <sys/types.h>

// Get internal stats from /proc/self/stat
DDRes proc_read(ProcStatus *);
