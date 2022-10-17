file (GLOB MTS_SRCS "*.h" "*.cpp")
add_library(MTS STATIC
    ${MTS_SRCS}
)

target_include_directories(MTS PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/lib/pactree/include
)

INSTALL(TARGETS MTS 
        ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}
)

add_executable (main main.cpp)
target_link_libraries (
	main
	MTS
	tsoplog
	util
	numa 
	pmem
	pmemobj
	uring
	jemalloc
	tbb
	pactree
)
