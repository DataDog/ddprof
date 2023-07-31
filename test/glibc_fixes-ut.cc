#include <atomic>
#include <csignal>
#include <gtest/gtest.h>
#include <unistd.h>

// assuming this variable is accessible from your test file
static std::atomic<bool> g_child_called;
static std::atomic<bool> g_parent_called;
static std::atomic<bool> g_prepare_called;

void prepare() { g_prepare_called = true; }

void parent() { g_parent_called = true; }

void child() { g_child_called = true; }

TEST(GlibcFixes, pthread_atfork) {
  g_child_called = false;
  g_parent_called = false;
  g_prepare_called = false;

  // register handlers
  EXPECT_EQ(0, pthread_atfork(prepare, parent, child));

  pid_t child_pid = fork();

  if (child_pid == 0) {
    // This is the child process
    EXPECT_TRUE(g_child_called);
    exit(0);
  } else {

    // validate that the handlers were called
    EXPECT_TRUE(g_prepare_called);
    EXPECT_TRUE(g_parent_called);

    waitpid(child_pid, nullptr, 0); // wait for child to finish
  }
}
