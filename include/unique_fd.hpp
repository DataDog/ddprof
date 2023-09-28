// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <memory>
#include <unistd.h>

namespace ddprof {

template <typename T, T NullValue = T()> class Handle {
public:
  // cppcheck-suppress noExplicitConstructor
  Handle(std::nullptr_t) {}
  // cppcheck-suppress noExplicitConstructor
  Handle(T x) : _val(x) {}
  Handle() = default;

  explicit operator bool() const { return _val != NullValue; }
  operator T() const { return _val; }

  T get() const { return _val; }

  friend bool operator==(Handle a, Handle b) = default;

private:
  T _val{NullValue};
};

using FdHandle = Handle<int, -1>;

template <typename Handle, typename Deleter> struct HandleDeleter {
  using pointer = Handle;
  void operator()(pointer p) { Deleter{}(p); }
};

inline constexpr auto fdclose = [](auto fd) { ::close(fd); };

using UniqueFd =
    std::unique_ptr<int, HandleDeleter<FdHandle, decltype(fdclose)>>;

} // namespace ddprof
