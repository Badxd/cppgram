# Known limitations (private release)

This is the authoritative limitations list for this release. `FINAL_REPORT.md` gives the status and the counts. Test
counts are in `TEST_RESULTS.md`, build facts are in `BUILD_RESULTS.md`, per-entry parity is in `PARITY_MATRIX.md`, and
the current status by area is in `IMPLEMENTATION_STATUS.md`.

What was run here: the offline test suites, on one x86_64 Linux machine, GCC 13.3 Release, against fake DCs and a local
loopback socket. What was not run: anything that talks to Telegram. Passing offline tests show what the client sends and
how it decodes replies from a scripted fake. They do not show Telegram's behaviour.

## Scope and status

1. Private only. Not approved for public release. No GitHub push, pull request, workflow, release or remote change was
   made. This directory is not a git repository, so there is no commit history to verify.
2. Kurigram parity is a source and evidence classification, not a behavioural audit. `PARITY_MATRIX.md` classifies all
   330 namespaced entries: 326 IMPLEMENTED (153 of them have a named test that passes offline, 173 are source only), 3
   IMPLEMENTED_WITH_LIMITATIONS, 0 PARTIAL, 0 NOT_IMPLEMENTED, 0 NOT_APPLICABLE and 1 SCHEMA_BLOCKED. No entry was
   re-audited against Kurigram's behaviour. A passing test is evidence only for what it asserts.
3. Three checklist rows are IMPLEMENTED_WITH_LIMITATIONS: `ephemeral.SendMessage`, `ephemeral.EditMessage` and
   `messages.GetInlineBotResults`. Their remaining limits are items 11 to 13 below.
4. `channels.UpdatePinnedForumTopic` is SCHEMA_BLOCKED. It is not in the layer 233 schema, and Kurigram's own generated TL
   lacks it too. The existing `Client::pin_forum_topic` uses `messages.updatePinnedForumTopic`.
5. Secret chats and calls are not implemented. Kurigram does not invoke them, so they are not among the 330 reference
   entries. They are outside this project's scope.
6. Fake-TLS (`ee`) MTProto proxy secrets are rejected with an error. Plain and padded secrets work.
7. No live Telegram verification was run. No credentials were used, and no login, OTP, phone number or session string was
   requested, created or processed. Every live item is NOT RUN (see `VPS_TESTING_GUIDE.md`, steps O to Q).
8. Sanitizers were not run. `tools/run_sanitizers.sh` builds the whole suite with ASan and UBSan at `-j1`. Its own header
   says that needs about 8 GB and about 1.5 hours. TSan was not run. Fuzzing, benchmarks and static analysis were not run.
9. Name matches between CppGram and Kurigram do not prove matching behaviour. Each function needs review and a test
   against a real or a faithful fake DC.
10. Some Bot API compatibility claims were read through a summariser and are unverified against the raw spec.

## Ephemeral, inline and keyboards

11. `ephemeral.sendMessage` and `ephemeral.editMessage` take media as a cached photo or document, as a `media_path`
    uploaded through `Client::save_file` with progress, or as an already uploaded file. Cancelling the upload of a
    `media_path` works only when the call runs under `Client::async`, because `EphemeralMessageOptions` has no stop
    token. Called directly, the upload cannot be cancelled. The offline test
    `EphemeralUploadFakeDc.CancelDuringUploadStopsBeforeSend` cancels a 10-part upload part way and checks that no send
    follows. Telegram's acceptance of an edit or a send is not checked by the client (see item 14).
12. Rich message bodies (`inputRichMessageHTML` and `inputRichMessageMarkdown`) take files as cached photos or documents,
    or as a local path. A path is uploaded through `Client::save_file` and then `messages.uploadMedia` with the bot's own
    peer, and the returned media is used. Two limits apply:
    - a rich-file upload reports no progress; only `media_path` uploads of ephemeral messages do;
    - a rich file that names both a path and cached media is refused before any request.
    Other media kinds are refused. The upload path is tested offline in `RichBlocksFakeDc.RichBodyLocalPhotoIsUploadedBeforeSend`.
