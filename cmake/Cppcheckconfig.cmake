
find_program(CPP_CHECK_COMMAND NAMES cppcheck)
if (CPP_CHECK_COMMAND)
   #The manual : http://cppcheck.sourceforge.net/manual.pdf
   message("-- CppCheck found : ${CPP_CHECK_COMMAND}")

   # One strategy is to list the files to check
   file(GLOB_RECURSE ALL_SOURCE_FILES src/*.c include/*.hpp src/*.cpp src/*.cc include/*.h test/*.cc test/*.h)
   set(CPPCHECK_TEMPLATE "cppcheck:{id}:{file}:{line}:{severity}:{message}")

   # Another one is to exclude all the irrelevant errors (harder to know which build folders to exclude)
   # list(APPEND IGNORE_ARG 
   # "-i${CMAKE_SOURCE_DIR}/vendor"
   # "-i${CMAKE_SOURCE_DIR}/build_Release")

   list(APPEND CPP_CHECK_COMMAND 
         "--enable=warning,performance,portability,information,style"
         "--template=${CPPCHECK_TEMPLATE}"
         "--quiet" 
         # "--verbose"
         "--suppressions-list=${CMAKE_SOURCE_DIR}/CppCheckSuppressions.txt"
         #"--cppcheck-build-dir=${CMAKE_BINARY_DIR}" #does not work well with suppressions
         )

   add_custom_target(
      cppcheck
      COMMAND ${CPP_CHECK_COMMAND}
      --error-exitcode=1 # make sure CI pipeline fails
      # --check-config #check what header files are missing
      ${ALL_SOURCE_FILES}
      )
  
   # To include it in standard make command (slows down regular make and less relevant errors, strange ?)
   #set(CMAKE_C_CPPCHECK ${CPP_CHECK_COMMAND})
   #set(CMAKE_CXX_CPPCHECK ${CPP_CHECK_COMMAND})
endif()
