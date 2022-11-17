// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include "noisy_neighbors.hpp"

bool operator<(const ThreadFrame &A, const ThreadFrame &B) {
  if (A._method != B._method) return A._method < B._method;
  if (A._filename != B._filename) return A._filename < B._filename;
  if (A._class != B._class) return A._class < B._class;
  if (A._package != B._package) return A._package < B._package;
  return  false;
}
