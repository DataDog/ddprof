#pragma once

#include "ddprof_module.hpp"
#include "ddprof_defs.hpp"
#include "ddprof_file_info-i.hpp"
#include "symbol_table.hpp"
#include "symbol.hpp"
#include "dso.hpp"

#include <unordered_map>

struct blaze_symbolizer;

namespace ddprof {
class BlazeSymbolLookup {
public:
  BlazeSymbolLookup();
  ~BlazeSymbolLookup();
  SymbolIdx_t get_or_insert(const DDProfMod &ddprof_mod,
                            SymbolTable &table,
                            FileInfoId_t file_info_id,
                            std::string &path,
                            ProcessAddress_t process_pc);

  void erase(FileInfoId_t file_info_id);

private:
  struct SymbolLoc{
    SymbolIdx_t _symbol_idx;
    SymbolIdx_t _inlined_func;
    int _line_no;
  };
  using SymbolLocMap = std::unordered_map<ElfAddress_t, SymbolLoc>;
  using FileInfo2SymbolMap = std::unordered_map<FileInfoId_t, SymbolLocMap>;

  blaze_symbolizer *_symbolizer;
};

}