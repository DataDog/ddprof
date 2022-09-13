set(llvm_dir "${CMAKE_SOURCE_DIR}/third_party/llvm-core/")

file(GLOB_RECURSE ORBIT_SRCS src/*.cpp)
add_library(orbit STATIC ${ORBIT_SRCS})
target_include_directories(
  orbit PUBLIC src src/Introspection/include src/ModuleUtils/include src/ObjectUtils/include
               src/OrbitBase/include src/UserSpaceInstrumentation/include)
target_link_libraries(orbit PUBLIC absl::base absl::strings absl::time absl::str_format
                      absl::flat_hash_map)
target_include_directories(
  orbit PUBLIC ${llvm_dir}/include)
target_link_libraries(
  orbit
  PUBLIC
  capstone::capstone
  LLVMHeaders
  LLVMObject
  LLVMSymbolize)  
  