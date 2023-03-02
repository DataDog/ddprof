#include "jit/jitdump.hpp"

#include "logger.hpp"
#include "span.hpp"

#include <fstream>
#include <vector>

// If we want to consider big endian, we will need this
// has bswap_64/32/16
// #include <byteswap.h>

namespace ddprof {

namespace {
static constexpr uint32_t k_header_magic = 0x4A695444;
static constexpr uint32_t k_header_magic_rev = 0x4454694A;

uint64_t load64(const uint64_t *data) {
  uint64_t ret;
  memcpy(&ret, data, sizeof(uint64_t));
  // #ifdef BIG_ENDIAN
  //   bswap_64(Ret);
  // #endif
  return ret;
}

int32_t load32(const int32_t *data) {
  int32_t ret;
  memcpy(&ret, data, sizeof(int32_t));
  // #ifdef BIG_ENDIAN
  //   bswap_32(Ret);
  // #endif
  return ret;
}

} // namespace

DDRes jit_read_header(std::ifstream &file_stream, JITHeader &header) {
  file_stream.read(reinterpret_cast<char *>(&header), sizeof(JITHeader));
  if (!file_stream.good()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "incomplete jit file");
  }
  if (header.magic == k_header_magic) {
    // expected value (no need to swap data)
  } else if (header.magic == k_header_magic_rev) {
    // todo everything should be swapped throughout the parsing (not handled)
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Swap data not handled");
  } else {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Unknown jit format");
  }
  int64_t remaining_size = header.total_size - sizeof(header);
  if (remaining_size > 0) {
    std::vector<char> read_buf;
    read_buf.resize(remaining_size);
    file_stream.read(read_buf.data(), remaining_size);
  } else {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Invalid header size");
  }
  if (header.version != k_jit_header_version) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "Version not handled");
  }
  return ddres_init();
}

// true if we should continue
bool jit_read_prefix(std::ifstream &file_stream, JITRecordPrefix &prefix) {
  file_stream.read(reinterpret_cast<char *>(&prefix), sizeof(JITRecordPrefix));
  if (!file_stream.good()) {
    // It is expected that we reach EOF here
    return false;
  }
  if (prefix.id == JITRecordType::JIT_CODE_CLOSE) {
    return false;
  }
  if (prefix.id >= JIT_CODE_MAX) {
    LG_WRN("Unknown JIT Prefix ID");
    return false;
  }
  return true;
}

constexpr uint32_t k_size_code_load_integers =
    sizeof(uint32_t) * 2 + sizeof(uint64_t) * 4;

DDRes jit_read_code_load(std::ifstream &file_stream,
                         JITRecordCodeLoad &code_load,
                         std::vector<char> &buff) {
#ifdef DEBUG
  LG_DBG("----  Read code load  ----");
#endif
  // we should at least have size for prefix / pid / tid / addr..
  if ((code_load.prefix.total_size) <
      (sizeof(JITRecordPrefix) + k_size_code_load_integers)) {
    // Unlikely unless the write was truncated
    DDRES_RETURN_WARN_LOG(DD_WHAT_JIT, "Invalid code load structure");
  }
  buff.resize(code_load.prefix.total_size - sizeof(JITRecordPrefix));
  file_stream.read(buff.data(),
                   code_load.prefix.total_size - sizeof(JITRecordPrefix));
  if (!file_stream.good()) {
    // can happen if we are in the middle of a write
    DDRES_RETURN_WARN_LOG(DD_WHAT_JIT, "Incomplete code load structure");
  }
  uint32_t *buf_32 = reinterpret_cast<uint32_t *>(buff.data());
  code_load.pid = *buf_32++;
  code_load.tid = *buf_32++;

  uint64_t *buf_64 = reinterpret_cast<uint64_t *>(buf_32);
  code_load.vma = load64(buf_64++);
  code_load.code_addr = load64(buf_64++);
  code_load.code_size = load64(buf_64++);
  code_load.code_index = load64(buf_64++);
  // remaining = total - (everything we read)
  int remaining_size = code_load.prefix.total_size - sizeof(JITRecordPrefix) -
      k_size_code_load_integers;
  if (remaining_size < static_cast<int>(code_load.code_size)) {
    // inconsistency
    DDRES_RETURN_WARN_LOG(DD_WHAT_JIT, "Incomplete code load structure");
  }
  int str_size = remaining_size - code_load.code_size;
  if (str_size > 1) {
    code_load.func_name =
        std::string(reinterpret_cast<char *>(buf_64), str_size - 1);
  }
#ifdef DEBUG
  LG_DBG("Func name = %s, address = %lx (%lu) time=%lu",
         code_load.func_name.c_str(), code_load.code_addr, code_load.code_size,
         code_load.prefix.timestamp);
#endif
  return ddres_init();
}

