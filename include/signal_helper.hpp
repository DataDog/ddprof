// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <cstddef>
#include <cstdint>

#include <signal.h>

namespace ddprof {
bool process_is_alive(int pidId);

int convert_addr_to_string(uintptr_t ptr, char *buff, size_t buff_size);

void sigsegv_handler(int sig, siginfo_t *si, void *uc);

} // namespace ddprof
