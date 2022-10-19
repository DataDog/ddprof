# External dependencies (json package)
find_package(nlohmann_json 3.10.2 REQUIRED)

add_exe(
  selfuw self_unwind.cc stackchecker.cpp
  LIBRARIES DDProf::Native nlohmann_json::nlohmann_json
  DEFINITIONS "MYNAME=\"selfuw\"" "STACK_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

disable_clangtidy(selfuw)
target_static_libcxx(selfuw)

message(STATUS "top level ${CMAKE_SOURCE_DIR}")
# slightly hacky : we should only include public headers
target_include_directories(
  selfuw PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/include/lib
                 ${CMAKE_SOURCE_DIR}/third_party)
install(TARGETS selfuw DESTINATION selfuw)
