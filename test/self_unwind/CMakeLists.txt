# External dependencies (json package)
find_package(nlohmann_json 3.10.2 REQUIRED)

add_exe(selfuw
        self_unwind.cc
        stackchecker.cpp
        LIBRARIES DDProf::Native nlohmann_json::nlohmann_json
        DEFINITIONS "MYNAME=\"selfuw\"" "STACK_DATA=\"${CMAKE_CURRENT_SOURCE_DIR}/data\"")

set_property(TARGET selfuw PROPERTY CXX_STANDARD 11)
set_property(TARGET selfuw PROPERTY C_STANDARD 11)
use_libcxx(TARGET selfuw STATIC)

# slightly hacky : we should only include public headers 
target_include_directories(selfuw PRIVATE ../../include ../../include/lib)
install(TARGETS selfuw)
