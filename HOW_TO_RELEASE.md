# How to prepare a release

This repository does not publish anything. It builds a source archive on your machine. Tagging, uploading and announcing
are manual steps you take yourself. No workflow in this repository creates a tag, a release or a push.

1. Decide the version. Follow Semantic Versioning. Until 1.0.0, a minor version may change the public API.
   Record the basis in `CHANGELOG.md` and `RELEASE_NOTES.md`.
2. Set `VERSION`. Set `app_version` in `include/cppgram/client.h` to the same value. Add a `## <version>` heading to
   `CHANGELOG.md`. Then run `bash tools/check_version.sh`.
3. Run the fast checks: `make checks` (source lists, version, format on the allowlist, examples syntax).
4. Run the schema check if any schema or generator file changed: `make schema-check`.
5. Run the offline suites: `make test`. A release needs a fresh result, not a historical one. Keep the logs.
6. Build the archive: `make package`. It writes `dist/cppgram-<version>-source.zip` (or the `RELEASE_ZIP` you set). The
   script prints the zip's SHA-256. Record that value outside the archive.
7. Verify the archive: `make verify-release` (or `bash tools/verify_release.sh <zip>`). It checks zip integrity, extracts
   into an empty directory, checks every entry in the bundled `SHA256SUMS.txt`, and checks required files and forbidden paths.
8. Keep the archive out of git. `dist/` is excluded from the archive and from the repository.
9. If you publish, do it yourself: create the tag, upload the archive and the SHA-256 value, and attach
   `RELEASE_NOTES.md`. Do not publish a live-verified claim unless the live checks were run and their output recorded.

The `Release` workflow (`.github/workflows/release.yml`) runs steps 3 to 7 when you start it by hand. It uploads the
archive as a workflow artifact with a short retention period. It does not publish anything.
