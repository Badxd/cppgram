#!/usr/bin/env bash
# Offline checks for cppgram_live_tests. Nothing here touches the network:
#   - no credentials: every Telegram test must SKIP (exit 3)
#   - fake-TLS (ee) proxy secret: mtproto_proxy must report NOT_SUPPORTED (exit 4) before connecting
#   - bad usage: exit 2
# Usage: tools/check_live_tests_offline.sh <path-to-cppgram_live_tests>
set -u

BIN="${1:?usage: $0 <path-to-cppgram_live_tests>}"
if [ ! -x "$BIN" ]; then
    echo "FAIL: $BIN is not executable" >&2
    exit 1
fi

FAILS=0
expect() {
    # expect <label> <expected exit> <expected outcome text> -- <command...>
    local label="$1" want_exit="$2" want_text="$3"
    shift 3
    [ "$1" = "--" ] && shift
    local out
    out="$(env -i PATH="$PATH" HOME="${HOME:-/tmp}" "$@" 2>&1)"
    local got=$?
    if [ "$got" -ne "$want_exit" ] || ! grep -q "$want_text" <<<"$out"; then
        echo "FAIL $label: exit=$got (want $want_exit), output did not contain '$want_text'"
        echo "$out" | sed 's/^/    /'
        FAILS=$((FAILS + 1))
    else
        echo "ok   $label"
    fi
}

TESTS="reconnect flood_wait socks5 session_restore mtproto_proxy padded_framing"
for t in $TESTS; do
    expect "$t without credentials skips" 3 "RESULT $t: SKIP" -- "$BIN" "$t"
done

expect "fake-TLS secret is NOT_SUPPORTED offline" 4 "RESULT mtproto_proxy: NOT_SUPPORTED" -- \
    TG_API_ID=1 TG_API_HASH=x TG_BOT_TOKEN=x \
    TG_MTPROTO_PROXY_HOST=127.0.0.1 TG_MTPROTO_PROXY_PORT=1 \
    TG_MTPROTO_PROXY_SECRET=ee000000000000000000000000000000006578616d706c652e636f6d "$BIN" mtproto_proxy

expect "plain secret without credentials skips" 3 "RESULT mtproto_proxy: SKIP" -- \
    TG_MTPROTO_PROXY_HOST=127.0.0.1 TG_MTPROTO_PROXY_PORT=1 \
    TG_MTPROTO_PROXY_SECRET=00000000000000000000000000000000 "$BIN" mtproto_proxy

expect "padded test rejects a plain secret" 3 "RESULT padded_framing: SKIP" -- \
    TG_API_ID=1 TG_API_HASH=x TG_BOT_TOKEN=x \
    TG_MTPROTO_PROXY_HOST=127.0.0.1 TG_MTPROTO_PROXY_PORT=1 \
    TG_MTPROTO_PROXY_SECRET=00000000000000000000000000000000 "$BIN" padded_framing

expect "socks5 without proxy settings skips" 3 "RESULT socks5: SKIP" -- \
    TG_API_ID=1 TG_API_HASH=x TG_BOT_TOKEN=x "$BIN" socks5

expect "bad secret format is FAIL without echoing it" 1 "RESULT mtproto_proxy: FAIL" -- \
    TG_API_ID=1 TG_API_HASH=x TG_BOT_TOKEN=x \
    TG_MTPROTO_PROXY_HOST=127.0.0.1 TG_MTPROTO_PROXY_PORT=1 \
    TG_MTPROTO_PROXY_SECRET=not-hex-at-all-nope "$BIN" mtproto_proxy

expect "unknown test name is a usage error" 2 "usage:" -- "$BIN" nope

if [ "$FAILS" -ne 0 ]; then
    echo "$FAILS offline check(s) failed"
    exit 1
fi
echo "all offline checks passed"