DDRes jit_read_debug_info(std::ifstream &file_stream,
                          JITRecordDebugInfo &debug_info,
                          std::vector<char> &buff) {
#ifdef DEBUG
  LG_DBG("---- Read debug info ----");
#endif
  buff.resize(debug_info.prefix.total_size - sizeof(JITRecordPrefix));
  file_stream.read(buff.data(), buff.size());
  if (!file_stream.good()) {
    DDRES_RETURN_WARN_LOG(DD_WHAT_JIT, "Incomplete debug info structure");
  }
  uint64_t *buf_64 = reinterpret_cast<uint64_t *>(buff.data());
  debug_info.code_addr = load64(buf_64++);
  debug_info.nr_entry = load64(buf_64++);
  debug_info.entries.resize(debug_info.nr_entry);

  for (unsigned i = 0; i < debug_info.nr_entry; ++i) {
    debug_info.entries[i].addr = load64(buf_64++);
    int32_t *buf_32 = reinterpret_cast<int32_t *>(buf_64);
    debug_info.entries[i].lineno = load32(buf_32++);
    debug_info.entries[i].discrim = load32(buf_32++);
    char *buf_char = reinterpret_cast<char *>(buf_32);
    if (static_cast<unsigned char>(*buf_char) == 0xff &&
        *(buf_char + 1) == '\0') {
      if (i >= 1) {
        debug_info.entries[i].name = debug_info.entries[i - 1].name;
      } else {
        LG_WRN("Invalid attempt to copy previous debug entry\n");
      }
    }
    debug_info.entries[i].name = std::string(buf_char);
    buf_char += debug_info.entries[i].name.size() + 1;
#ifdef DEBUG
    LG_DBG("Name:line = %s:%d / %lx / time=%lu",
           debug_info.entries[i].name.c_str(), debug_info.entries[i].lineno,
           debug_info.entries[i].addr, debug_info.prefix.timestamp);
#endif
    buf_64 = reinterpret_cast<uint64_t *>(buf_char);
  }
  return ddres_init();
}

DDRes jit_read_records(std::ifstream &file_stream, JITDump &jit_dump) {
  // a buffer to copy the data
  std::vector<char> buff;
  bool valid_entry = true;
  do {
    JITRecordPrefix prefix;
    valid_entry = jit_read_prefix(file_stream, prefix);
    if (valid_entry) {
      switch (prefix.id) {
      case JITRecordType::JIT_CODE_LOAD: {
        JITRecordCodeLoad current;
        current.prefix = prefix;
        DDRES_CHECK_FWD(jit_read_code_load(file_stream, current, buff));
        jit_dump.code_load.push_back(std::move(current));
        break;
      }
      case JITRecordType::JIT_CODE_DEBUG_INFO: {
        JITRecordDebugInfo current;
        current.prefix = prefix;
        DDRES_CHECK_FWD(jit_read_debug_info(file_stream, current, buff));
        jit_dump.debug_info.push_back(std::move(current));
        break;
      }
      default: {
        // llvm seems to only emit the two above
        DDRES_RETURN_ERROR_LOG(DD_WHAT_JIT, "jitdump record not handled");
        break;
      }
      }
    }
  } while (valid_entry);
  return ddres_init();
}

DDRes jitdump_read(std::string_view file, JITDump &jit_dump) {
  std::ifstream file_stream(file.data(), std::ios::binary);
  // We are not locking, assumption is that even if we fail to read a given
  // section we can always retry later. The aim is not to slow down the app
  if (!file_stream.good()) {
    DDRES_RETURN_ERROR_LOG(DD_WHAT_NO_JIT_FILE, "File %s not readable",
                           file.data());
  }

  try {
    LG_DBG("JITDump starting parse of %s", file.data());
    DDRES_CHECK_FWD(jit_read_header(file_stream, jit_dump.header));
    DDRES_CHECK_FWD(jit_read_records(file_stream, jit_dump));
  }
  // incomplete files can trigger exceptions
  CatchExcept2DDRes();
  return ddres_init();
}
} // namespace ddprof
