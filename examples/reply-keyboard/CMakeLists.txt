cmake_minimum_required(VERSION 3.20)
project(reply-keyboard LANGUAGES CXX)

find_package(cppgram CONFIG REQUIRED)

add_executable(reply-keyboard src/main.cpp)
target_compile_features(reply-keyboard PRIVATE cxx_std_20)
target_compile_options(reply-keyboard PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
)
target_link_libraries(reply-keyboard PRIVATE cppgram::cppgram)
install(TARGETS reply-keyboard RUNTIME DESTINATION bin)
