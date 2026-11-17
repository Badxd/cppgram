#!/usr/bin/env python3
"""Independent MTProxy obfuscated-transport reference.

Written from the MTProto transport description, not from CppGram's code. It uses only the
`cryptography` package (AES-256-CTR and SHA-256) and produces deterministic vectors that the
C++ dump tool (cppgram_dump.cpp) must reproduce byte for byte.

Usage:
  reference.py vectors  <out_vectors.txt>   write the input vectors
  reference.py verify   <vectors.txt> <cpp_output.txt>   compare CppGram output with the reference
"""

import hashlib
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TAG = {
    "intermediate": bytes.fromhex("eeeeeeee"),
    "padded": bytes.fromhex("dddddddd"),
    "abridged": bytes.fromhex("efefefef"),
}
MAX_PAD = 15


def keystream(key: bytes, iv: bytes, n: int) -> bytes:
    enc = Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor()
    return enc.update(b"\x00" * n)


def ctr_apply(key: bytes, iv: bytes, offset: int, data: bytes) -> bytes:
    """AES-CTR applied to `data` starting at keystream byte `offset`."""
    ks = keystream(key, iv, offset + len(data))[offset:]
    return bytes(a ^ b for a, b in zip(data, ks))


def direction_key(raw32: bytes, secret: bytes) -> bytes:
    return hashlib.sha256(raw32 + secret).digest()


def build_init(seed: int, framing: str, dc: int) -> bytes:
    """Deterministic 64-byte plaintext init. Tag at 56..60, DC int16 LE at 60..62."""
    init = bytearray(((i * 37 + seed) & 0xFF) for i in range(64))
    # Avoid the forbidden prefixes so the vector is a valid client init.
    init[0] = 0x11
    init[4:8] = bytes([1, 2, 3, 4])
    init[56:60] = TAG[framing]
    init[60:62] = dc.to_bytes(2, "little", signed=True)
    return bytes(init)


def client_handshake(init: bytes, secret: bytes):
    """Returns (wire_handshake_64, tx_key, tx_iv, rx_key, rx_iv)."""
    tx_key = direction_key(init[8:40], secret)
    tx_iv = init[40:56]
    rev = init[::-1]
    rx_key = direction_key(rev[8:40], secret)
    rx_iv = rev[40:56]
    # Bytes 56..64 are encrypted with the TX stream at keystream offset 56.
    ks = keystream(tx_key, tx_iv, 64)
    wire = bytearray(init)
    for i in range(56, 64):
        wire[i] = init[i] ^ ks[i]
    return bytes(wire), tx_key, tx_iv, rx_key, rx_iv


def server_view(wire: bytes, secret: bytes):
    """Proxy side: recovers the init from the wire handshake and the keys it will use."""
    # The proxy decrypts the tail with the keystream of the client's TX key.
    tx_key_c = direction_key(wire[8:40], secret)
    ks = keystream(tx_key_c, wire[40:56], 64)
    init = bytearray(wire)
    for i in range(56, 64):
        init[i] = wire[i] ^ ks[i]
    return bytes(init)


def expected_for_case(case: dict) -> dict:
    framing = case["framing"]
    secret = bytes.fromhex(case["secret"])
    init = bytes.fromhex(case["init"])
    wire, tx_key, tx_iv, rx_key, rx_iv = client_handshake(init, secret)

    # Client -> proxy: continue the TX stream at offset 64.
    tx_plain = bytes.fromhex(case["tx_plain"])
    tx_cipher = ctr_apply(tx_key, tx_iv, 64, tx_plain)

    # Proxy -> client: the proxy encrypts with the client's RX key (reversed init), stream at offset 0.
    rx_plain = bytes.fromhex(case["rx_plain"])
    rx_cipher = ctr_apply(rx_key, rx_iv, 0, rx_plain)

    return {
        "handshake": wire.hex(),
        "tx_cipher": tx_cipher.hex(),
        "rx_cipher": rx_cipher.hex(),
        "server_init_check": server_view(wire, secret).hex() == init.hex(),
    }


CASES = [
    {"name": "intermediate_plain", "framing": "intermediate", "dc": 2, "seed": 3, "secret_kind": "plain"},
    {"name": "padded_dd", "framing": "padded", "dc": 1, "seed": 91, "secret_kind": "dd"},
    {"name": "abridged_plain", "framing": "abridged", "dc": 5, "seed": 170, "secret_kind": "plain"},
    {"name": "dc_negative", "framing": "intermediate", "dc": -4, "seed": 7, "secret_kind": "plain"},
]

SECRET = bytes(range(0x10, 0x20))  # 16-byte secret, deterministic


def make_vectors():
    out = []
    for c in CASES:
        init = build_init(c["seed"], c["framing"], c["dc"])
        tx_plain = bytes((i * 13 + c["seed"]) & 0xFF for i in range(64 + 8))
        rx_plain = bytes((i * 29 + c["seed"]) & 0xFF for i in range(40))
        case = {
            "name": c["name"],
            "framing": c["framing"],
            "dc": str(c["dc"]),
            "secret": SECRET.hex(),
            "init": init.hex(),
            "tx_plain": tx_plain.hex(),
            "rx_plain": rx_plain.hex(),
        }
        out.append(case)
    return out


def write_vectors(path):
    lines = []
    for case in make_vectors():
        lines.append(f"case {case['name']}")
        for k in ("framing", "dc", "secret", "init", "tx_plain", "rx_plain"):
            lines.append(f"{k} {case[k]}")
        lines.append("end")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def read_vectors(path):
    cases, cur = [], None
    with open(path) as f:
        for raw in f:
            parts = raw.split()
            if not parts:
                continue
            if parts[0] == "case":
                cur = {"name": parts[1]}
            elif parts[0] == "end":
                cases.append(cur)
                cur = None
            else:
                cur[parts[0]] = parts[1]
    return cases


def read_cpp(path):
    res = {}
    cur = None
    with open(path) as f:
        for raw in f:
            parts = raw.split()
            if not parts:
                continue
            if parts[0] == "case":
                cur = parts[1]
                res[cur] = {}
            elif parts[0] != "end":
                res[cur][parts[0]] = parts[1]
    return res


def verify(vec_path, cpp_path):
    cases = read_vectors(vec_path)
    got = read_cpp(cpp_path)
    failures = 0
    for case in cases:
        exp = expected_for_case(
            {**case, "framing": case["framing"]}
        )
        name = case["name"]
        ok = True
        for key in ("handshake", "tx_cipher", "rx_cipher"):
            if got.get(name, {}).get(key) != exp[key]:
                ok = False
                print(f"FAIL {name} {key}")
                print(f"  reference: {exp[key]}")
                print(f"  cppgram:   {got.get(name, {}).get(key)}")
        if not exp["server_init_check"]:
            ok = False
            print(f"FAIL {name} server_init_check")
        print(("PASS " if ok else "FAIL ") + name)
        failures += 0 if ok else 1
    return failures


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "vectors":
        write_vectors(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "verify":
        sys.exit(1 if verify(sys.argv[2], sys.argv[3]) else 0)
    else:
        print(__doc__)
        sys.exit(2)
