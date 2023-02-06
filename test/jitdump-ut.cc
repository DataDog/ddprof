#include <gtest/gtest.h>

#include "jit/jitdump.hpp"
#include "loghandle.hpp"

namespace ddprof {

// General algorithm
//
// 1) Get JITDump path
// Dso type will tell us that there is a JIT file.
// LLVM sources explain the logic about where we can find it. though we don't
// need that.
//
// We need to save the fact that we have a JIT file for this PID
// We have to store this information per PID. We will store the address
// At which the DSO is mmaped. This way we can check if it is still loaded.
//
// 2) Retrieve symbols
// Whenever we will come across the symbolisation of an unknown region
// Read the file. Save the size of the file
// Cache the symbols according to the addresses
// Create an associated mapping
//
//

// Where is file    ---------------------------
//
// ~/.debug/jit
// Looking at the sources
//
// if (const char *BaseDir = getenv("JITDUMPDIR"))
//  Path.append(BaseDir);
// else if (!sys::path::home_directory(Path))
//  Path = ".";
//
// create debug directory
// Path += "/.debug/jit/";
// if (auto EC = sys::fs::create_directories(Path)) {
//  errs() << "could not create jit cache directory " << Path << ": "
//         << EC.message() << "\n";
//  return false;
//}
//
// If I don't have a home ?
//
// when do you look for file ? (option ?)
//
// --- JIT option
// When do you update the JIT symbols ?
// - When the mmap changes ?
// - when you don't have a symbol
//

// Algorithm
// - Dso is read. It is associated with the type jitdump
// - We are on anonymous
//
TEST(JITTest, SimpleRead) {
  LogHandle handle;
  std::string jit_path =
      std::string(UNIT_TEST_DATA) + "/" + std::string("jit.dump");
  JITDump jit_dump;
  DDRes res = jit_read(jit_path, jit_dump);
  ASSERT_TRUE(IsDDResOK(res));
}
} // namespace ddprof
