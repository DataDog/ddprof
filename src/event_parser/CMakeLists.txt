# Unless explicitly stated otherwise all files in this repository are licensed under the Apache
# License Version 2.0. This product includes software developed at Datadog
# (https://www.datadoghq.com/). Copyright 2021-Present Datadog, Inc.

find_package(BISON REQUIRED)
find_package(FLEX REQUIRED)

set(EVENT_PARSER_DIR ${CMAKE_CURRENT_BINARY_DIR}/event_parser)
set(EVENT_PARSER_SRC ${EVENT_PARSER_DIR}/src)
set(EVENT_PARSER_INCLUDE ${EVENT_PARSER_DIR}/include)
file(MAKE_DIRECTORY ${EVENT_PARSER_DIR} ${EVENT_PARSER_SRC} ${EVENT_PARSER_INCLUDE})

bison_target(event_parser_yacc event_parser.y ${EVENT_PARSER_SRC}/event_parser.tab.cpp
             DEFINES_FILE ${EVENT_PARSER_INCLUDE}/event_parser.h)
flex_target(event_parser_lex event_parser.l ${EVENT_PARSER_SRC}/event_parser.lex.cpp)
add_flex_bison_dependency(event_parser_lex event_parser_yacc)

set(DD_EVENT_PARSER_SOURCES
    # cmake-format: sortable
    ${EVENT_PARSER_SRC}/event_parser.lex.cpp ${EVENT_PARSER_SRC}/event_parser.tab.cpp
    ../perf_archmap.cc)

add_library(event_parser STATIC ${DD_EVENT_PARSER_SOURCES} ../perf_archmap.cc ../event_config.cc)
target_include_directories(event_parser PUBLIC ${EVENT_PARSER_INCLUDE} ../../include)
set_property(TARGET event_parser PROPERTY POSITION_INDEPENDENT_CODE ON)
set_property(TARGET event_parser PROPERTY CXX_CLANG_TIDY "")
add_library(DDProf::Parser ALIAS event_parser)
