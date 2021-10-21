#include "llvm/Demangle/Demangle.h"

#include <gtest/gtest.h>

struct test_case {
  std::string test;
  std::string answer;
};

// Partly borrowed from the LLVM unit tests
std::vector<struct test_case> cases = {
    {"_", "_"},
    {"_Z3fooi", "foo(int)"},
    {"_RNvC3foo3bar", "foo::bar"},
    {"_ZN4llvm4yaml7yamlizeISt6vectorINSt7__cxx1112basic_stringIcSt11char_"
     "traitsIcESaIcEEESaIS8_EENS0_12EmptyContextEEENSt9enable_ifIXsr18has_"
     "SequenceTraitsIT_EE5valueEvE4typeERNS0_2IOERSD_bRT0_",
     "std::enable_if<has_SequenceTraits<std::vector<std::__cxx11::basic_string<"
     "char, std::char_traits<char>, std::allocator<char> >, "
     "std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, "
     "std::allocator<char> > > > >::value, void>::type "
     "llvm::yaml::yamlize<std::vector<std::__cxx11::basic_string<char, "
     "std::char_traits<char>, std::allocator<char> >, "
     "std::allocator<std::__cxx11::basic_string<char, std::char_traits<char>, "
     "std::allocator<char> > > >, llvm::yaml::EmptyContext>(llvm::yaml::IO&, "
     "std::vector<std::__cxx11::basic_string<char, std::char_traits<char>, "
     "std::allocator<char> >, std::allocator<std::__cxx11::basic_string<char, "
     "std::char_traits<char>, std::allocator<char> > > >&, bool, "
     "llvm::yaml::EmptyContext&)"},
    {"_ZWowThisIsWrong", "_ZWowThisIsWrong"},
};

#define BUF_LEN 1024
TEST(DemangleTest, Positive) {
  for (auto const &tcase : cases) {
    std::string demangled_func = llvm::demangle(tcase.test);
    EXPECT_EQ(demangled_func, tcase.answer);
  }
}
