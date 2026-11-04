cmake_minimum_required(VERSION 3.16)
project(cppgram LANGUAGES CXX)

# Build speed. Each item falls back to the plain toolchain when its tool is missing; nothing here is required.
#   CPPGRAM_USE_CCACHE / CPPGRAM_COMPILER_CACHE  reuse object files (ccache or sccache)
#   CPPGRAM_FAST_LINKER                          link with mold or lld instead of GNU ld
#   CPPGRAM_UNITY_BUILD                          merge sources into fewer translation units (opt-in)
option(CPPGRAM_USE_CCACHE "Use a compiler cache when one is installed (see CPPGRAM_COMPILER_CACHE)" ON)
set(CPPGRAM_COMPILER_CACHE "auto" CACHE STRING "Compiler cache: auto, ccache, sccache or off")
set_property(CACHE CPPGRAM_COMPILER_CACHE PROPERTY STRINGS auto ccache sccache off)
set(CPPGRAM_FAST_LINKER "auto" CACHE STRING "Fast linker: auto (mold, then lld), mold, lld or off")
set_property(CACHE CPPGRAM_FAST_LINKER PROPERTY STRINGS auto mold lld off)
option(CPPGRAM_UNITY_BUILD "Merge sources into batches per target: faster clean builds, slower single-file rebuilds" OFF)

# Tooling reads build/compile_commands.json (clangd, clang-tidy, IDEs). Costs nothing at build time.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CPPGRAM_USE_CCACHE AND NOT CPPGRAM_COMPILER_CACHE STREQUAL "off" AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
    if(CPPGRAM_COMPILER_CACHE STREQUAL "ccache")
        set(_cppgram_cache_names ccache)
    elseif(CPPGRAM_COMPILER_CACHE STREQUAL "sccache")
        set(_cppgram_cache_names sccache)
    else()
        set(_cppgram_cache_names ccache sccache)
    endif()
    find_program(CPPGRAM_CACHE_EXECUTABLE NAMES ${_cppgram_cache_names})
    if(CPPGRAM_CACHE_EXECUTABLE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CPPGRAM_CACHE_EXECUTABLE}")
        message(STATUS "cppgram: using ${CPPGRAM_CACHE_EXECUTABLE} for C++ compilation")
    elseif(NOT CPPGRAM_COMPILER_CACHE STREQUAL "auto")
        message(FATAL_ERROR "CPPGRAM_COMPILER_CACHE=${CPPGRAM_COMPILER_CACHE} but no such program was found")
    endif()
endif()

# Fast linker. Linking the library and every test binary is part of each incremental build; mold and lld link
# faster than GNU ld. A candidate is used only if a test program links with it.
include(CheckCXXSourceCompiles)
if(NOT CPPGRAM_FAST_LINKER STREQUAL "off")
    if(CPPGRAM_FAST_LINKER STREQUAL "auto")
        set(_cppgram_linkers mold lld)
    else()
        set(_cppgram_linkers ${CPPGRAM_FAST_LINKER})
    endif()
    set(_cppgram_linker_found OFF)
    foreach(_cppgram_linker IN LISTS _cppgram_linkers)
        set(CMAKE_REQUIRED_LINK_OPTIONS "-fuse-ld=${_cppgram_linker}")
        check_cxx_source_compiles("int main() { return 0; }" CPPGRAM_HAVE_LINKER_${_cppgram_linker})
        unset(CMAKE_REQUIRED_LINK_OPTIONS)
        if(CPPGRAM_HAVE_LINKER_${_cppgram_linker})
            add_link_options("-fuse-ld=${_cppgram_linker}")
            message(STATUS "cppgram: linking with ${_cppgram_linker}")
            set(_cppgram_linker_found ON)
            break()
        endif()
    endforeach()
    if(NOT _cppgram_linker_found AND NOT CPPGRAM_FAST_LINKER STREQUAL "auto")
        message(FATAL_ERROR "CPPGRAM_FAST_LINKER=${CPPGRAM_FAST_LINKER} does not link with this toolchain")
    endif()
endif()

