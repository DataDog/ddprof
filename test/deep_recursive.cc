#include <iostream>
#include <pthread.h>
#include <thread>

struct ThreadBounds {
  const std::byte *_start;
  const std::byte *_end;
};

thread_local ThreadBounds _tl_bounds;

// Return stack end address (stack end address is the start of the stack since
// stack grows down)
int retrieve_stack_bounds(const std::byte *&start, const std::byte *&end) {
  void *stack_addr;
  size_t stack_size;
  pthread_attr_t attrs;
  int exit_code = 0;
  if (pthread_getattr_np(pthread_self(), &attrs) != 0) {
    exit_code = -1;
    goto clean_attr_exit;
  }

  if (pthread_attr_getstack(&attrs, &stack_addr, &stack_size) != 0) {
    exit_code = -1;
    goto clean_attr_exit;
  }
  start = static_cast<std::byte *>(stack_addr);
  end = static_cast<std::byte *>(stack_addr) + stack_size;
clean_attr_exit:
  pthread_attr_destroy(&attrs);
  return exit_code;
}

void deep_recursive_call(int depth) {
  if (depth == 0) {
    return;
  }
  deep_recursive_call(depth - 1);

  ThreadBounds prev = _tl_bounds;
  retrieve_stack_bounds(_tl_bounds._start, _tl_bounds._end);
  if (prev._start != nullptr && prev._start != _tl_bounds._start) {
    printf("Bounds -- %p : %p \n ", _tl_bounds._start, _tl_bounds._end);
    exit(0);
  }
}

int main() {
  const int depth = 100000000;
  //  std::thread t1(deep_recursive_call, depth);
  //  std::thread t2(deep_recursive_call, depth);
  //  std::thread t3(deep_recursive_call, depth);
  //  t1.join();
  //  t2.join();
  //  t3.join();
  deep_recursive_call(100000000);
  std::cout << "Recursive call completed" << std::endl;
  return 0;
}
