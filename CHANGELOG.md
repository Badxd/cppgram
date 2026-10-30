# Changelog

## Unreleased

### Security
- DH parameters from the server are validated (2048-bit safe prime, generator residue, `1 < g_a < p-1`).
- `random_id` values come from OpenSSL `RAND_bytes` instead of `std::mt19937_64`.
- Session files and the sqlite session store are created with mode `0600`.
- msg_key verification uses a constant-time comparison.

### Tooling
- GitHub Actions: Release build and tests, plus an ASan/UBSan job.
- `tools/check_examples.sh` syntax-checks all examples.
- `tools/run_sanitizers.sh` runs the test suite under sanitizers locally.

### Fuzzing
- libFuzzer targets for the TL reader, session string decoder, and HTML/Markdown parser (`fuzz/`, `-DBUILD_FUZZERS=ON`).

### Docs
- `SECURITY.md`, `AUDIT.md`.
