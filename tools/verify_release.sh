#!/usr/bin/env bash
# Verifies a release archive made by tools/package_release.py:
#   1. zip integrity (unzip -t),
#   2. extraction into a fresh empty directory,
#   3. every entry of the bundled SHA256SUMS.txt matches, and every file in the archive is listed,
#   4. the required files are present and no excluded path is present.
# Usage: bash tools/verify_release.sh path/to/archive.zip
set -euo pipefail
ZIP="${1:?usage: verify_release.sh ARCHIVE.zip}"
[ -f "$ZIP" ] || { echo "FAIL no such archive: $ZIP" >&2; exit 2; }
for tool in unzip sha256sum; do
    command -v "$tool" >/dev/null || { echo "FAIL $tool is required" >&2; exit 2; }
done

status=0
echo "zip sha256: $(sha256sum "$ZIP" | cut -d' ' -f1)"

if unzip -tq "$ZIP" >/dev/null; then
    echo "OK   zip integrity (unzip -t)"
else
    echo "FAIL zip integrity"; exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
unzip -q "$ZIP" -d "$WORK"
tops="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d | wc -l)"
if [ "$tops" -ne 1 ]; then
    echo "FAIL expected one top-level folder, found $tops"; exit 1
fi
ROOT_DIR="$(find "$WORK" -mindepth 1 -maxdepth 1 -type d)"
echo "OK   extracted one top-level folder: $(basename "$ROOT_DIR")"

if [ ! -f "$ROOT_DIR/SHA256SUMS.txt" ]; then
    echo "FAIL SHA256SUMS.txt missing from the archive"; exit 1
fi
(
    cd "$ROOT_DIR"
    sha256sum -c --quiet SHA256SUMS.txt
) && echo "OK   SHA256SUMS.txt: every listed file matches" || { echo "FAIL checksum mismatch"; status=1; }

listed="$(awk '{ print $2 }' "$ROOT_DIR/SHA256SUMS.txt" | sort)"
actual="$(cd "$ROOT_DIR" && find . -type f ! -path './SHA256SUMS.txt' | sed 's|^\./||' | sort)"
if [ "$listed" = "$actual" ]; then
    echo "OK   every file in the archive is listed ($(echo "$listed" | wc -l) files)"
else
    echo "FAIL the checksum list and the archive contents differ"
    diff <(echo "$listed") <(echo "$actual") | head -20
    status=1
fi

required="VERSION CHANGELOG.md RELEASE_NOTES.md README.md CMakeLists.txt Makefile LICENSE NOTICE FINAL_REPORT.md
IMPLEMENTATION_STATUS.md PARITY_MATRIX.md KNOWN_LIMITATIONS.md VPS_TESTING_GUIDE.md TEST_RESULTS.md BUILD_RESULTS.md
schema/SHA256SUMS.txt schema/README.md include/cppgram/client.h include/cppgram/mtproto/schema/api.h
src/mtproto/schema/api_impl_0.cpp tests/CMakeLists.txt tools/check_schema.sh tools/package_release.py
tools/verify_release.sh .github/workflows/ci.yml cmake/cppgramConfig.cmake.in"
missing=0
for f in $required; do
    [ -f "$ROOT_DIR/$f" ] || { echo "FAIL missing required file: $f"; missing=1; }
done
if [ "$missing" -eq 0 ]; then
    echo "OK   required files present"
else
    status=1
fi

bad="$(cd "$ROOT_DIR" && find . \( -path './build*' -o -name '*.session' -o -name '*.session-journal' -o -name '*.db' \
    -o -name '*.sqlite' -o -name '*.log' -o -name '*.o' -o -name '*.a' -o -name '*.so' -o -name '.env' -o -name '*.key' \
    -o -name '*.pem' -o -name 'compile_commands.json' \) -print | head -20)"
if [ -z "$bad" ]; then
    echo "OK   no build outputs, sessions, databases, logs or credential-like files"
else
    echo "FAIL excluded paths present:"; echo "$bad"; status=1
fi

if [ "$status" -eq 0 ]; then
    echo "VERIFY PASSED"
else
    echo "VERIFY FAILED"
fi
exit "$status"
