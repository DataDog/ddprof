# Run commands before automatic detection of compiler
# Example with :
## cmake -C init_cache.cmake
set(CMAKE_C_COMPILER CACHE FILEPATH clang-12)
set(CMAKE_CXX_COMPILER CACHE FILEPATH clang-cpp-12)
