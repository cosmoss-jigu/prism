set(CMAKE_CXX_FLAGS_RELEASE "-O3")
file(GLOB artsrc "*.cpp" "*.h")

add_library(pdlart STATIC ${artsrc})

target_include_directories(pdlart PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    #$ENV{PACTREE_HOME}
    #${CMAKE_SOURCE_DIR}/lib/pactree/
)

INSTALL(TARGETS pdlart 
        ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR}
        )

