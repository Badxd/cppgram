#!/usr/bin/env python3
"""End-to-end receive-path reference for the obfuscated MTProxy transport.

Builds the server's encrypted byte stream from first principles (intermediate or padded-intermediate
frames, plain MTProto packets inside them), splits it into TCP-like chunks, and records the frames
and plain bodies a correct client must produce. `cppgram_frames.cpp` runs the same stream through
CppGram's decrypt -> Transport -> unwrap_plain_message path and prints what it produced.

Usage:
  frames_reference.py vectors <out.txt>
  frames_reference.py verify  <vectors.txt> <cpp_out.txt>

Requires the `cryptography` package. Independent of CppGram code.
"""

import hashlib
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

TAG = {"intermediate": bytes.fromhex("eeeeeeee"), "padded": bytes.fromhex("dddddddd")}
SECRET = bytes(range(0x10, 0x20))


def ks(key, iv, n):
    return Cipher(algorithms.AES(key), modes.CTR(iv)).encryptor().update(b"\x00" * n)


def direction_key(raw32, secret):
    return hashlib.sha256(raw32 + secret).digest()


def build_init(seed, framing, dc):
    init = bytearray(((i * 37 + seed) & 0xFF) for i in range(64))
    init[0] = 0x11
    init[4:8] = bytes([1, 2, 3, 4])
    init[56:60] = TAG[framing]
    init[60:62] = dc.to_bytes(2, "little", signed=True)
    return bytes(init)


def rx_material(init, secret):
    rev = init[::-1]
    return direction_key(rev[8:40], secret), rev[40:56]


def server_encrypt(plain, key, iv):
    """Server -> client stream: CTR from keystream offset 0."""
    stream = ks(key, iv, len(plain))
    return bytes(a ^ b for a, b in zip(plain, stream))


def frame_bytes(payload, pad):
    n = len(payload) + len(pad)
    return n.to_bytes(4, "little") + payload + pad


def plain_packet(body, msg_id=0x5F5E100000000001):
    return (0).to_bytes(8, "little") + msg_id.to_bytes(8, "little") + len(body).to_bytes(4, "little") + body


def chunk(data, sizes):
    out, pos = [], 0
    for s in sizes:
        if pos >= len(data):
            break
        out.append(data[pos:pos + s])
        pos += s
    if pos < len(data):
        out.append(data[pos:])
    return out


def pad_of(n, seed):
    return bytes((seed + i * 17) & 0xFF for i in range(n))


CASES = []


def add_case(name, framing, mode, frames, sizes, dc=2, seed=3, truncate_tail=0):
    """frames: list of (payload, pad). mode: 'frames' or 'plain'."""
    init = build_init(seed, framing, dc)
    key, iv = rx_material(init, SECRET)
    wire_plain = b"".join(frame_bytes(p, pd) for p, pd in frames)
    if truncate_tail:
        wire_plain = wire_plain[:-truncate_tail]
    wire = server_encrypt(wire_plain, key, iv)
    # Expected: each complete length-delimited frame yields its bytes, padding included.
    # In plain mode the frame body is then judged by the plain-packet rule.
    expected = []
    pos = 0
    for p, pd in frames:
        size = 4 + len(p) + len(pd)
        if pos + size <= len(wire_plain):
            body = p + pd
            expected.append(plain_body_expected(body) if mode == "plain" else ("ok", body))
        pos += size
    CASES.append({
        "name": name, "framing": framing, "mode": mode, "dc": dc, "seed": seed,
        "init": init, "wire": wire, "sizes": sizes, "expected": expected,
    })


def plain_body_expected(body):
    """Mirror of the plain-packet rule (TDLib behaviour): the 4-byte length after msg_id is authoritative, any bytes
    after it are ignored, and fewer bytes than the length claims is truncation."""
    if len(body) < 20 or int.from_bytes(body[0:8], "little") != 0:
        return ("err", "not a plain packet")
    length = int.from_bytes(body[16:20], "little")
    remaining = len(body) - 20
    if length > remaining:
        return ("err", "truncated")
    return ("ok", body[20:20 + length])


