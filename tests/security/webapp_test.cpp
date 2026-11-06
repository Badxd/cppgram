#include "cppgram/webapp.h"

#include <gtest/gtest.h>

#include <string>

using namespace cppgram;
using webapp::Status;

namespace {

// Known-answer vector. Generated independently with Python's hmac/hashlib (not with this library), using the
// documented algorithm: secret = HMAC_SHA256("WebAppData", token); hash = HMAC_SHA256(secret, data_check_string).
constexpr char kBotToken[] = "123456:AAFakeTokenForTestOnly_0123456789abc";
constexpr std::int64_t kAuthDate = 1700000000;
constexpr char kValidInit[]
    = "query_id=AAHdF6IQAAAAAN0XohDhrOrc"
      "&user=%7B%22id%22%3A279058397%2C%22first_name%22%3A%22Vladislav%22%2C%22last_name%22%3A%22Kibenko%22%2C"
      "%22username%22%3A%22vdkfrost%22%2C%22language_code%22%3A%22ru%22%2C%22is_bot%22%3Afalse%7D"
      "&auth_date=1700000000"
      "&hash=85f384565784f25f88fe74eb048c6430b01250722d508c348966ba9c3025e00c";

constexpr std::int64_t kNow = kAuthDate + 100;

std::string replace_once(std::string s, const std::string& from, const std::string& to) {
    const auto pos = s.find(from);
    EXPECT_NE(pos, std::string::npos) << "pattern not found: " << from;
    if (pos != std::string::npos)
        s.replace(pos, from.size(), to);
    return s;
}

// Changes the hash so it cannot match. Keeps the same length and charset.
std::string with_hash(const std::string& init, const std::string& hash) {
    const auto pos = init.find("&hash=");
    return init.substr(0, pos) + "&hash=" + hash;
}

} // namespace

TEST(WebAppTest, ValidSignatureIsAccepted) {
    const auto r = webapp::verify(kValidInit, kBotToken, kNow);
    ASSERT_EQ(r.status, Status::Valid) << webapp::to_string(r.status);
    ASSERT_TRUE(r.ok());
    ASSERT_TRUE(r.data.has_value());
    EXPECT_EQ(r.data->auth_date, kAuthDate);
    ASSERT_TRUE(r.data->query_id.has_value());
    EXPECT_EQ(*r.data->query_id, "AAHdF6IQAAAAAN0XohDhrOrc");
    ASSERT_TRUE(r.data->user.has_value());
    EXPECT_EQ(r.data->user->id, 279058397);
    EXPECT_EQ(r.data->user->first_name, "Vladislav");
    ASSERT_TRUE(r.data->user->last_name.has_value());
    EXPECT_EQ(*r.data->user->last_name, "Kibenko");
    EXPECT_EQ(*r.data->user->username, "vdkfrost");
    EXPECT_EQ(*r.data->user->language_code, "ru");
    EXPECT_FALSE(r.data->user->is_bot);
}

TEST(WebAppTest, HashIsNotReturnedInFields) {
    const auto r = webapp::verify(kValidInit, kBotToken, kNow);
    ASSERT_TRUE(r.ok());
    EXPECT_EQ(r.data->fields.count("hash"), 0u);
    EXPECT_EQ(r.data->fields.count("auth_date"), 1u);
    EXPECT_EQ(r.data->fields.count("user"), 1u);
}

TEST(WebAppTest, WrongTokenIsRejected) {
    const auto r = webapp::verify(kValidInit, "123456:AAAnotherTokenThatIsWrong_0123456789xyz", kNow);
    EXPECT_EQ(r.status, Status::BadSignature);
    EXPECT_FALSE(r.data.has_value());
}

TEST(WebAppTest, ModifiedUserDataIsRejected) {
    const auto init = replace_once(kValidInit, "Vladislav", "Vladimir");
    const auto r = webapp::verify(init, kBotToken, kNow);
    EXPECT_EQ(r.status, Status::BadSignature);
    EXPECT_FALSE(r.data.has_value()) << "unverified user data must never be exposed";
}

