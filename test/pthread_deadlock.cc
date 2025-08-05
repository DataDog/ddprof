#include <thread>

namespace {
void func() {
  pthread_attr_t attrs;
  pthread_getattr_np(pthread_self(), &attrs);
  pthread_attr_destroy(&attrs);
}
} // namespace

int main() {
  std::thread t(func);
  t.join();
}
