#pragma once

#include <string>

#include "cppgram/mtproto/session.h"

namespace cppgram {

// Packs a saved session (DC, auth key, salt, time offset) into one base64 line that can be
// kept in an env var or a secret store and passed back as ClientOptions::session_string.
std::string encode_session_string(const mtproto::SessionData& data);

// Parses a string made by encode_session_string. Returns false when the string is malformed.
bool decode_session_string(const std::string& text, mtproto::SessionData& out);

}  // namespace cppgram
