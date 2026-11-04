# Versioning and API compatibility

## Version numbers

cppgram uses semantic versioning: `MAJOR.MINOR.PATCH`.

- **PATCH**: bug fixes and internal changes. No public header change that breaks a caller.
- **MINOR**: new functions, new fields with defaults, new optional behaviour. Existing code still compiles and
  behaves the same.
- **MAJOR**: removed or renamed public names, changed meaning of a parameter, changed storage format without a
  reader for the old one, changed error types callers catch.

Pre-1.0 releases may change in MINOR versions. Each breaking change is listed under a "Breaking" heading in
`CHANGELOG.md` with the migration step (see `guides/MIGRATION.md`).

## What counts as public API

- Everything under `include/cppgram/` except files whose name contains `internal`, and generated TL types in
  `mtproto/schema/api.h`.
- Behaviour documented in `guides/` and in header comments.
- Session file format and SQLite schema version (see `guides/SESSIONS.md`).

Anything under `src/` is internal.

## Rules for changes

1. Add, do not rename. Deprecate with a comment, keep the old name for at least one MINOR release.
2. New `ClientOptions` fields must have defaults that keep the old behaviour.
3. New error conditions must use the existing exception types (`RpcError`, `ConnectionError`,
   `SessionCorruptError`, and so on) so callers' `catch` blocks still work.
4. Storage formats: a new version must still read the previous one. The SQLite store refuses newer versions, so
   a downgrade must not be attempted.
5. TL schema updates change generated code. Bump the layer and note it in the changelog.

## Checks before a release

- `ctest` passes in a Release build.
- `tools/check_schema.sh` passes (generated code matches the schema, layer agrees with `kLayer`).
- `tools/check_headers.sh` passes (each public header compiles alone).
- `tools/check_examples.sh` passes.
- The changelog has an entry for every public change.

These checks run in CI (`.github/workflows/ci.yml`), which has not run on GitHub from this repository's sandbox.
