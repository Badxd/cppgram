// Internal helper shared by the 2FA password paths (set_password, recover_password). Not installed.
#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

#include "cppgram/mtproto/crypto.h"
#include "cppgram/mtproto/schema/api.h"
#include "cppgram/mtproto/srp.h"

namespace cppgram::detail {

// Builds account.passwordInputSettings that set a new 2FA password. The server's suggested salt1 is extended with
// 32 random bytes so the stored verifier is not tied to the suggested salt. Throws when the server sent no usable
// KDF parameters.
inline mtproto::tl::types::account_passwordInputSettings make_new_password_settings(
    const mtproto::tl::types::account_password& pw, const std::string& new_password, const std::string& hint) {
    namespace ty = mtproto::tl::types;
    namespace tl = mtproto::tl;

    if (new_password.empty()) throw std::invalid_argument("new password must not be empty");
    if (!pw.new_algo) throw std::runtime_error("server sent no new password parameters");
    const auto* algo = std::get_if<ty::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow>(
        &pw.new_algo->value);
    if (algo == nullptr) throw std::runtime_error("server uses an unsupported password KDF algorithm");

    ty::passwordKdfAlgoSHA256SHA256PBKDF2HMACSHA512iter100000SHA256ModPow new_algo;
    new_algo.salt1 = algo->salt1;
    const mtproto::Bytes extra = mtproto::random_bytes(32);
    new_algo.salt1.insert(new_algo.salt1.end(), extra.begin(), extra.end());
    new_algo.salt2 = algo->salt2;
    new_algo.g = algo->g;
    new_algo.p = algo->p;

    ty::account_passwordInputSettings settings;
    settings.new_algo = std::make_shared<tl::PasswordKdfAlgo>(tl::PasswordKdfAlgo{new_algo});
    settings.new_password_hash = mtproto::srp_verifier(
        mtproto::Bytes(new_password.begin(), new_password.end()), new_algo.salt1, new_algo.salt2, new_algo.g,
        new_algo.p);
    settings.hint = hint;
    return settings;
}

}  // namespace cppgram::detail
