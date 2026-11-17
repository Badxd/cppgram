#!/usr/bin/env bash
# Checks that the release version is consistent across the files that state it:
#   VERSION                 the single source (CMake reads it)
#   include/cppgram/client.h  app_version, the version the client announces to Telegram
#   CHANGELOG.md            has a heading for this version
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
status=0
version="$(tr -d '[:space:]' < VERSION)"
if [[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+(-[0-9A-Za-z.]+)?$ ]]; then
    echo "OK   VERSION is a semantic version: $version"
else
    echo "FAIL VERSION is not a semantic version: '$version'"; status=1
fi
client_version="$(grep -o 'app_version = "[^"]*"' include/cppgram/client.h | head -1 | cut -d'"' -f2)"
if [ "$client_version" = "$version" ]; then
    echo "OK   client.h app_version matches VERSION"
else
    echo "FAIL client.h app_version is '$client_version', VERSION is '$version'"; status=1
fi
if grep -q "^## $version" CHANGELOG.md; then
    echo "OK   CHANGELOG.md has a heading for $version"
else
    echo "FAIL CHANGELOG.md has no '## $version' heading"; status=1
fi
exit $status
