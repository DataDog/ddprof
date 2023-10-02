// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <chrono>
#include <iostream>
#include <string>

#include "../include/ddprof_base.hpp"

static constexpr size_t k_work_amount = 3000;
static constexpr size_t k_work_amount_decrease_per_call = 100;

template <int N> DDPROF_NOINLINE std::string compute() {
  char arr[N];
  constexpr size_t k_nb_letters{26};

  for (int i = 0; i < N - 1; ++i) {
    arr[i] = 'a' + i % k_nb_letters;
  }
  arr[N - 1] = '\0';

  std::string ret_arr(arr, N);
  if constexpr (N > k_work_amount_decrease_per_call) {
    ret_arr = ret_arr + compute<N - k_work_amount_decrease_per_call>();
  }
  return ret_arr;
}

int main() {
  using namespace std::chrono;
  auto start = high_resolution_clock::now();
  auto end = start + seconds(2);
  while (high_resolution_clock::now() < end) {
    auto str = compute<k_work_amount>();
    ddprof::DoNotOptimize(str);
    //    std::cout << str;
  }
  return 0;
}
