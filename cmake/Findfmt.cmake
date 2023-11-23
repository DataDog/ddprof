find_package(fmt CONFIG NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)

if(fmt_FOUND)
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.1.1)

FetchContent_MakeAvailable(fmt)
