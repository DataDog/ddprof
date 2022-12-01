message(STATUS "Compiling an async profiler loader")
add_exe(
    async_prof_load main.cc
    LIBRARIES dl DDProf::AsyncProf)

target_include_directories(async_prof_load PRIVATE ${CMAKE_SOURCE_DIR}/include
                                                   ${CMAKE_SOURCE_DIR}/third_party)