13. `messages.getInlineBotResults` decodes text, web page, geo, venue, contact, invoice, widget and rich results. Rich result
    page blocks are typed (`RichBlockInfo`, with 34 kinds including `Unsupported`, in `src/rich_blocks.cpp`), and the full
    TL object is kept in `rich_raw`. The public fields for the four kinds that were previously thin are now:
    - Table: title, bordered, striped, compact, and each cell's plain text, header, alignment, colspan and rowspan. A span
      below 1 throws `SerializationError` (`RichBlocksFakeDc.TableCellSpansBelowOneAreRejected`).
    - RelatedArticles: url, webpage id, title, description, author, photo id and published date.
    - Map: caption, zoom, width, height, latitude and longitude.
    - Channel: id, title and username.
    The remaining limits are:
    - table cell text is plain text only, so cell formatting is dropped (it stays in `rich_raw`);
    - related-article photos are returned as ids, not as media objects;
    - ButtonRow blocks keep only their kind; the buttons are in `rich_raw`;
    - a block the decoder does not handle fails the whole result with `SerializationError`, and so does nesting deeper
      than 32 levels.
14. Invoice results (`InlineTextResultInfo::Kind::Invoice`) are sent as `inputBotInlineMessageMediaInvoice`:
    - The provider data is passed through unchanged. This client does not validate the JSON. An empty value is sent as
      `{}`.
    - The provider photo is not sent.
    - Title is 1 to 32 bytes, description 1 to 255 bytes and payload 1 to 128 bytes. These bounds are checked before the
      request, and a violation throws `std::invalid_argument`.
    - Nothing here contacts a payment provider or starts or completes a payment. Whether the bot's provider setup accepts
      the invoice is not verified.
15. Widget results send either HTML with a positive width and height, or a registered `widget_id` with only `chat_data`
    and a message. This client does not check Telegram's limits on HTML size or content.
    On the received side, `public_data` and `public_data_hash` share one flag bit in the layer 233 schema, so the wire
    carries both or neither. Build a `botInlineMessageWidget` with both set or both unset.
16. Reply keyboards on results decode into typed rows. Some requests cannot be represented in the public type and throw
    `errors::SerializationError`: a chat or channel request with admin rights, and a user request that asks for the name,
    username or photo. Unknown TL button variants also throw.
17. Web documents on results (thumb and content) decode the ImageSize, Video, Audio and Filename attributes. An unknown
    attribute is recorded by constructor id in `WebDocumentInfo::unknown_attribute_ids` and the result is kept. Nothing is
    dropped silently, and one unknown attribute does not fail the whole call. Decoding for those attributes is still
    limited to the four named above.
18. Empty rows in inline keyboards are accepted by the outbound validator. Telegram's handling is not verified.
19. The offline tests for items 11 to 18 are in `tests/mtproto/ephemeral_upload_fake_dc_test.cpp`,
    `inline_send_fake_dc_test.cpp`, `inline_decode_fake_dc_test.cpp`, `ephemeral_inline_fake_dc_test.cpp`,
    `rich_blocks_fake_dc_test.cpp` and `keyboard_roundtrip_test.cpp`. All pass offline. They check the request the client
    sends and the decoded fields. They do not show Telegram's behaviour. Two tests were added in this pass and passed
    when run: `EphemeralUploadFakeDc.CancelDuringUploadStopsBeforeSend` and `RichBlocksFakeDc.TableCellSpansBelowOneAreRejected`.
    The map-block test (`RichBlocksFakeDc.TableRelatedMapAndChannelDecodeIntoPublicFields`) failed earlier because its
    fixture left the map caption unset. That was a fixture error, and the decoder was not changed.

## Authorization and 2FA

20. 2FA password recovery (`recover_password`) now works from the `PasswordRequired` state. Before this release that
    combination threw `logic_error` after Telegram had already signed the client in. It is covered by
    `RecoveryCompletesLoginFromPasswordRequired` (`auth_state_test.cpp`) and by two fake-DC tests in
    `password_recovery_fake_dc_test.cpp`. Neither has run against Telegram.
21. `recover_password` with a new password depends on `account.getPassword` in the login state. Not verified live.
22. QR login and authorization import are tested offline only. `import_authorization` must run on a client connected to
    the target DC. None of this has run against Telegram.
