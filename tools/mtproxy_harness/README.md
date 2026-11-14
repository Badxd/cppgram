# MTProxy obfuscated-transport harness

Independent check of CppGram's obfuscated handshake, key derivation, and keystream. `reference.py`
is written from the transport description. It does not use CppGram code. `cppgram_dump.cpp` is the
only part that touches CppGram.

Steps (run from this directory):

```sh
python3 reference.py vectors /path/to/vectors.txt
g++ -std=c++20 -I ../../include cppgram_dump.cpp -o /path/to/cppgram_dump /path/to/libcppgram.a -lssl -lcrypto -lpthread
/path/to/cppgram_dump /path/to/vectors.txt > /path/to/cpp_out.txt
python3 -I reference.py verify /path/to/vectors.txt /path/to/cpp_out.txt
```

Checks per case: the 64-byte handshake (tag, DC, key/IV derivation, bytes 56..64 encryption),
TX ciphertext continuing at keystream offset 64, and RX keystream derived from the reversed init.
Requires the `cryptography` package. Vectors use a fixed 16-byte secret and are deterministic.

The secret, API hash, and bot token are never part of these files. The secret here is a test
constant.

## Receive path (frames)

`frames_reference.py` builds the server's encrypted stream from first principles (intermediate and
padded frames, plain packets, chunked reads) and records what a correct client must produce.
`cppgram_frames.cpp` runs the same bytes through CppGram's decrypt -> Transport -> unwrap path.

```sh
python3 frames_reference.py vectors vectors/frames_vectors.txt   # regenerates the committed vectors
g++ -std=c++20 -I ../../include cppgram_frames.cpp -o cppgram_frames /path/to/libcppgram.a -lssl -lcrypto -lpthread
./cppgram_frames vectors/frames_vectors.txt > frames_out.txt
python3 -I frames_reference.py verify vectors/frames_vectors.txt frames_out.txt
```

Covered: split and coalesced reads, byte-at-a-time headers, empty frames, padding 0..15 per frame,
truncated tail (no frame emitted), plain packets with 0 / 15 / 16 / 22 / 96 / 102 / 116 / 123 trailing bytes (all accepted; the length field is authoritative, as in TDLib), two
coalesced plain packets in one read, and a plain length field larger than the frame.

Vectors are regenerated deterministically; the committed files under `vectors/` are the reference.
The fixed test secret (`101112...1f`) is a test constant, not a credential.

## Temporary vs release files

Release: `reference.py`, `frames_reference.py`, `cppgram_dump.cpp`, `cppgram_frames.cpp`, `README.md`, `vectors/`.
Temporary (not in the release): compiled binaries and `/tmp`-style working outputs under the scratchpad.
