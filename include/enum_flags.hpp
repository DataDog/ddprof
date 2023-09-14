#pragma once

#include <type_traits>

namespace ddprof {
template <typename E> struct EnableBitMaskOperators : std::false_type {};
} // namespace ddprof

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator|(E lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) |
                        static_cast<underlying>(rhs));
}

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator&(E lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) &
                        static_cast<underlying>(rhs));
}

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator^(E lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(static_cast<underlying>(lhs) ^
                        static_cast<underlying>(rhs));
}

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator~(E lhs) {
  using underlying = std::underlying_type_t<E>;
  return static_cast<E>(~static_cast<underlying>(lhs));
}

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator|=(E &lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  lhs = static_cast<E>(static_cast<underlying>(lhs) |
                       static_cast<underlying>(rhs));
  return lhs;
}

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator&=(E &lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  lhs = static_cast<E>(static_cast<underlying>(lhs) &
                       static_cast<underlying>(rhs));
  return lhs;
}

template <typename E>
constexpr std::enable_if_t<ddprof::EnableBitMaskOperators<E>::value, E>
operator^=(E &lhs, E rhs) {
  using underlying = std::underlying_type_t<E>;
  lhs = static_cast<E>(static_cast<underlying>(lhs) ^
                       static_cast<underlying>(rhs));
  return lhs;
}

#define ALLOW_FLAGS_FOR_ENUM(name)                                             \
  template <> struct ddprof::EnableBitMaskOperators<name> : std::true_type {};