TEST(WebAppTest, ModifiedAuthDateIsRejected) {
    const auto init = replace_once(kValidInit, "auth_date=1700000000", "auth_date=1700000001");
    const auto r = webapp::verify(init, kBotToken, kNow);
    EXPECT_EQ(r.status, Status::BadSignature);
}

TEST(WebAppTest, ModifiedQueryFieldIsRejected) {
    const auto init = replace_once(kValidInit, "AAHdF6IQAAAAAN0XohDhrOrc", "AAHdF6IQAAAAAN0XohDhrOrd");
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::BadSignature);
}

TEST(WebAppTest, AddedFieldIsRejected) {
    const auto init = std::string(kValidInit) + "&start_param=extra";
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::BadSignature);
}

TEST(WebAppTest, ExpiredAuthDateIsRejectedAfterTheSignatureCheck) {
    webapp::VerifyOptions opt;
    opt.max_age = std::chrono::seconds(3600);
    const auto r = webapp::verify(kValidInit, kBotToken, kAuthDate + 3601, opt);
    EXPECT_EQ(r.status, Status::Expired);
    EXPECT_FALSE(r.data.has_value());
}

TEST(WebAppTest, AgeExactlyAtTheLimitIsAccepted) {
    webapp::VerifyOptions opt;
    opt.max_age = std::chrono::seconds(3600);
    EXPECT_EQ(webapp::verify(kValidInit, kBotToken, kAuthDate + 3600, opt).status, Status::Valid);
}

TEST(WebAppTest, FutureAuthDateIsRejected) {
    webapp::VerifyOptions opt;
    opt.max_future_skew = std::chrono::seconds(60);
    // Local clock 61 seconds behind auth_date: outside the allowed skew.
    EXPECT_EQ(webapp::verify(kValidInit, kBotToken, kAuthDate - 61, opt).status, Status::FromFuture);
    // Within the skew: accepted.
    EXPECT_EQ(webapp::verify(kValidInit, kBotToken, kAuthDate - 60, opt).status, Status::Valid);
}

TEST(WebAppTest, MissingHashIsRejected) {
    const std::string init = "query_id=AAHdF6IQAAAAAN0XohDhrOrc&auth_date=1700000000";
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::MissingHash);
}

TEST(WebAppTest, MissingAuthDateIsRejected) {
    const std::string init = "query_id=AAHdF6IQAAAAAN0XohDhrOrc&hash=" + std::string(64, 'a');
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::MissingAuthDate);
}

TEST(WebAppTest, NonNumericAuthDateIsRejected) {
    const auto init = replace_once(kValidInit, "auth_date=1700000000", "auth_date=17x0000000");
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::BadAuthDate);
}

TEST(WebAppTest, HashOfWrongShapeIsMalformed) {
    EXPECT_EQ(webapp::verify(with_hash(kValidInit, "abc"), kBotToken, kNow).status, Status::Malformed);
    EXPECT_EQ(webapp::verify(with_hash(kValidInit, std::string(64, 'z')), kBotToken, kNow).status, Status::Malformed);
}

TEST(WebAppTest, EmptyInputIsEmpty) {
    EXPECT_EQ(webapp::verify("", kBotToken, kNow).status, Status::Empty);
    EXPECT_EQ(webapp::verify("?", kBotToken, kNow).status, Status::Empty);
}

TEST(WebAppTest, EmptyTokenNeverVerifies) {
    EXPECT_EQ(webapp::verify(kValidInit, "", kNow).status, Status::BadSignature);
}

TEST(WebAppTest, MalformedPairStructureIsRejected) {
    EXPECT_EQ(webapp::verify("a=1&&b=2", kBotToken, kNow).status, Status::Malformed);
    EXPECT_EQ(webapp::verify("a=1&", kBotToken, kNow).status, Status::Malformed);
    EXPECT_EQ(webapp::verify("novalue", kBotToken, kNow).status, Status::Malformed);
    EXPECT_EQ(webapp::verify("=1", kBotToken, kNow).status, Status::Malformed);
}

