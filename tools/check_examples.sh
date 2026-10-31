#!/usr/bin/env bash
# Syntax-checks every example against the public headers. Does not link or contact Telegram.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
while IFS= read -r f; do
    [ -f "$f" ] || continue
    if g++ -std=c++20 -fsyntax-only -Wall -I"$ROOT/include" -I"$ROOT/src" "$f" 2>/tmp/check_examples.err; then
        echo "OK   ${f#$ROOT/}"
    else
        echo "FAIL ${f#$ROOT/}"; cat /tmp/check_examples.err; fail=1
    fi
done < <(find "$ROOT/examples" -name "*.cpp" | sort)
exit $fail
