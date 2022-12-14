// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "version.hpp"

#include <cstdio>

void print_version() { printf(MYNAME " %s\n", str_version().data()); }
