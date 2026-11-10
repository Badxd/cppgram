find_package(GTest REQUIRED)

add_executable(cppgram_bot_test
    bot/Api.cpp
    bot/CurlHttpClient.cpp
    bot/EventHandler.cpp
    bot/HttpFormField.cpp
    bot/HttpServer.cpp
    bot/InlineKeyboard.cpp
    bot/InlineKeyboardValidation.cpp
    bot/TgLongPoll.cpp
    bot/TgWebhookServer.cpp
    bot/TypesJson.cpp
)
target_link_libraries(cppgram_bot_test PRIVATE cppgram::cppgram GTest::gtest_main)
if(ENABLE_COVERAGE)
    target_compile_options(cppgram_bot_test PRIVATE -O0 -g --coverage)
    target_link_options(cppgram_bot_test PRIVATE --coverage)
endif()

add_executable(cppgram_mtproto_test mtproto/mtproto_test.cpp mtproto/schema_layer_test.cpp mtproto/auth_transfer_test.cpp mtproto/qr_login_fake_dc_test.cpp mtproto/stories_fake_dc_test.cpp mtproto/auth_stories_batch4_test.cpp mtproto/password_recovery_fake_dc_test.cpp mtproto/stories_edit_fake_dc_test.cpp mtproto/account_reads_fake_dc_test.cpp mtproto/payments_stars_fake_dc_test.cpp mtproto/wallet_fake_dc_test.cpp mtproto/remaining_methods_fake_dc_test.cpp mtproto/folders_audit_fake_dc_test.cpp mtproto/partial_methods_fake_dc_test.cpp mtproto/ephemeral_inline_fake_dc_test.cpp mtproto/keyboard_roundtrip_test.cpp mtproto/ephemeral_upload_fake_dc_test.cpp mtproto/inline_send_fake_dc_test.cpp mtproto/inline_decode_fake_dc_test.cpp mtproto/rich_blocks_fake_dc_test.cpp mtproto/handshake_test.cpp mtproto/session_test.cpp mtproto/highlevel_test.cpp mtproto/parser_test.cpp mtproto/storage_test.cpp mtproto/srp_test.cpp mtproto/transport_test.cpp mtproto/retry_test.cpp mtproto/update_gate_test.cpp mtproto/auth_state_test.cpp mtproto/transfer_test.cpp mtproto/cancel_test.cpp mtproto/cancel_stress_test.cpp mtproto/channel_pts_test.cpp mtproto/session_crypto_test.cpp mtproto/socks5_test.cpp mtproto/proxy_secret_test.cpp mtproto/obfuscated_test.cpp mtproto/connection_net_test.cpp mtproto/client_fake_dc_test.cpp mtproto/session_encrypted_test.cpp mtproto/sqlite_encrypted_test.cpp mtproto/session_string_encrypted_test.cpp mtproto/storage_concurrency_test.cpp mtproto/inflight_cancel_test.cpp mtproto/dc_migration_fake_dc_test.cpp mtproto/phone_migration_fake_dc_test.cpp mtproto/auth_migration_state_test.cpp mtproto/proxy_transport_audit_test.cpp)
target_link_libraries(cppgram_mtproto_test PRIVATE cppgram::cppgram GTest::gtest_main)
# Internal seams (for example session_crypto_detail.h) are reached from tests through src/.
target_include_directories(cppgram_mtproto_test PRIVATE ${CMAKE_SOURCE_DIR}/src)

include(GoogleTest)
gtest_discover_tests(cppgram_bot_test)
gtest_discover_tests(cppgram_mtproto_test)
add_executable(cppgram_security_test
    security/errors_test.cpp
    security/log_test.cpp
    security/webapp_test.cpp
    security/integration_test.cpp
)
target_link_libraries(cppgram_security_test PRIVATE cppgram::cppgram GTest::gtest_main)
gtest_discover_tests(cppgram_security_test)

add_executable(cppgram_metrics_test
    metrics/metrics_test.cpp
)
target_link_libraries(cppgram_metrics_test PRIVATE cppgram::cppgram GTest::gtest_main)
gtest_discover_tests(cppgram_metrics_test)

add_executable(cppgram_async_test
    async/async_test.cpp
    async/task_test.cpp
    async/docs_examples_test.cpp
)
target_link_libraries(cppgram_async_test PRIVATE cppgram::cppgram GTest::gtest_main)
gtest_discover_tests(cppgram_async_test)

add_executable(cppgram_media_test
    media/media_fake_dc_test.cpp
)
target_link_libraries(cppgram_media_test PRIVATE cppgram::cppgram GTest::gtest_main)
gtest_discover_tests(cppgram_media_test)
