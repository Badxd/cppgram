#!/usr/bin/env bash
# Rewrites schema/SHA256SUMS.txt from the current inputs, generator and generated files.
# Run it only after a reviewed schema or generator change, then run tools/check_schema.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
{
    sha256sum schema/main_api.tl schema/sys_msgs.tl schema/auth_key.tl tools/tl_codegen/tl_codegen.cpp
    sha256sum include/cppgram/mtproto/schema/api.h
    sha256sum src/mtproto/schema/api_impl_*.cpp | sort -k2
} > schema/SHA256SUMS.txt
echo "wrote schema/SHA256SUMS.txt ($(wc -l < schema/SHA256SUMS.txt) entries)"