TEST(WebAppTest, MalformedPercentEncodingIsRejected) {
    EXPECT_EQ(webapp::verify("a=%G1&hash=x", kBotToken, kNow).status, Status::BadEncoding);
    EXPECT_EQ(webapp::verify("a=%4&hash=x", kBotToken, kNow).status, Status::BadEncoding);
    EXPECT_EQ(webapp::verify("a=%", kBotToken, kNow).status, Status::BadEncoding);
}

TEST(WebAppTest, DuplicateFieldsAreRejected) {
    const auto init = std::string(kValidInit) + "&auth_date=1700000000";
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::DuplicateField);
    // A duplicate that only appears after decoding is still a duplicate.
    const std::string encoded_dup = "%61=1&a=2&hash=" + std::string(64, 'a');
    EXPECT_EQ(webapp::verify(encoded_dup, kBotToken, kNow).status, Status::DuplicateField);
}

TEST(WebAppTest, CorrectlySignedButNonObjectUserIsBadUser) {
    // Signed with the same token and algorithm; only the user payload is invalid.
    const std::string init
        = "auth_date=1700000000&user=not-json&hash=5244e2a259d7964581561ef123902347d65072e438655ffd682361c3715b0555";
    const auto r = webapp::verify(init, kBotToken, kNow);
    EXPECT_EQ(r.status, Status::BadUser);
    EXPECT_FALSE(r.data.has_value());
}

TEST(WebAppTest, CorrectlySignedUserWithoutFirstNameIsBadUser) {
    const std::string init = "auth_date=1700000000&user=%7B%22id%22%3A5%7D&hash="
                             "71a763edbcc43b7807e29e9499eb9177c67f78f51a7f9e460ea45a47f049b4a3";
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::BadUser);
}

TEST(WebAppTest, UnsignedBadUserIsBadSignatureNotBadUser) {
    // Parsed user data is never reported before the signature verifies.
    const std::string init = "user=not-json&auth_date=1700000000&hash=" + std::string(64, '0');
    EXPECT_EQ(webapp::verify(init, kBotToken, kNow).status, Status::BadSignature);
}

TEST(WebAppTest, ConstantTimeEqualBehaviour) {
    EXPECT_TRUE(webapp::constant_time_equal("abc", "abc"));
    EXPECT_TRUE(webapp::constant_time_equal("", ""));
    EXPECT_FALSE(webapp::constant_time_equal("abc", "abd"));
    EXPECT_FALSE(webapp::constant_time_equal("abc", "ab"));
    EXPECT_FALSE(webapp::constant_time_equal("", "a"));
    EXPECT_FALSE(webapp::constant_time_equal(std::string(64, 'a'), std::string(63, 'a') + "b"));
}

TEST(WebAppTest, HmacSha256MatchesRfc4231Vector) {
    // RFC 4231 test case 2: key "Jefe", data "what do ya want for nothing?"
    EXPECT_EQ(webapp::hmac_sha256_hex("Jefe", "what do ya want for nothing?"),
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST(WebAppTest, VerifyOrThrowThrowsAuthenticationErrorWithStatusOnly) {
    // kValidInit is from 2023, so with the system clock it is expired. The throw path is what matters here.
    try {
        (void)webapp::verify_or_throw(kValidInit, kBotToken);
        FAIL() << "expected AuthenticationError for an expired initData";
    } catch (const errors::AuthenticationError& e) {
        EXPECT_EQ(e.category(), errors::Category::Authentication);
        EXPECT_NE(std::string(e.what()).find("expired"), std::string::npos);
        EXPECT_EQ(std::string(e.what()).find(kBotToken), std::string::npos);
    }

    try {
        (void)webapp::verify_or_throw("", kBotToken);
        FAIL() << "expected AuthenticationError";
    } catch (const errors::AuthenticationError& e) {
        EXPECT_EQ(e.category(), errors::Category::Authentication);
        EXPECT_EQ(std::string(e.what()).find(kBotToken), std::string::npos);
    }
}

TEST(WebAppTest, ErrorMessagesNeverContainTheToken) {
    const auto r = webapp::verify(kValidInit, kBotToken, kNow + 999999);
    EXPECT_EQ(r.status, Status::Expired);
    EXPECT_EQ(std::string(webapp::to_string(r.status)).find("AAFake"), std::string::npos);
}
