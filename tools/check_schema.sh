#!/usr/bin/env bash
# Reproducibility check for the TL code generator:
#   1. regenerates the schema code from schema/*.tl into a temp directory,
#   2. diffs it against the checked-in include/cppgram/mtproto/schema/api.h and src/mtproto/schema/*.cpp,
#   3. checks that the layer in schema/main_api.tl matches the layer the client sends (kLayer in src/client.cpp).
# Needs the cppgram_tl_codegen binary: pass its path as $1, or it is looked up in build/.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CODEGEN="${1:-}"
if [ -z "$CODEGEN" ]; then
    CODEGEN="$(find "$ROOT/build" -maxdepth 3 -name cppgram_tl_codegen -type f | head -1)"
fi
if [ -z "$CODEGEN" ] || [ ! -x "$CODEGEN" ]; then
    echo "cppgram_tl_codegen not found; build it first (cmake --build build --target cppgram_tl_codegen)" >&2
    exit 2
fi
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/src/mtproto/schema"
"$CODEGEN" "$WORK/api.h" "$WORK/src/mtproto/schema" \
    "$ROOT/schema/main_api.tl" "$ROOT/schema/sys_msgs.tl" "$ROOT/schema/auth_key.tl"

status=0
if diff -q "$WORK/api.h" "$ROOT/include/cppgram/mtproto/schema/api.h" >/dev/null; then
    echo "OK   api.h"
else
    echo "FAIL api.h differs from regenerated output"; status=1
fi
for f in "$WORK"/src/mtproto/schema/*.cpp; do
    b="$(basename "$f")"
    if diff -q "$f" "$ROOT/src/mtproto/schema/$b" >/dev/null; then
        echo "OK   $b"
    else
        echo "FAIL $b differs from regenerated output"; status=1
    fi
done

schema_layer="$(grep -o 'LAYER [0-9]*' "$ROOT/schema/main_api.tl" | head -1 | awk '{print $2}')"
client_layer="$(grep -o 'kLayer = [0-9]*' "$ROOT/src/client.cpp" | head -1 | awk '{print $3}')"
if [ "$schema_layer" = "$client_layer" ]; then
    echo "OK   layer $schema_layer (schema and client agree)"
else
    echo "FAIL layer mismatch: schema=$schema_layer client=$client_layer"; status=1
fi
exit $status
