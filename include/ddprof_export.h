#pragma once

#include <stddef.h>

#include "ddprof.h"
#include "ddprof_context.h"

DDRes export(DDProfContext *ctx, int64_t now);
