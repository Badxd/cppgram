cmake_minimum_required(VERSION 3.16)
project(cppgram LANGUAGES CXX)

option(ENABLE_TESTS "Build tests" OFF)
option(ENABLE_SANITIZERS "Instrument the library and tests with address and undefined behavior sanitizers" OFF)
option(ENABLE_COVERAGE "Instrument the library and tests for code coverage" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(BUILD_DOCUMENTATION "Build API documentation" OFF)
option(BUILD_LIVE_CHECK "Build the live Telegram connectivity check (examples/live_check.cpp)" OFF)

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
    src/convert.cpp
    src/filters.cpp
)
add_library(cppgram::cppgram ALIAS cppgram)

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

if(BUILD_LIVE_CHECK)
    add_executable(cppgram_live_check examples/live_check.cpp)
    target_link_libraries(cppgram_live_check PRIVATE cppgram)
    target_include_directories(cppgram_live_check PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()
