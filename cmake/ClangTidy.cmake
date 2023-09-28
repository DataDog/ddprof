option(ENABLE_CLANG_TIDY "Run clang-tidy with the compiler." OFF)
if(ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_COMMAND NAMES clang-tidy)
  if(NOT CLANG_TIDY_COMMAND)
    message(ERROR "CMake_RUN_CLANG_TIDY is ON but clang-tidy is not found!")
  else()
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_COMMAND};-header-filter='${CMAKE_SOURCE_DIR}/include/*'")
  endif()

  # Create a preprocessor definition that depends on .clang-tidy content so the compile command will
  # change when .clang-tidy changes.  This ensures that a subsequent build re-runs clang-tidy on all
  # sources even if they do not otherwise need to be recompiled.  Nothing actually uses this
  # definition.  We add it to targets on which we run clang-tidy just to get the build dependency on
  # the .clang-tidy file.
  file(SHA1 ${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy clang_tidy_sha1)
  add_compile_definitions(CLANG_TIDY_SHA1=${clang_tidy_sha1})
  unset(clang_tidy_sha1)
  set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy)
endif()

function(disable_clangtidy target)
  set_target_properties(${target} PROPERTIES CXX_CLANG_TIDY "" C_CLANG_TIDY "")
endfunction()
