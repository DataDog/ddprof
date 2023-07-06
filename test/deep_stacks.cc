// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <chrono>
#include <iostream>
#include <string>

#include "../include/ddprof_base.hpp"

template <int N> DDPROF_NOINLINE std::string compute() {
  char arr[N];

  for (int i = 0; i < N - 1; ++i) {
    arr[i] = 'a' + i % 26;
  }
  arr[N - 1] = '\0';

  std::string ret_arr(arr, N);
  if constexpr (N > 100) {
    ret_arr = ret_arr + compute<N - 100>();
  }
  return ret_arr;
}

int main() {
  using namespace std::chrono;
  auto start = high_resolution_clock::now();
  auto end = start + seconds(2); // set a time limit of 10 seconds
  while (high_resolution_clock::now() < end) {
    volatile auto str = compute<3000>();
    ddprof::DoNotOptimize(str);
    //    std::cout << str;
  }
  return 0;
}
