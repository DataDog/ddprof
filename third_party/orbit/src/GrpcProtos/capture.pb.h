#pragma once

namespace orbit_grpc_protos {

class InstrumentedFunction {
 public:
  const std::string& function_name() const { return function_name_; }
  const std::string& file_path() const { return file_path_; }
  uint64_t file_build_id() const { return file_build_id_; }
  uint64_t file_offset() const { return file_offset_; }
  uint64_t function_id() const { return function_id_; }
  uint64_t function_size() const { return function_size_; }
  uint64_t function_virtual_address() const { return function_virtual_address_; }

  std::string file_path_;
  uint64_t file_build_id_;
  uint64_t file_offset_;
  uint64_t function_id_;
  uint64_t function_virtual_address_;  
  uint64_t function_size_;
  std::string function_name_;
};

struct CaptureOptions {
 public:
  using InstrumentedFunctions = std::vector<InstrumentedFunction>;
  pid_t pid() const { return pid_; }

  const InstrumentedFunctions& instrumented_functions() const { return instrumented_functions_; }

  pid_t pid_;
  InstrumentedFunctions instrumented_functions_;
};
}  // namespace orbit_grpc_protos
