cmake_minimum_required(VERSION 3.16)
project(iopmp_ref_model C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Treat all warnings as errors so bad code fails the build immediately.
add_compile_options(-Wall -Wextra -Werror)

# Optional gcov/lcov code-coverage instrumentation (-DIOPMP_COVERAGE=ON).
option(IOPMP_COVERAGE "Build with gcov code-coverage instrumentation" OFF)
if(IOPMP_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# Register the test infrastructure so that 'ctest' works from the build root.
enable_testing()

# Each subdirectory defines its own targets.
add_subdirectory(libiopmp)
add_subdirectory(libsystem)
add_subdirectory(tests)
