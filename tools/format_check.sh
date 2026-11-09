#!/usr/bin/env bash
# Formatting check. Checks only the files in tools/format_allowlist.txt, which already pass clang-format-18.
# The rest of the tree is not checked, and it is not reformatted: that would touch about 190 unrelated files.
# Set CLANG_FORMAT to another binary to use a different clang-format. CI pins clang-format-18.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
CF="${CLANG_FORMAT:-clang-format-18}"
if ! command -v "$CF" >/dev/null 2>&1; then
    echo "format-check: $CF not found" >&2
    exit 2
fi
checked=0; failed=0
while IFS= read -r f; do
    case "$f" in ""|\#*) continue ;; esac
    checked=$((checked + 1))
    if [ ! -f "$f" ]; then
        echo "FAIL missing file listed in the allowlist: $f"; failed=$((failed + 1)); continue
    fi
    if ! "$CF" --dry-run --Werror "$f" >/dev/null 2>&1; then
        echo "FAIL not formatted: $f"; failed=$((failed + 1))
    fi
done < tools/format_allowlist.txt
echo "format-check ($("$CF" --version | head -1)): $checked files, $failed failing"
[ "$failed" -eq 0 ]
