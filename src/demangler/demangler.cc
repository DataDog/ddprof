// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "demangler/demangler.hpp"

#include <array>
#include <string>
#include <utility>

#include <llvm/Demangle/Demangle.h>

namespace ddprof {

namespace {
// With some exceptions we don't handle here, v0 Rust symbols can end in a
// prefix followed by a 16-hexdigit hash, which must be removed
constexpr std::string_view hash_pre = "::h";
constexpr std::string_view hash_eg = "0123456789abcdef";

inline bool is_hexdig(const char c) {
  return (c >= 'a' && c <= 'f') || (c >= '0' && c <= '9');
}

// Minimal check that a string can end, and does end, in a hashlike substring
// Some tools check for entropy, we do not.
inline bool has_hash(const std::string_view &str) {
  // If the size can't conform, then the string is invalid
  if (str.size() <= hash_pre.size() + hash_eg.size()) {
    return false;
  }

  // Check that the string contains the hash prefix in the right position
  if (str.compare(str.size() - hash_eg.size() - hash_pre.size(),
                  hash_pre.size(), hash_pre)) {
    return false;
  }

  // Check that the string ends in lowercase hex digits
  for (size_t i = str.size() - hash_eg.size(); i < str.size(); ++i) {
    if (!is_hexdig(str[i])) {
      return false;
    }
  }
  return true;
}

// Rather than performing a strict check (which would be necessary for
// supporting the use of '$' on some platforms as an informative token), this
// implementation makes a minimal check indicating that a string is likely to
// be a mangled Rust name.
bool is_probably_rust_legacy(const std::string_view &str) {
  // Is the string too short to have a hash part in thefirst place?
  if (!has_hash(str)) {
    return false;
  }

  // Throw out `$$` and `$????$`, but not in-between
  const char *ptr = str.data();
  const char *end = ptr + str.size() - hash_pre.size() - hash_eg.size();
  for (; ptr <= end; ++ptr) {
    if (*ptr == '$') {
      // NOLINTNEXTLINE: bugprone-branch-clone
      if (ptr[1] == '$') {
        return false;
      }
      return ptr[2] == '$' || ptr[3] == '$' || ptr[4] == '$';
    }
    if (*ptr == '.') {
      return '.' != ptr[1] ||
          '.' != ptr[2]; // '.' and '..' are fine, '...' is not
    }
  }
  return true;
}

// Simple conversion from hex digit to integer
// Includes capitals out of completeness, but this should not be necessary for
// the implementation
inline int hex_to_int(char dig) {
  constexpr int k_a_hex_value = 0xa;
  if (dig >= '0' && dig <= '9') {
    return dig - '0';
  }
  if (dig >= 'a' && dig <= 'f') {
    return dig - 'a' + k_a_hex_value;
  }
  if (dig >= 'A' && dig <= 'F') {
    return dig - 'A' + k_a_hex_value;
  }
  return -1;
}

// Demangles a Rust string by building a copy piece-by-piece
std::string rust_demangle(const std::string_view &str) {
  static constexpr auto patterns =
      std::to_array<std::pair<std::string_view, std::string_view>>({
          {"..", "::"},
          {"$C$", ","},
          {"$BP$", "*"},
          {"$GT$", ">"},
          {"$LT$", "<"},
          {"$LP$", "("},
          {"$RP$", ")"},
          {"$RF$", "&"},
          {"$SP$", "@"},
      });

  std::string ret;
  ret.reserve(str.size() - hash_eg.size() - hash_pre.size());

  size_t i = 0;

  // Special-case for repairing C++ demangling defect for Rust
  if (str[0] == '_' && str[1] == '$') {
    ++i;
  }

  for (; i < str.size() - hash_pre.size() - hash_eg.size(); ++i) {

    // Fast sieve for pattern-matching, since we know first chars
    if (str[i] == '.' || str[i] == '$') {
      bool replaced = false;

      // Try to replace one of the patterns
      for (const auto &pair : patterns) {
        const std::string_view &pattern = pair.first;
        const std::string_view &replacement = pair.second;
        if (!str.compare(i, pattern.size(), pattern)) {
          ret += replacement;
          i += pattern.size() - 1; // -1 because iterator inc
          replaced = true;
          break;
        }
      }

      // If we failed to replace, try a few failovers.  Notably, we recognize
      // that Rust may insert Unicode code points in the function name (other
      // implementations treat many individual points as patterns to search on)
      if (!replaced && str[i] == '.') {
        // Special-case for '.'
        ret += '-';
      } else if (!replaced && !str.compare(i, 2, "$u") && str[i + 4] == '$') {
        constexpr size_t k_nb_read_chars = 5;
        constexpr int hexa_base = 16;
        int const hi = hex_to_int(str[i + 2]);
        int const lo = hex_to_int(str[i + 3]);
        if (hi != -1 && lo != -1) {
          ret += static_cast<unsigned char>(lo + (hexa_base * hi));
          i += k_nb_read_chars - 1; // - 1 because iterator inc
        } else {
          // We didn't have valid unicode values, but we should still skip
          // the $u??$ sequence
          ret += str.substr(i, k_nb_read_chars);
          i += k_nb_read_chars - 1; // -1 because iterator inc
        }
      } else if (!replaced) {
        ret += str[i];
      }
    } else {
      ret += str[i];
    }
  }

  return ret;
}

} // namespace

// If it quacks like Rust, treat it like Rust
std::string Demangler::demangle(const std::string &mangled) {
  auto demangled = llvm::demangle(mangled);
  if (is_probably_rust_legacy(demangled)) {
    return rust_demangle(demangled);
  }
  return demangled;
}

std::string Demangler::non_microsoft_demangle(const char *mangled) {
  std::string res;
  if (llvm::nonMicrosoftDemangle(mangled, res)) {
    if (is_probably_rust_legacy(res)) {
      return rust_demangle(res);
    }
    return res;
  }
  return std::string{mangled};
}

} // namespace ddprof
