// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include "scope.hpp"

namespace details {

struct DeferDummy {};

template <class F> ddprof::scope_exit<F> operator*(DeferDummy, F &&f) {
  return ddprof::scope_exit<F>{std::forward<F>(f)};
}

} // namespace details

template <class F> ddprof::scope_exit<F> make_defer(F &&f) {
  return ddprof::scope_exit<F>{std::forward<F>(f)};
}

#define DEFER_(LINE) zz_defer##LINE
#define DEFER(LINE) DEFER_(LINE)
#define defer                                                                  \
  [[maybe_unused]] const auto &DEFER(__COUNTER__) =                            \
      ::details::DeferDummy{} *[&]()
