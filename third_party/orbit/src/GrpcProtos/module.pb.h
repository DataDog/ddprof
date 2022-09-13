#pragma once

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace orbit_grpc_protos {

enum ModuleInfo_ObjectFileType : int {
  ModuleInfo_ObjectFileType_kUnknown = 0,
  ModuleInfo_ObjectFileType_kElfFile = 1,
  ModuleInfo_ObjectFileType_kCoffFile = 2,
  ModuleInfo_ObjectFileType_ModuleInfo_ObjectFileType_INT_MIN_SENTINEL_DO_NOT_USE_ =
      std::numeric_limits<int32_t>::min(),
  ModuleInfo_ObjectFileType_ModuleInfo_ObjectFileType_INT_MAX_SENTINEL_DO_NOT_USE_ =
      std::numeric_limits<int32_t>::max()
};

struct ModuleInfo {
  struct ObjectSegment {
    void set_offset_in_file(uint64_t value) { offset_in_file_ = value; }
    uint64_t offset_in_file() const { return offset_in_file_; }

    void set_size_in_file(uint64_t value) { size_in_file_ = value; }
    uint64_t size_in_file() const { return size_in_file_; }

    void set_address(uint64_t value) { address_ = value; }
    uint64_t address() const { return address_; }
    
    void set_size_in_memory(uint64_t value) { size_in_memory_ = value; }
    uint64_t size_in_memory() const { return size_in_memory_; }

    uint64_t offset_in_file_;
    uint64_t size_in_file_;
    uint64_t address_;
    uint64_t size_in_memory_;
  };

  using ObjectSegments = std::vector<ObjectSegment>;
  typedef ModuleInfo_ObjectFileType ObjectFileType;
  static constexpr ObjectFileType kUnknown = ModuleInfo_ObjectFileType_kUnknown;
  static constexpr ObjectFileType kElfFile = ModuleInfo_ObjectFileType_kElfFile;
  static constexpr ObjectFileType kCoffFile = ModuleInfo_ObjectFileType_kCoffFile;

  // string name = 1;
  void clear_name() { name_.clear(); }
  const std::string& name() const { return name_; }
  void set_name(const std::string& value) { name_ = value; }
  void set_name(std::string&& value) { name_ = value; }
  void set_name(const char* value) { name_ = value; }
  void set_name(const char* value, size_t size) { name_.assign(value, size); }
  std::string* mutable_name() { return &name_; }
  //   std::string* release_name();
  //   void set_allocated_name(std::string* name);

  // string file_path = 2;
  void clear_file_path() { file_path_.clear(); }
  const std::string& file_path() const { return file_path_; }
  void set_file_path(const std::string& value) { file_path_.assign(value); }
  void set_file_path(std::string&& value) { file_path_ = value; }
  void set_file_path(const char* value) { file_path_ = value; }
  void set_file_path(const char* value, size_t size) { file_path_.assign(value, size); }
  std::string* mutable_file_path();
  std::string* release_file_path();
  void set_allocated_file_path(std::string* file_path);

  // string build_id = 6;
  void clear_build_id() { build_id_.clear(); }
  const std::string& build_id() const { return build_id_; }
  void set_build_id(const std::string& value) { build_id_ = value;}
  void set_build_id(std::string&& value)  { build_id_ = value; }
  void set_build_id(const char* value)  { build_id_ = value;}
  void set_build_id(const char* value, size_t size)  { build_id_.assign(value, size);}
  std::string* mutable_build_id();
  std::string* release_build_id();
  void set_allocated_build_id(std::string* build_id);

  // string soname = 9;
  void clear_soname() { soname_.clear(); }
  const std::string& soname() const { return soname_; }
  void set_soname(const std::string& value)  { soname_ = value; }
  void set_soname(std::string&& value) { soname_ = value; }
  void set_soname(const char* value)  { soname_ = value; }
  void set_soname(const char* value, size_t size)  { soname_.assign(value, size); }
  std::string* mutable_soname();
  std::string* release_soname();
  void set_allocated_soname(std::string* soname);

  // uint64_tfile_size = 3;
  void clear_file_size();
  uint64_t file_size() const { return file_size_; }
  void set_file_size(uint64_t value) { file_size_ = value; }

  // uint64_taddress_start = 4;
  void clear_address_start();
  uint64_t address_start() const { return address_start_; }
  void set_address_start(uint64_t value) { address_start_ = value; }

  // uint64_taddress_end = 5;
  void clear_address_end();
  uint64_t address_end() const { return address_end_; }
  void set_address_end(uint64_t value) { address_end_ = value; }

  // uint64_tload_bias = 7;
  void clear_load_bias();
  uint64_t load_bias() const { return load_bias_; }
  void set_load_bias(uint64_t value) { load_bias_ = value; }

  // uint64_texecutable_segment_offset = 8;
  void clear_executable_segment_offset();
  uint64_t executable_segment_offset() const { return executable_segment_offset_; }
  void set_executable_segment_offset(uint64_t value) { executable_segment_offset_ = value; }

  // .orbit_grpc_protos.ModuleInfo.ObjectFileType object_file_type = 10;
  void clear_object_file_type();
  ::orbit_grpc_protos::ModuleInfo_ObjectFileType object_file_type() const {
    return static_cast<ObjectFileType>(object_file_type_);
  }
  void set_object_file_type(::orbit_grpc_protos::ModuleInfo_ObjectFileType value) {
    object_file_type_ = static_cast<int>(value);
  }

  ObjectSegment* add_object_segments() {
    object_segments_.emplace_back();
    return &object_segments_.back();
  }

  std::string name_;
  std::string file_path_;
  std::string build_id_;
  std::string soname_;
  uint64_t file_size_;
  uint64_t address_start_;
  uint64_t address_end_;
  uint64_t load_bias_;
  uint64_t executable_segment_offset_;
  int object_file_type_;
  ObjectSegments object_segments_;
};

}  // namespace orbit_grpc_protos
