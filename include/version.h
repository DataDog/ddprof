// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "string_view.h"

// Name and versions are defined in build system
#ifndef MYNAME
#  define MYNAME "ddprof"
#endif

#ifndef VER_MAJ
#  define VER_MAJ 0
#endif
#ifndef VER_MIN
#  define VER_MIN 0
#endif
#ifndef VER_PATCH
#  define VER_PATCH 0
#endif
#ifndef VER_REV
#  define VER_REV "custom"
#endif

/// Versions are updated in cmake files
string_view str_version();

void print_version();
