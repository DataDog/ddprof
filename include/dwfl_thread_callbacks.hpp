// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "dwfl_internals.hpp"
#include <dwarf.h>

pid_t next_thread(Dwfl *, void *, void **);
bool set_initial_registers(Dwfl_Thread *, void *);
bool memory_read_dwfl(Dwfl *dwfl, Dwarf_Addr addr, Dwarf_Word *result,
                      int regno, void *arg);
