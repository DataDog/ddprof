

# External dependencies (linked with FetchContent)
include(FetchContent)

find_package(nlohmann_json 3.10.2 REQUIRED)

add_exe(selfuw
        self_unwind.cc
        stackchecker.cpp
        LIBRARIES DDProf::Native nlohmann_json::nlohmann_json
        DEFINITIONS "MYNAME=\"selfuw\"" "STACK_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")
# It would be better to include public only
target_compile_features(selfuw
  PRIVATE c_std_11 cxx_std_17
)
target_include_directories(selfuw PRIVATE ../../include ../../include/lib)
install(TARGETS selfuw)
