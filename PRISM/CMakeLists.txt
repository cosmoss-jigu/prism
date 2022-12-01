cmake_minimum_required(VERSION 3.11)
project(MTS)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "-pthread -Wall -Wextra -march=native")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wno-class-memaccess -Wno-attributes -Wno-unused-parameter -Wno-unused-value -Wno-packed-not-aligned -Wno-unused-variable -Wno-address-of-packed-member -mprfchw -Wno-unused-function")
set(CMAKE_CXX_FLAGS_RELEASE "-g -O3 -Wno-all -march=native -mtune=native -mprfchw -ltcmalloc")

add_definitions(-DMTS_STATS_LATENCY -DMTS_STATS_GET -DMTS_STATS_OPLOG_WAIT -DMTS_STATS_WAF)
add_definitions("-DTS_NVM_IS_PMDK")
add_definitions("-DTS_TRACE_LEVEL=TS_ERROR")

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(lib)
add_subdirectory(src)

if(IS_DIRECTORY "${CMAKE_SOURCE_DIR}/tests")
    add_subdirectory(tests)
endif()
