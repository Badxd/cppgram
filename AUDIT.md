# Audit status

Status of the audit checkpoints. Each item says what was verified and what was not.

| Area | Status | Evidence |
|---|---|---|
| DH parameters in handshake | Fixed | `validate_dh_params` in `src/mtproto/auth.cpp`; tests in `handshake_test.cpp` |
| random_id generation | Fixed | `src/secure_random.h` used by 7 client files; test `SecureRandomIdsAreDistinct` |
| Session file permissions | Fixed | `src/secure_file.h`; test `PrivateFileIsOwnerOnly` |
| msg_key comparison | Fixed | `constant_time_equal` in `crypto.cpp`; test added |
| msg_key / AES-IGE key derivation | Reviewed | Matches the MTProto 2.0 formulas for client (x=0) and server (x=8) |
| TL reader bounds | Reviewed | `require()` on every read; vector counts checked against remaining data |
| `parser.cpp` attribute loop | Reviewed | Always advances `pos`; terminates |
| SQL in `storage.cpp` | Reviewed | Prepared statements with bound parameters |
| Sanitizers | Wired in CI and `tools/run_sanitizers.sh` | Not yet run to completion in this sandbox |
| Fuzzers | Not started | Planned: TL reader, `decode_session_string`, HTML parser |
| Benchmarks | Not started | — |
| Full clean baseline build | Not done in this batch | Incremental rebuild only; run a clean build before release |

Nothing here is a claim of production readiness. The handshake was live-tested on the VPS
in earlier batches; the rest is verified by unit tests and reading the code.
