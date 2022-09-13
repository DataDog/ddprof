find_package(capstone CONFIG QUIET)

if(TARGET capstone::capstone)
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  capstone
  GIT_REPOSITORY https://github.com/capstone-engine/capstone.git
  GIT_TAG 4.0.2)

set(CAPSTONE_BUILD_SHARED
    OFF
    CACHE INTERNAL "")
set(CAPSTONE_BUILD_TESTS
    OFF
    CACHE INTERNAL "")
set(CAPSTONE_ARCHITECUTRE_DEFAULT
    OFF
    CACHE INTERNAL "")
set(CAPSTONE_ARM64_SUPPORT
    ON
    CACHE INTERNAL "")
set(CAPSTONE_X86_SUPPORT
    ON
    CACHE INTERNAL "")

FetchContent_MakeAvailable(capstone)
FetchContent_GetProperties(capstone SOURCE_DIR capstone_src_dir)
add_library(capstone_interface INTERFACE)
target_link_libraries(capstone_interface INTERFACE capstone-static)
target_include_directories(capstone_interface INTERFACE ${capstone_src_dir}/include)
add_library(capstone::capstone ALIAS capstone_interface)
