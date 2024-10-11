// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2024-Present
// Datadog, Inc.

#pragma once

#include <type_traits>

namespace ddprof {

// Declaration of EnableBitMaskOperators trait
template <typename E> struct EnableBitMaskOperators : std::false_type {};

// Define the concept to check if bitmask operators are enabled for enum E
template <typename E>
concept EnableBitMaskOperatorsConcept =
    std::is_enum_v<E> && EnableBitMaskOperators<E>::value;
} // namespace ddprof

// Use the concept for operator overloading
template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E operator|(E lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) |
                        static_cast<underlying>(rhs));
}

template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E operator&(E lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) &
                        static_cast<underlying>(rhs));
}

template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E operator^(E lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) ^
                        static_cast<underlying>(rhs));
}

template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E operator~(E lhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(~static_cast<underlying>(lhs));
}

template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E &operator|=(E &lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  lhs = static_cast<E>(static_cast<underlying>(lhs) |
                       static_cast<underlying>(rhs));
  return lhs;
}

template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E &operator&=(E &lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  lhs = static_cast<E>(static_cast<underlying>(lhs) &
                       static_cast<underlying>(rhs));
  return lhs;
}

template <ddprof::EnableBitMaskOperatorsConcept E>
constexpr E &operator^=(E &lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  lhs = static_cast<E>(static_cast<underlying>(lhs) ^
                       static_cast<underlying>(rhs));
  return lhs;
}

// Macro to allow bitmask operators for a specific enum
#define ALLOW_FLAGS_FOR_ENUM(name)                                             \
  template <> struct ddprof::EnableBitMaskOperators<name> : std::true_type {};
