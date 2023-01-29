#include "jit/jitdump.hpp"

#include "logger.hpp"
#include <fstream>


namespace  ddprof {
DDRes jit_read(std::string_view file) {
  std::ifstream file_stream(file.data(), std::ios::binary);
  if (!file_stream.good()){
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "File not readable");
  }
  LG_DBG("Opened %s \n", file.data());
  JITHeader header;
  file_stream.read(reinterpret_cast<char*>(&header), sizeof(JITHeader));
  LG_DBG("magic-val = %x", header.magic);
  if (header.magic == k_header_magic) {
    // expected value
  }
  else if (header.magic == k_header_magic_rev) {
    LG_DBG("Data will be swapped");
  }
  else {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Unknown jit format");
  }

  return ddres_init();
}
}
