// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once
#include "ddres_def.h"

typedef struct DDProfInput DDProfInput;
typedef struct DDProfContext DDProfContext;

/***************************** Context Management *****************************/
DDRes ddprof_context_set(const DDProfInput *input, DDProfContext *);
void ddprof_context_free(DDProfContext *);
