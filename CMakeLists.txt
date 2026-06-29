cmake_minimum_required(VERSION 3.16)
project(iopmp_ref_model C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Treat all warnings as errors so bad code fails the build immediately.
add_compile_options(-Wall -Wextra -Werror)

# Register the test infrastructure so that 'ctest' works from the build root.
enable_testing()

# Each subdirectory defines its own targets.
add_subdirectory(libiopmp)
add_subdirectory(libsystem)
add_subdirectory(tests)
