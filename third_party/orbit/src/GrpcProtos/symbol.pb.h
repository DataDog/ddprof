#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace orbit_grpc_protos {

struct SymbolInfo {
  void set_demangled_name(const std::string& value) { demangled_name_ = value; }
  const std::string& demangled_name() const { return demangled_name_; }

  void set_address(uint64_t value) { address_ = value; }
  uint64_t address() const { return address_; }

  void set_size(uint64_t value) { size_ = value; }
  uint64_t size() const { return size_; }

  void set_is_hotpatchable(bool value) { is_hotpatchable_ = value; }
  bool is_hotpatchable() const { return is_hotpatchable_; }
  
  std::string demangled_name_;
  uint64_t address_;
  uint64_t size_;
  bool is_hotpatchable_;
};

struct ModuleSymbols {
  using SymbolInfos = std::vector<SymbolInfo>;
  const SymbolInfos& symbol_infos() const { return symbol_infos_; }
  SymbolInfos* mutable_symbol_infos() { return &symbol_infos_; }
  SymbolInfo* add_symbol_infos() {
    symbol_infos_.emplace_back();
    return &symbol_infos_.back();
  }
  size_t symbol_infos_size() const { return symbol_infos_.size(); }

  SymbolInfos symbol_infos_;
};

struct LineInfo {
  void set_source_file(const std::string& value) { source_file_ = value; }
  const std::string& source_file() const { return source_file_; }
  
  void set_source_line(uint32_t value) { source_line_ = value; }
  uint32_t source_line() const { return source_line_; }

  std::string source_file_;
  uint32_t source_line_;
};

}  // namespace orbit_grpc_protos