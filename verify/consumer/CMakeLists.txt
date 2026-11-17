cmake_minimum_required(VERSION 3.20)
project(cppgram_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Found only through the installed package config. No source from the library tree is used.
find_package(CppGram 1.0.0 EXACT REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE cppgram::cppgram)
target_compile_options(consumer PRIVATE -Wall -Wextra -Wpedantic -Werror)
