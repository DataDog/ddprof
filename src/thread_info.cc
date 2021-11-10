// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "thread_info.hpp"

#include <iostream>
#include <thread>

namespace ddprof {
int get_nb_hw_thread() { return std::thread::hardware_concurrency(); }
} // namespace ddprof