cmake_minimum_required(VERSION 4.0)
project(fuzz)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(fuzz src/main.cpp
        src/utils.cpp
        src/executor.cpp
        src/mutations.cpp
        src/corpus.cpp
        src/crash.cpp
        src/coverage.cpp)

target_include_directories(fuzz PRIVATE include)
target_compile_options(fuzz PRIVATE -Wall -Wextra -Wpedantic -Werror -O2)

add_executable(target target.c
        cov_runtime.c)

target_compile_options(target PRIVATE
        -fsanitize=address
        -fsanitize-coverage=trace-pc-guard,trace-cmp
        -fno-omit-frame-pointer -O1 -g)
target_link_options(target PRIVATE -fsanitize=address)