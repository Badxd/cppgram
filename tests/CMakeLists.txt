find_package(GTest REQUIRED)

add_executable(cppgram_bot_test
    bot/Api.cpp
    bot/CurlHttpClient.cpp
    bot/EventHandler.cpp
    bot/HttpFormField.cpp
    bot/HttpServer.cpp
    bot/TgLongPoll.cpp
    bot/TgWebhookServer.cpp
    bot/TypesJson.cpp
)
target_link_libraries(cppgram_bot_test PRIVATE cppgram::cppgram GTest::gtest_main)
if(ENABLE_COVERAGE)
    target_compile_options(cppgram_bot_test PRIVATE -O0 -g --coverage)
    target_link_options(cppgram_bot_test PRIVATE --coverage)
endif()

add_executable(cppgram_mtproto_test mtproto/mtproto_test.cpp mtproto/handshake_test.cpp mtproto/session_test.cpp mtproto/highlevel_test.cpp mtproto/parser_test.cpp mtproto/storage_test.cpp mtproto/srp_test.cpp mtproto/transport_test.cpp)
target_link_libraries(cppgram_mtproto_test PRIVATE cppgram::cppgram GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(cppgram_bot_test)
gtest_discover_tests(cppgram_mtproto_test)
