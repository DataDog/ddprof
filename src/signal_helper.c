// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "signal_helper.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

bool process_is_alive(int pidId) { return (-1 != kill(pidId, 0)); }
