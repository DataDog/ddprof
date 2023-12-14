
#include "blaze_symbol_lookup.hpp"

#include "blazesym.h"


namespace ddprof {
BlazeSymbolLookup::BlazeSymbolLookup(){
  constexpr blaze_symbolizer_opts opts {
      .code_info = true,
      .inlined_fns = true,
      .demangle = true,
  };
  _symbolizer = blaze_symbolizer_new_opts(&opts);
}
BlazeSymbolLookup::~BlazeSymbolLookup(){
    blaze_symbolizer_free(_symbolizer);
}

SymbolIdx_t BlazeSymbolLookup::get_or_insert(const DDProfMod &ddprof_mod,
                                             SymbolTable &table,
                                             FileInfoId_t file_info_id,
                                             std::string &path,
                                             ProcessAddress_t process_pc) {

  const blaze_symbolize_src_elf src_elf {
    .path = path.c_str(),
    .debug_syms = true,
  };
  uintptr_t addrs[1] { process_pc - ddprof_mod._sym_bias};
  const struct blaze_result* blaze_res =  blaze_symbolize_elf_file_addrs(
      _symbolizer,
      &src_elf,
      addrs,
      1);
  if (blaze_res) {
    blaze_result_free(blaze_res);
  }
  return -1;
}

void BlazeSymbolLookup::erase(FileInfoId_t file_info_id) {}

}
