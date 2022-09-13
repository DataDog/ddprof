find_package(absl CONFIG NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)

if(absl_FOUND)
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG        20220623.1
)

# set(ABSL_PROPAGATE_CXX_STD ON CACHE INTERNAL "")
FetchContent_MakeAvailable(absl)
