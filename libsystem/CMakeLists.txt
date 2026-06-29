cmake_minimum_required(VERSION 3.16)

add_library(iopmp_system STATIC
    src/iopmp_system.c
)

target_include_directories(iopmp_system
    PUBLIC  include
    PRIVATE ${CMAKE_SOURCE_DIR}/libiopmp/include
)

# The system layer builds on the IOPMP model library.
target_link_libraries(iopmp_system PUBLIC iopmp)
