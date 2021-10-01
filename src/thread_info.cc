#include "thread_info.hpp"

#include <iostream>
#include <thread>

namespace ddprof {
int get_nb_hw_thread() { return std::thread::hardware_concurrency(); }
} // namespace ddprof