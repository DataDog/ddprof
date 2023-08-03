// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0. This product includes software
// developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present
// Datadog, Inc.

#include <gtest/gtest.h>

#include <pthread.h>
#include <unistd.h>

constexpr int num_threads = 10;
static pthread_key_t key;
// +1 for main thread
static std::array<bool, num_threads + 1> is_set = {};

void *set_get_key(void *threadid) {
  long tid = *((long *)(threadid));
  void *ret_val = pthread_getspecific(key);
  EXPECT_EQ(ret_val, nullptr);
  pthread_setspecific(key, (void *)threadid);
  ret_val = pthread_getspecific(key);
  EXPECT_EQ(*(reinterpret_cast<long *>(ret_val)), tid);
  is_set[tid] = true;
  // Check that set does not clear
  ret_val = pthread_getspecific(key);
  EXPECT_EQ(*(reinterpret_cast<long *>(ret_val)), tid);
  return nullptr;
}
static std::array<long, num_threads> thread_ids;

TEST(PThreadTest, SetGetSpecific) {
  pthread_key_create(&key, nullptr);
  pthread_t threads[num_threads];

  // Create threads

  for (int i = 0; i < num_threads; ++i) {
    thread_ids[i] = i;
    pthread_create(&threads[i], nullptr, set_get_key, &(thread_ids[i]));
  }

  // Join threads
  for (auto &thread : threads) {
    pthread_join(thread, nullptr);
  }
  // check behaviour for main thread
  static long main_thread_id = num_threads;
  set_get_key(&main_thread_id);

  // Check if set for all threads
  for (bool val : is_set) {
    EXPECT_TRUE(val);
  }

  if (fork() == 0) {
    // Child process
    // Reset the set values
    for (bool &val : is_set) {
      val = false;
    }

    // Create and join threads again
    for (long i = 0; i < num_threads; ++i) {
      pthread_create(&threads[i], nullptr, set_get_key, &(thread_ids[i]));
    }
    for (auto &thread : threads) {
      pthread_join(thread, nullptr);
    }

    // Expect main thread to be set already
    void *ret_val = pthread_getspecific(key);
    EXPECT_EQ(*(reinterpret_cast<long *>(ret_val)), main_thread_id);
    is_set[main_thread_id] = true;

    // Check if set for all threads
    for (bool val : is_set) {
      EXPECT_TRUE(val);
    }
    _exit(0);
  } else {
    // Parent process
    // Wait for the child process to finish
    wait(nullptr);
  }
  pthread_key_delete(key);
}
