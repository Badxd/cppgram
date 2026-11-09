# Telegram TL schema inputs

This directory holds the Telegram TL schema the MTProto client is generated from. The C++ code is generated from these
files, and the checked-in generated files must match a regeneration exactly.

## Authoritative version

- **Layer 233.** `schema/main_api.tl` marks it with `// LAYER 233`. The client sends the same layer: `kLayer = 233` in
  `src/client.cpp`. `tools/check_schema.sh` fails if the two differ.

## Inputs

| File | Role | Checksum |
|---|---|---|
| `main_api.tl` | Telegram API schema, layer 233 (types and functions) | `schema/SHA256SUMS.txt` |
| `sys_msgs.tl` | MTProto system messages | `schema/SHA256SUMS.txt` |
| `auth_key.tl` | Authorization key creation messages | `schema/SHA256SUMS.txt` |

The header of `main_api.tl` cites the tdesktop and tdlib schema files as its sources. `NOTICE` and `README.md` name
Kurigram's `compiler/api/source` as the reference. Those two statements do not agree, and the repository does not show
which one the file was taken from. This is listed in `KNOWN_LIMITATIONS.md` as a release-review item. Nothing in this
directory was changed to resolve it.

## Generator

`tools/tl_codegen/tl_codegen.cpp`, built as the CMake target `cppgram_tl_codegen`. It writes:

- `include/cppgram/mtproto/schema/api.h`: the types and function declarations.
- `src/mtproto/schema/api_impl_0.cpp` to `api_impl_18.cpp`: the bodies, 175 per file. Counts from the generator are
  1773 types, 877 functions and 683 abstract types.

The split is 175 bodies per file. It was chosen because it lowers the largest single compile from about 4.05 GB to about
2.84 GB peak RSS (see `BUILD_RESULTS.md` section 3). Both CMake source lists must name every `api_impl` file:
`add_library(cppgram ...)` and the unity-build exclusion list. `tools/check_cmake_sources.py` checks that.

## Commands

    make tl-generate        # regenerate the checked-in files from the inputs
    make schema-check       # regenerate into a temp dir, compare, check the layer and the checksums

`tools/check_schema.sh` checks, in order:

1. `api.h` and every `api_impl_*.cpp` regenerate byte-for-byte.
2. The checked-in `api_impl` file set is exactly the set the generator writes (no stale or missing files).
3. The layer in `main_api.tl` equals `kLayer` in `src/client.cpp`.
4. `schema/SHA256SUMS.txt` matches the inputs, the generator source and the generated files.

## Checksums

`schema/SHA256SUMS.txt` lists 24 files: the three inputs, the generator source, `api.h`, and the 19 `api_impl` files.
After a reviewed change to any of them, run `bash tools/update_schema_checksums.sh`, then `make schema-check`.

## Schema-blocked API

A Kurigram method that is not in layer 233 cannot be sent, so it cannot be implemented here without a schema change.
The one such method among the 330 reference entries is `channels.UpdatePinnedForumTopic`. It is listed in
`PARITY_MATRIX.md` as SCHEMA_BLOCKED. This schema does not invent constructors for it.

## Changing the layer

See `HOW_TO_UPDATE_TELEGRAM_API.md`. A layer change is not a drop-in change. The wire format of changed constructors
must be checked against the new schema, and offline tests must cover the new constructor IDs.
