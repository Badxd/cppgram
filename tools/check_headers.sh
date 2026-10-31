#!/usr/bin/env bash
# Compiles every public header on its own (include-what-you-use style check) with -fsyntax-only.
# Catches headers that rely on includes from other headers. Does not link.
# *.inc.h fragments are included only from their parent header (Api.h) and are skipped.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
fail=0
tmp="$(mktemp -d)"; trap 'rm -rf "$tmp"' EXIT
while IFS= read -r h; do
    rel="${h#$ROOT/include/}"
    printf '#include "%s"\n' "$rel" > "$tmp/one.cpp"
    if g++ -std=c++20 -fsyntax-only -Wall -Wextra -I"$ROOT/include" "$tmp/one.cpp" 2> "$tmp/err"; then
        echo "OK   $rel"
    else
        echo "FAIL $rel"; head -20 "$tmp/err"; fail=1
    fi
done < <(find "$ROOT/include/cppgram" -name '*.h' ! -path '*/schema/*' ! -name '*.inc.h' | sort)
exit $fail
