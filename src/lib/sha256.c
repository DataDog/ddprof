// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

// Minimal SHA-256 implementation (based on RFC 6234 / FIPS 180-4).
// No external dependencies beyond standard C headers.

#include "sha256.h"

#include <string.h>

static const uint32_t k_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

#define RR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define S0(x) (RR(x, 2) ^ RR(x, 13) ^ RR(x, 22))
#define S1(x) (RR(x, 6) ^ RR(x, 11) ^ RR(x, 25))
#define s0(x) (RR(x, 7) ^ RR(x, 18) ^ ((x) >> 3))
#define s1(x) (RR(x, 17) ^ RR(x, 19) ^ ((x) >> 10))
#define CH(x, y, z) (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static void sha256_transform(uint32_t state[8], const unsigned char block[64]) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = (uint32_t)block[i * 4] << 24 | (uint32_t)block[i * 4 + 1] << 16 |
        (uint32_t)block[i * 4 + 2] << 8 | (uint32_t)block[i * 4 + 3];
  }
  for (int i = 16; i < 64; i++) {
    w[i] = s1(w[i - 2]) + w[i - 7] + s0(w[i - 15]) + w[i - 16];
  }
  uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
  uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t t1 = h + S1(e) + CH(e, f, g) + k_sha256_k[i] + w[i];
    uint32_t t2 = S0(a) + MAJ(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

void sha256(const unsigned char *data, size_t len, unsigned char out[32]) {
  uint32_t state[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  size_t i;
  for (i = 0; i + 64 <= len; i += 64) {
    sha256_transform(state, data + i);
  }
  // Padding
  unsigned char block[64];
  size_t rem = len - i;
  memcpy(block, data + i, rem);
  block[rem++] = 0x80;
  if (rem > 56) {
    memset(block + rem, 0, 64 - rem);
    sha256_transform(state, block);
    rem = 0;
  }
  memset(block + rem, 0, 56 - rem);
  uint64_t bits = (uint64_t)len * 8;
  for (int j = 7; j >= 0; j--) {
    block[56 + (7 - j)] = (unsigned char)(bits >> (j * 8));
  }
  sha256_transform(state, block);
  for (int j = 0; j < 8; j++) {
    out[j * 4] = (unsigned char)(state[j] >> 24);
    out[j * 4 + 1] = (unsigned char)(state[j] >> 16);
    out[j * 4 + 2] = (unsigned char)(state[j] >> 8);
    out[j * 4 + 3] = (unsigned char)(state[j]);
  }
}

void sha256_hex(const unsigned char hash[32], char out[65]) {
  static const char hex[] = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[i * 2] = hex[hash[i] >> 4];
    out[i * 2 + 1] = hex[hash[i] & 0xf];
  }
  out[64] = '\0';
}
