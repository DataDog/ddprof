#include <coroutine>
#include <iostream>
#include <limits>
#include <string>

struct fibonacci_gen {
  struct promise_type {
    int current, next;
    std::string number_rep;
    char *buffer = {};
    auto get_return_object() { return fibonacci_gen{*this}; }

    auto initial_suspend() { return std::suspend_always{}; }

    auto final_suspend() { return std::suspend_always{}; }

    void return_void() {}

    void unhandled_exception() { std::exit(1); }

    auto yield_reset() {
      current = 0;
      next = 1;
      number_rep.clear();
      return std::suspend_always{};
    }

    auto yield_value(int value) {
      current = next;
      next = value;
      // std::cout << "OPE" << next << " " << current << " " <<
      // std::numeric_limits<int>::max() << std::endl; promoting to long long
      if (static_cast<long long int>(next) +
              static_cast<long long int>(current) >=
          std::numeric_limits<int>::max()) {
        // reset
        return yield_reset();
      }
      number_rep += " " + std::to_string(current);
      buffer = static_cast<char *>(realloc(buffer, number_rep.size() + 1));
      return std::suspend_always{};
    }
  };

  fibonacci_gen(promise_type &promise) : promise_(promise) {}

  bool operator()() {
    promise_.yield_value(promise_.next + promise_.current);
    return true;
  }

private:
  promise_type &promise_;
};

int main() {
  fibonacci_gen::promise_type promise;
  fibonacci_gen gen(promise);
  promise.next = 1;
  promise.current = 0;
  for (int i = 0; i < 500000; i++) {
    std::cout << promise.number_rep << std::endl;
    gen();
  }
  return 0;
}
