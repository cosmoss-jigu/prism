#set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DTS_ENABLE_ASSERT -DTS_ENABLE_FREE_POISIONING")
set(CMAKE_CXX_FLAGS_RELEASE "-march=native -mtune=native -O3 -g -ggdb3")
#set(CMAKE_BUILD_TYPE Debug)
file(GLOB tsoplogsrc "*.c" "*.h")

add_library(tsoplog STATIC ${tsoplogsrc})

target_include_directories(tsoplog PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(
	tsoplog
	pthread
	pmem
	pmemobj
	jemalloc
	gtest
)

INSTALL(TARGETS tsoplog 
        ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}
)