23. The layer 233 `user` change adds one flag. It was checked by reading the generated struct diff, not against a live
    account.

## Other feature areas (carried forward)

24. Story media areas: only url, weather, star gift and location are built. Venue, channel post, suggested reaction and
    input venue are not.
25. Story video media uses a MIME type from the file extension (mov, webm, otherwise mp4). Other containers are not
    checked.
26. CDN reupload is retried at most 3 times per chunk. The recovery and give-up paths have offline tests. It has not run
    against a real CDN. The give-up test asserts a range (at most 3 reuploads, at least 1), not an exact count.
27. Folders: `create_folder` and `edit_folder` refuse shared (chatlist) folders. Shared folders change only through the
    chatlist invite and update calls. Those rules have offline tests in `folders_audit_fake_dc_test.cpp`, not run live.
28. `channels.getAdminLog`: `action` is the generated TL variant, not one struct per action. Edit and delete messages are
    decoded to `Message`. Other fields need the caller to match on the variant.
29. Story media upload uses `save_file`. The offline test covers the small single-part path only, not the big-file path.
30. Listener exceptions in callback dispatch propagate out of `EventHandler::handleUpdate`, and later listeners for that
    update are skipped. A test pins this. The owner should confirm this is the intended behaviour.

## DC migration

31. PHONE_MIGRATE, NETWORK_MIGRATE and USER_MIGRATE handling (`src/dc.cpp`) has no dedicated offline test. The QR-login
    fake-DC tests touch the migrate path only indirectly. Treat DC migration as untested.

## Network, MTProto and FLOOD_WAIT

32. This release did not audit the network or MTProto hardening. Offline tests cover retry, transport and connection
    behaviour (`retry_test.cpp`, `transport_test.cpp`, `connection_net_test.cpp`, `client_fake_dc_test.cpp`). They pass
    offline. None of them has run against Telegram.
33. FLOOD_WAIT behaviour is covered only by offline fake-DC and unit tests. Nothing was run against Telegram's rate
    limits, and no FLOOD_WAIT was triggered on purpose.

## Security (targeted review only)

34. The targeted review was a grep for direct stdout or stderr writes of secret-bearing values. It found no writes of API
    hashes, bot tokens, session strings, auth keys or passwords. The logger (`include/cppgram/log.h`) masks bot-token
    patterns and sensitive key/value pairs. `tests/security/log_test.cpp` covers that and passes offline.
35. Session encryption (`session_crypto.cpp`, scrypt-derived key), typed errors and `initData` validation (`webapp.cpp`)
    were not changed in this release. A targeted read of `webapp.cpp` confirmed HMAC-SHA256 with the key derived from
    `"WebAppData"` and the bot token, duplicate-field rejection, a constant-time comparison, and the time window checked
    after the signature. That read is not a cryptographic audit.
36. Not covered: fuzzing, a TLS or DH parameter review, file-permission checks on session files, and a threat model.
    `guides/THREAT_MODEL.md` is an earlier document and was not re-verified for this release.
37. The new ephemeral, inline and keyboard paths validate input before any RPC and throw `std::invalid_argument` or
    `errors::SerializationError`. They do not log their inputs.
38. A source scan for bot tokens, session-string shapes, private keys and non-placeholder API hashes found none. The only
    API hash literals are the labelled dummy value `0123456789abcdef0123456789abcdef` in test code.

## Platform, build and memory

39. The release was built and tested with GCC 13.3.0 (Ubuntu 24.04) in Release. The changed files were also
    syntax-checked with Clang 18.1.3. Clang was not used for a build or a test run in this release.
40. ARM64 was not built or tested. Ubuntu 24.04 is the only platform tested.
41. There is no measurement of memory on a 1 GB VPS. The largest single compile is now `api_impl_17.cpp` at about 2.8 GB
    (it was `api_impl_8.cpp` at 4.05 GB before the split). A 1 GB machine would need several GB of swap to compile it, and
    that was not tried. `BUILD_RESULTS.md` section 6 has the numbers and recommendations.
42. The generated API file split was changed from 350 to 175 bodies per file in this pass, which changes the file list.
    `tools/check_schema.sh` passes on the new files. A `-O1` override for the generated files was measured and not
    adopted, because it saved only 4.5% on the largest file.
