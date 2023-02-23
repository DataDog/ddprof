// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.
#pragma once

#include <stddef.h>

// This is a port from the libc implementations
// The aim is to guarantee that our library works even when depending on newer
// libc APIs (like this one). The symbol should be private not to change
// the behaviour of the application we are profiling
int getentropy(void *, size_t);
