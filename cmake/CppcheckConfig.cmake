# Unless explicitly stated otherwise all files in this repository are licensed under the Apache License Version 2.0.
# This product includes software developed at Datadog (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

find_program(CPP_CHECK_COMMAND NAMES cppcheck)
if (CPP_CHECK_COMMAND)
   #The manual : http://cppcheck.sourceforge.net/manual.pdf
   message("-- CppCheck found : ${CPP_CHECK_COMMAND}")
   set(CPPCHECK_TEMPLATE "cppcheck:{id}:{file}:{line}:{severity}:{message}")

   list(APPEND CPP_CHECK_COMMAND
         "--enable=warning,performance,portability,information,style"
         "--template=${CPPCHECK_TEMPLATE}"
         "--library=googletest"
         "--quiet"
         "--inline-suppr"
         "--project=${CMAKE_BINARY_DIR}/compile_commands.json"
         "--suppressions-list=${CMAKE_SOURCE_DIR}/CppCheckSuppressions.txt"
         -i ${CMAKE_SOURCE_DIR}/test
         -i ${CMAKE_SOURCE_DIR}/third_party
         )

   add_custom_target(
      cppcheck
      COMMAND ${CPP_CHECK_COMMAND}
      --error-exitcode=1 # make sure CI pipeline fails
      # --check-config #check what header files are missing
      )
endif()