43. A header-to-source check was made for `include/cppgram/client.h` only. A regex pass found 455 declared method names.
    437 have definitions in `src/`. The five `invoke_*` wrappers are templates defined in the header. The other 13 names
    were extraction noise from inline code (for example `call`, `lock`, `move`). Every other public header was checked only
    by the build, the install and the examples compile-check. A per-symbol review of all 55 headers was not done.
44. clang-format is not clean across the whole tree. Pre-existing files were not reformatted.
45. `examples/minimal_api` is built only with `-DBUILD_MINIMAL_EXAMPLE=ON`. It makes no network call. The 17 examples are
    compile-checked only, and none was linked or run.

## Repository

46. This directory is not a git repository. No commit hashes or push claims are made.
47. `docs/historical/` holds earlier status documents. Each carries a HISTORICAL banner and is superseded by this release's
    documents. Their numbers are not current.

## Pre-VPS release preparation (added in this pass)

48. **Schema provenance conflicts.** `schema/main_api.tl` has a header that cites tdesktop and TDLib. `NOTICE` and the
    README cite Kurigram (LGPL-3.0). The schema is therefore not yet cleared as MIT-compatible. This is a licence review
    item for the owner, not a resolved fact. See `schema/README.md`.
49. **NOTICE contains owner contact details and links.** They were left as they are. The owner should review them before
    any public release.
50. **The Bot API generator is not in this repository.** `src/ApiMethods.cpp` and `include/cppgram/bot/ApiMethods.inc.h`
    say they are generated by a make target that does not exist here. `api/codegen.yaml` and `api/telegram-bot-api.yaml`
    have no consumer in this tree. Bot API generation is therefore not reproducible from this release. The files were
    not hand-edited in this pass.
51. **clang-format allowlist.** Only 53 of 243 C++ and header files pass `clang-format-18` with the repository style.
    `tools/format_check.sh` checks the 53 files on the allowlist (`tools/format_allowlist.txt`). The other 190 files are
    not checked. No mass reformat was done, to keep the diff to unrelated files at zero.
52. **CI and release workflows have not run.** `.github/workflows/ci.yml`, `release.yml` and `live.yml` were written and
    checked for YAML syntax and least-privilege permissions. Nothing was executed on GitHub. `actionlint` and `shellcheck`
    were not available to lint them.
53. **ARM64 CI is gated.** The `build-arm64` job runs only when the repository variable `ENABLE_ARM64_CI` is `true`. It is
    off by default. No ARM64 result exists.
54. **Cross-compilation is not verified.** The aarch64 command in `docs/PORTABILITY.md` has not been run. No aarch64
    toolchain or qemu was available.
55. **Windows is not supported.** The `if(WIN32)` block in `CMakeLists.txt` is untested and is not in CI.
56. **Example programs are compile-checked only.** `tools/check_examples.sh` runs `-fsyntax-only` on 17 files. None is
    linked or run. The `examples/*/CMakeLists.txt` files are consumer templates and are not added by the top-level build.
57. **Live workflow is manual and never executed here.** `live.yml` runs only on `workflow_dispatch`, reports SKIPPED when
    the Telegram secrets are absent, and removes session files in an `always()` step. It uploads no artifacts. It has not
    been started.
58. **API index is a name-level check.** `docs/API_INDEX.md` lists 470 `cppgram::Client` member names. The checker
    confirms each name is defined somewhere under `src/` or inline in a public header. It does not check signatures,
    behaviour or tests. `docs/API.md` examples are guidance and are not compiled as a test.
59. **Version basis.** `VERSION` is `0.1.0`. The basis is in `CHANGELOG.md` and `RELEASE_NOTES.md`. No tag exists, and no
    release was published.
60. **Portability details** are in `docs/PORTABILITY.md`: `unsigned __int128` in the Diffie-Hellman code, POSIX-only
    headers, and a CMake Windows block that is not supported.
61. **Release zip reproducibility** depends on the packaging script's fixed timestamp and sorted entries. Its SHA-256 is
    reported outside the archive, because a zip cannot contain its own hash.
