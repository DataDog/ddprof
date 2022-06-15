
# Reuse the same statsd implem as ddprof
set(STATSD_SRC_FILE ../../src/statsd.cc)
set(LOGGER_SRC_FILE ../../src/logger.cc)
set(DDRESLIST_SRC_FILE ../../src/ddres_list.cc)
set(STATSD_INCLUDE_DIR ../../include)

set(COLLATZ_SRC
    collatz.cc
    ${STATSD_SRC_FILE}
    ${LOGGER_SRC_FILE}
    ${DDRESLIST_SRC_FILE})
list(APPEND COLLATZ_DEFINITION_LIST "MYNAME=\"collatz\"" "VER_REV=\"${BUILD_REV}\"")

add_exe(collatz
        ${COLLATZ_SRC}
        LIBRARIES Threads::Threads 
        DEFINITIONS ${COLLATZ_DEFINITION_LIST})
target_include_directories(collatz PRIVATE ${STATSD_INCLUDE_DIR})

add_exe(collatz-profiling-shared
        ${COLLATZ_SRC}
        LIBRARIES Threads::Threads dd_profiling-shared
        DEFINITIONS ${COLLATZ_DEFINITION_LIST} USE_DD_PROFILING)
target_include_directories(collatz-profiling-shared PRIVATE ${STATSD_INCLUDE_DIR})

add_exe(collatz-profiling-static
        ${COLLATZ_SRC}
        LIBRARIES Threads::Threads dd_profiling-static
        DEFINITIONS ${COLLATZ_DEFINITION_LIST} USE_DD_PROFILING)
target_include_directories(collatz-profiling-static PRIVATE ${STATSD_INCLUDE_DIR})

install(TARGETS collatz DESTINATION collatz)
install(TARGETS collatz-profiling-shared DESTINATION collatz)
install(TARGETS collatz-profiling-static DESTINATION collatz)

