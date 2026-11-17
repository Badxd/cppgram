#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

#include "cppgram/storage.h"

namespace {

std::string temp_db(const char* name) {
    const auto path = std::filesystem::temp_directory_path() / (std::string("cppgram_test_") + name + ".sqlite");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return path.string();
}

}  // namespace

TEST(Storage, SessionRoundTrip) {
    const std::string path = temp_db("session");
    {
        cppgram::SqliteStorage store(path);
        cppgram::mtproto::SessionData data;
        EXPECT_FALSE(store.load_session(data));
        data.dc_id = 2;
        data.auth_key.assign(256, 0xAB);
        data.server_salt = 0xFEEDFACECAFEBEEFull;
        data.time_offset = -42;
        store.save_session(data);
    }
    {
        cppgram::SqliteStorage store(path);
        cppgram::mtproto::SessionData data;
        ASSERT_TRUE(store.load_session(data));
        EXPECT_EQ(data.dc_id, 2);
        EXPECT_EQ(data.auth_key.size(), 256u);
        EXPECT_EQ(data.auth_key[0], 0xAB);
        EXPECT_EQ(data.server_salt, 0xFEEDFACECAFEBEEFull);
        EXPECT_EQ(data.time_offset, -42);
    }
    std::filesystem::remove(path);
}

TEST(Storage, UpdateStateRoundTrip) {
    const std::string path = temp_db("state");
    {
        cppgram::SqliteStorage store(path);
        EXPECT_FALSE(store.load_state().has_value());
        store.save_state(cppgram::StoredUpdateState{100, 7, 1700000000, 3});
        store.save_state(cppgram::StoredUpdateState{101, 7, 1700000001, 4});  // overwrite
    }
    {
        cppgram::SqliteStorage store(path);
        const auto st = store.load_state();
        ASSERT_TRUE(st.has_value());
        EXPECT_EQ(st->pts, 101);
        EXPECT_EQ(st->qts, 7);
        EXPECT_EQ(st->date, 1700000001);
        EXPECT_EQ(st->seq, 4);
    }
    std::filesystem::remove(path);
}

#include "cppgram/session_string.h"

TEST(SessionString, RoundTripKeepsEveryField) {
    cppgram::mtproto::SessionData in;
    in.dc_id = 4;
    in.time_offset = -1234567;
    in.server_salt = 0x0123456789abcdefULL;
    in.auth_key.resize(256);
    for (std::size_t i = 0; i < in.auth_key.size(); ++i) in.auth_key[i] = static_cast<std::uint8_t>(i * 7 + 3);

    const std::string text = cppgram::encode_session_string(in);
    cppgram::mtproto::SessionData out;
    ASSERT_TRUE(cppgram::decode_session_string(text, out));
    EXPECT_EQ(out.dc_id, in.dc_id);
    EXPECT_EQ(out.time_offset, in.time_offset);
    EXPECT_EQ(out.server_salt, in.server_salt);
    EXPECT_EQ(out.auth_key, in.auth_key);
}

TEST(SessionString, RejectsMalformedInput) {
    cppgram::mtproto::SessionData out;
    EXPECT_FALSE(cppgram::decode_session_string("not base64 !!", out));
    EXPECT_FALSE(cppgram::decode_session_string("AAAA", out));
}
