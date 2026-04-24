// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#pragma once

#include <stddef.h>
#include <stdint.h>

// Compute SHA-256 of |data| (|len| bytes) into |out| (32 bytes).
void sha256(const unsigned char *data, size_t len, unsigned char out[32]);

// Convert a 32-byte hash to a 64-character hex string (plus NUL terminator).
void sha256_hex(const unsigned char hash[32], char out[65]);
