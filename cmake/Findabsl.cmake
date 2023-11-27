find_package(absl CONFIG NO_SYSTEM_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH)

if(absl_FOUND)
  return()
endif()

include(FetchContent)
FetchContent_Declare(
  absl
  GIT_REPOSITORY https://github.com/abseil/abseil-cpp.git
  GIT_TAG fb3621f4f897824c0dbe0615fa94543df6192f30 # Abseil LTS branch, Aug 2023, Patch 1
                                                   # tag#20230802.1
)

set(ABSL_PROPAGATE_CXX_STD
    ON
    CACHE INTERNAL "")
FetchContent_MakeAvailable(absl)
