// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// SHA-256 test vectors from NIST FIPS 180-4.

extern "C" {
#include "sha256.h"
}

#include <gtest/gtest.h>

namespace {

std::string compute_sha256_hex(const char *data, size_t len) {
  unsigned char hash[32];
  sha256(reinterpret_cast<const unsigned char *>(data), len, hash);
  char hex[65];
  sha256_hex(hash, hex);
  return hex;
}

// NIST one-block message
TEST(Sha256Test, OneBlock) {
  EXPECT_EQ(compute_sha256_hex("abc", 3),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

// NIST two-block message
TEST(Sha256Test, TwoBlock) {
  EXPECT_EQ(compute_sha256_hex(
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

// Empty string
TEST(Sha256Test, Empty) {
  EXPECT_EQ(compute_sha256_hex("", 0),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

} // namespace