def build_cases():
    CASES.clear()
    add_case("intermediate_split", "intermediate", "frames",
             [(b"", b""), (b"\x01", b""), (pad_of(16, 1), b""), (pad_of(300, 9), b"")],
             sizes=[1, 3, 17, 70])
    add_case("padded_pad_each", "padded", "frames",
             [(pad_of(20 + i, i), pad_of(i, 50 + i)) for i in range(16)],
             sizes=[4, 4, 4, 1, 1, 200])
    add_case("truncated_tail", "intermediate", "frames",
             [(pad_of(40, 2), b""), (pad_of(40, 4), b"")], sizes=[10, 500], truncate_tail=5)
    add_case("plain_pad0", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), b"")], sizes=[9999])
    add_case("plain_pad15", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(15, 3))], sizes=[9999])
    add_case("plain_pad16_accepted", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(16, 3))], sizes=[9999])
    add_case("plain_pad22_accepted", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(22, 3))], sizes=[9999])
    add_case("plain_pad96_accepted", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(96, 3))], sizes=[9999])
    add_case("plain_pad102_accepted", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(102, 3))], sizes=[9999])
    add_case("plain_pad116_accepted", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(116, 3))], sizes=[9999])
    add_case("plain_pad123_accepted", "padded", "plain",
             [(plain_packet(pad_of(40, 7)), pad_of(123, 3))], sizes=[9999])
    add_case("plain_two_coalesced_split", "padded", "plain",
             [(plain_packet(pad_of(33, 1)), pad_of(5, 2)),
              (plain_packet(pad_of(64, 4)), pad_of(9, 6))], sizes=[3, 30, 40, 7])
    add_case("plain_length_too_big", "intermediate", "plain",
             [(plain_packet(pad_of(40, 7))[:16] + (500).to_bytes(4, "little") + pad_of(40, 7), b"")],
             sizes=[9999])


def write_vectors(path):
    build_cases()
    lines = []
    for c in CASES:
        lines.append(f"case {c['name']}")
        lines.append(f"framing {c['framing']}")
        lines.append(f"mode {c['mode']}")
        lines.append(f"dc {c['dc']}")
        lines.append(f"secret {SECRET.hex()}")
        lines.append(f"init {c['init'].hex()}")
        lines.append("sizes " + " ".join(str(s) for s in c["sizes"]))
        lines.append(f"wire {c['wire'].hex()}")
        for kind, val in c["expected"]:
            if kind == "ok":
                lines.append(f"expect ok {val.hex()}")
            else:
                lines.append(f"expect err {val[1]}")
        lines.append("end")
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")


def read_blocks(path):
    cases, cur = [], None
    with open(path) as f:
        for raw in f:
            parts = raw.rstrip("\n").split(" ")
            if parts[0] == "case":
                cur = {"name": parts[1], "expect": []}
            elif parts[0] == "end":
                cases.append(cur)
                cur = None
            elif parts[0] == "expect":
                cur["expect"].append((parts[1], " ".join(parts[2:])))
            elif parts[0] == "sizes":
                cur["sizes"] = [int(x) for x in parts[1:]]
            else:
                cur[parts[0]] = " ".join(parts[1:])
    return cases


def verify(vec_path, out_path):
    cases = read_blocks(vec_path)
    got = {}
    cur = None
    with open(out_path) as f:
        for raw in f:
            parts = raw.rstrip("\n").split(" ", 1)
            if parts[0] == "case":
                cur = parts[1]
                got[cur] = []
            elif parts[0] == "item":
                got[cur].append(parts[1])
    failures = 0
    for c in cases:
        exp = []
        for kind, val in c["expect"]:
            exp.append(val if kind == "ok" else "ERR")
        mine = got.get(c["name"], [])
        ok = True
        if len(mine) != len(exp):
            ok = False
        else:
            for a, b in zip(mine, exp):
                if b == "ERR":
                    if not a.startswith("ERR"):
                        ok = False
                elif a != b:
                    ok = False
        print(("PASS " if ok else "FAIL ") + c["name"] + f"  items: expected {len(exp)}, cppgram {len(mine)}")
        if not ok:
            failures += 1
            print("  expected:", exp)
            print("  cppgram: ", mine)
    return failures


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "vectors":
        write_vectors(sys.argv[2])
    elif len(sys.argv) >= 4 and sys.argv[1] == "verify":
        sys.exit(1 if verify(sys.argv[2], sys.argv[3]) else 0)
    else:
        print(__doc__)
        sys.exit(2)