option(ENABLE_TESTS "Build tests" OFF)
option(ENABLE_SANITIZERS "Instrument the library and tests with address and undefined behavior sanitizers" OFF)
option(ENABLE_COVERAGE "Instrument the library and tests for code coverage" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_DOCUMENTATION "Build API documentation" OFF)
option(BUILD_LIVE_CHECK "Build the live Telegram connectivity check (examples/live_check.cpp)" OFF)
option(BUILD_LIVE_TESTS "Build the live Telegram test runner (examples/live_tests/live_tests.cpp). Needs real credentials to run" OFF)
option(ENABLE_BENCHMARKS "Build the micro-benchmark program (bench/bench_main.cpp). Measures local work only" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Boost 1.65.1 REQUIRED)
find_package(CURL 7.58.0 REQUIRED)
find_package(nlohmann_json 3.7.3 REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(cppgram
    src/Api.cpp
    src/ApiCodec.cpp
    src/ApiMethods.cpp
    src/Bot.cpp
    src/EventBroadcaster.cpp
    src/EventHandler.cpp
    src/HttpClient.cpp
    src/TgException.cpp
    src/CurlHttpClient.cpp
    src/TgLongPoll.cpp
    src/TgWebhookLocalServer.cpp
    src/TgWebhookTcpServer.cpp
    src/InputFile.cpp
    src/Types.cpp
    src/mtproto/tl.cpp
    src/mtproto/crypto.cpp
    src/mtproto/transport.cpp
    src/mtproto/retry.cpp
    src/update_gate.cpp
    src/auth_state.cpp
    src/mtproto/schema/api_impl_0.cpp
    src/mtproto/schema/api_impl_1.cpp
    src/mtproto/schema/api_impl_2.cpp
    src/mtproto/schema/api_impl_3.cpp
    src/mtproto/schema/api_impl_4.cpp
    src/mtproto/schema/api_impl_5.cpp
    src/mtproto/schema/api_impl_6.cpp
    src/mtproto/schema/api_impl_7.cpp
    src/mtproto/schema/api_impl_8.cpp
    src/mtproto/schema/api_impl_9.cpp
    src/mtproto/auth.cpp
    src/mtproto/rsa_keys.cpp
    src/mtproto/connection.cpp
    src/mtproto/session.cpp
    src/mtproto/srp.cpp
    src/client.cpp
    src/client_login.cpp
    src/transfer.cpp
    src/mtproto/cancel.cpp
    src/mtproto/socks5.cpp
    src/mtproto/proxy_secret.cpp
    src/mtproto/obfuscated.cpp
    src/channel_pts.cpp
    src/client_extra.cpp
    src/client_more.cpp
    src/client_media.cpp
    src/client_markup.cpp
    src/client_stickers.cpp
    src/client_text.cpp
    src/parser.cpp
    src/storage.cpp
    src/client_bot.cpp
    src/client_message.cpp
    src/client_chats.cpp
    src/client_forum.cpp
    src/client_folders.cpp
    src/client_invites.cpp
    src/client_stories_messages.cpp
    src/client_folder_invites.cpp
    src/client_forum_and_stars.cpp
    src/client_account_settings.cpp
    src/client_contacts_and_blocks.cpp
    src/client_business_and_stealth.cpp
    src/client_batch5.cpp
    src/client_batch6.cpp
    src/client_batch7.cpp
    src/dc.cpp
    src/client_chat_extras.cpp
    src/client_invite_check.cpp
    src/client_own_star_gifts.cpp
    src/client_star_gift_listing.cpp
    src/client_star_gift_collections.cpp
    src/client_story_reactions.cpp
    src/client_bot_admin.cpp
    src/client_business_bots.cpp
    src/client_poll_stop.cpp
    src/client_checklists.cpp
    src/client_inline_edit.cpp
    src/client_payment_receipts.cpp
    src/client_story_posting.cpp
    src/client_account_security.cpp
    src/client_profile.cpp
    src/client_send.cpp
    src/client_session.cpp
    src/session_string.cpp
    src/session_crypto.cpp
    src/convert.cpp
    src/filters.cpp
)
add_library(cppgram::cppgram ALIAS cppgram)

if(CPPGRAM_UNITY_BUILD)
    # These files stay one translation unit each. Merging them either exhausts memory on small machines (the generated
    # api_impl files and Types.cpp, which is also why CI uses -j1) or puts two files that define the same helper name
    # in one anonymous namespace (the other files listed here). Found by scanning src/*.cpp for duplicate helper names.
    set_source_files_properties(
        src/Types.cpp
        src/mtproto/schema/api_impl_0.cpp
        src/mtproto/schema/api_impl_1.cpp
        src/mtproto/schema/api_impl_2.cpp
        src/mtproto/schema/api_impl_3.cpp
        src/mtproto/schema/api_impl_4.cpp
        src/mtproto/schema/api_impl_5.cpp
        src/mtproto/schema/api_impl_6.cpp
        src/mtproto/schema/api_impl_7.cpp
        src/mtproto/schema/api_impl_8.cpp
        src/mtproto/schema/api_impl_9.cpp
        src/mtproto/srp.cpp
        src/client.cpp
        src/client_send.cpp
        src/client_media.cpp
        src/client_chat_extras.cpp
        src/client_batch5.cpp
        src/client_bot.cpp
        src/client_stickers.cpp
        src/client_profile.cpp
        src/client_own_star_gifts.cpp
        src/client_star_gift_collections.cpp
        PROPERTIES SKIP_UNITY_BUILD_INCLUSION ON)
    set_target_properties(cppgram PROPERTIES UNITY_BUILD ON UNITY_BUILD_BATCH_SIZE 8)
endif()

set_source_files_properties(src/Types.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:MSVC>:/bigobj>"
)

target_compile_features(cppgram PUBLIC cxx_std_20)
target_compile_options(cppgram PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall>
)
if(ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        message(FATAL_ERROR "ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    target_compile_options(cppgram PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cppgram PUBLIC -fsanitize=address,undefined)
endif()
if(ENABLE_COVERAGE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "ENABLE_COVERAGE requires GCC")
    endif()
    target_compile_options(cppgram PRIVATE -O0 -g --coverage)
    target_link_options(cppgram PUBLIC --coverage)
endif()
target_include_directories(cppgram PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(cppgram PUBLIC
    Boost::headers
    CURL::libcurl
    nlohmann_json::nlohmann_json
    Threads::Threads
    OpenSSL::Crypto
    ZLIB::ZLIB
)
target_link_libraries(cppgram PRIVATE SQLite::SQLite3)
set_target_properties(cppgram PROPERTIES
    CXX_EXTENSIONS OFF
)

if(BUILD_SHARED_LIBS)
    target_compile_definitions(cppgram PUBLIC CPPGRAM_DLL)
endif()

if(WIN32)
    target_compile_definitions(cppgram PUBLIC
        _WIN32_WINNT=0x0601
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(cppgram PRIVATE Ws2_32)
endif()

install(TARGETS cppgram
    EXPORT cppgramTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

set(CPPGRAM_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/cppgram")
configure_package_config_file(
    cmake/cppgramConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/cppgramConfig.cmake"
    INSTALL_DESTINATION "${CPPGRAM_INSTALL_CMAKEDIR}"
)
install(EXPORT cppgramTargets
    NAMESPACE cppgram::
    FILE cppgramTargets.cmake
    DESTINATION "${CPPGRAM_INSTALL_CMAKEDIR}"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cppgramConfig.cmake"
    DESTINATION "${CPPGRAM_INSTALL_CMAKEDIR}"
)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_DOCUMENTATION)
    find_package(Doxygen REQUIRED)
    add_custom_target(doc_doxygen ALL
        COMMAND Doxygen::doxygen ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating API documentation with Doxygen"
        VERBATIM
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/doc/html/
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )
endif()

# TL code generator (C++ only). Regenerate the schema code with: make tl-generate
add_executable(cppgram_tl_codegen tools/tl_codegen/tl_codegen.cpp)
add_custom_target(tl-generate
    COMMAND cppgram_tl_codegen
        ${CMAKE_CURRENT_SOURCE_DIR}/include/cppgram/mtproto/schema/api.h
        ${CMAKE_CURRENT_SOURCE_DIR}/src/mtproto/schema
        ${CMAKE_CURRENT_SOURCE_DIR}/schema/main_api.tl
        ${CMAKE_CURRENT_SOURCE_DIR}/schema/sys_msgs.tl
        ${CMAKE_CURRENT_SOURCE_DIR}/schema/auth_key.tl
    DEPENDS cppgram_tl_codegen
    COMMENT "Generating TL schema code"
    VERBATIM
)

# Verifies that regenerating the schema code reproduces the checked-in files and that the layer matches.
add_custom_target(tl-check
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tools/check_schema.sh $<TARGET_FILE:cppgram_tl_codegen>
    DEPENDS cppgram_tl_codegen
    COMMENT "Checking TL schema reproducibility"
    VERBATIM
)

if(ENABLE_BENCHMARKS)
    add_executable(cppgram_bench bench/bench_main.cpp)
    target_link_libraries(cppgram_bench PRIVATE cppgram)
endif()

if(BUILD_LIVE_CHECK)
    add_executable(cppgram_live_check examples/live_check.cpp)
    target_link_libraries(cppgram_live_check PRIVATE cppgram)
    target_include_directories(cppgram_live_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

# Live tests: only the runner and the library, no test suite, no fuzzers. Optimization is left at the CMake default
# for the build type; a live run is network-bound, so there is no reason to pay for -O3 here.
if(BUILD_LIVE_TESTS)
    add_executable(cppgram_live_tests examples/live_tests/live_tests.cpp)
    target_link_libraries(cppgram_live_tests PRIVATE cppgram)
    target_include_directories(cppgram_live_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

option(BUILD_FUZZERS "Build libFuzzer targets (clang with libFuzzer + ASan runtime)" OFF)
if(BUILD_FUZZERS)
    foreach(fuzz_name fuzz_tl_reader fuzz_session_string fuzz_html_parser fuzz_session_crypto)
        add_executable(${fuzz_name} fuzz/${fuzz_name}.cpp)
        target_include_directories(${fuzz_name} PRIVATE include src)
        target_link_libraries(${fuzz_name} PRIVATE cppgram)
        target_compile_options(${fuzz_name} PRIVATE -fsanitize=fuzzer,address -g -O1)
        target_link_options(${fuzz_name} PRIVATE -fsanitize=fuzzer,address)
    endforeach()
endif()
