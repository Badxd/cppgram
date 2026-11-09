# Implementation checklist (batch 4)

Status values: IMPLEMENTED (code written, compile-checked), TESTS WRITTEN (regression tests written, pass offline; live NOT RUN),
IMPLEMENTED_WITH_LIMITATIONS (implemented; the remaining limits are written in the row), PARTIAL (some parts missing; none at present),
NOT IMPLEMENTED (no code), BLOCKED (exact blocker given).
"Compile-checked" means `g++ -fsyntax-only` passed on the changed file. It does not mean the code runs correctly.

## Batch 4 (this session)

| Function | Status | Where | Notes |
|---|---|---|---|
| auth.resendCode | IMPLEMENTED, TESTS WRITTEN (offline tests pass; live NOT RUN) | src/login.cpp `resend_login_code` | Shares `sent_code_info` with send_login_code |
| auth.signUp | IMPLEMENTED (no offline test; live NOT RUN) | src/qr_login.cpp `sign_up` | No test yet. Signup needs a real phone code; owner live test |
| auth.requestPasswordRecovery | IMPLEMENTED, TESTS WRITTEN (offline tests pass; live NOT RUN) | src/qr_login.cpp | Returns the masked email pattern |
| auth.recoverPassword | IMPLEMENTED (code + optional new password; offline tests pass; live NOT RUN) | src/qr_login.cpp `recover_password`, src/password_settings.h | new_password sets new_settings via account.getPassword KDF params. Regression test tests/mtproto/password_recovery_fake_dc_test.cpp is NOT RUN |
| stories.getAllStories | IMPLEMENTED, TESTS WRITTEN (offline test passes for the not-modified path only; live NOT RUN) | src/stories_messages.cpp | Full stories path (with peers) is code only |
| stories.editStory | Client::edit_story (caption, media photo or video, media areas, privacy rules, music) | src/stories_messages.cpp `edit_story_caption` | IMPLEMENTED (offline tests pass; live NOT RUN) |

## Status summary (this pass)

Implemented in code; offline tests pass, live NOT RUN:
- CDN reupload (upload.reuploadCdnFile): download_via_cdn retries the chunk after cdnFileReuploadNeeded, bounded at 3 per chunk. Fake-DC tests cover the recovery and the give-up path.
- Stars, gifts, payment forms, Stars subscriptions and gift auctions: implemented (payments.* rows in Session 6).
- TON wallet (wallet.GetProofChallenge, GetUserAddresses, ReplaceWallet): implemented. TON signing for imported wallets is outside the library.
- Business, chatlists and folder functions: audited in Session 7. The gaps the audit found are fixed (see Session 7 rows).
- Story media upload (photo and video) through edit_story: fake-DC tests cover the upload and the inputMedia variant.

Not implemented or blocked:
- channels.UpdatePinnedForumTopic: not in layer 233 (documented mismatch; see the rows marked BLOCKED or NOT IMPLEMENTED).
- Secret chats and calls: Kurigram does not invoke them, so they are not parity items.
- Async, memory and VPS items: not part of this batch.
- IMPLEMENTED_WITH_LIMITATIONS rows: ephemeral.EditMessage, ephemeral.SendMessage, messages.GetInlineBotResults. Their remaining limits are written in the status cell of each row (the rows are the source of truth). No row is PARTIAL at present.

## Not run in this session (owner runs these)

- Sanitizers, fuzzing, benchmarks, live Telegram, a CTest run, example link. The offline binaries were run directly, not through CTest.
- Live runs of the tests in tests/mtproto/auth_stories_batch4_test.cpp are NOT RUN. Their offline runs pass.

## Session 6: remaining Kurigram-invoked functions (status per function)

Status key: IMPLEMENTED means the method exists and compiles. PARTIAL means the call is sent and the main fields are mapped, but part of the reply or input is not wrapped (listed in the row). NOT IMPLEMENTED is listed with the reason. The offline suites were built and run (GCC 13.3, Release); the counts are in TEST_RESULTS.md. Nothing here was run against Telegram; live checks are NOT RUN. The per-entry classification is in PARITY_MATRIX.md, which is a source and evidence classification and not a behavioral audit.

| Kurigram function | Client method / location | Source | Test | Status |
|---|---|---|---|---|
| GetFutureSalts | handshake/keepalive (snake-case future_salts in session code) | src/client.cpp, src/mtproto/session* | covered by existing session tests | IMPLEMENTED (internal) |
| InvokeWithBusinessConnection | template Client::invoke_with_business_connection | include/cppgram/client.h (template) | remaining_methods test not applicable (template wrapper) | IMPLEMENTED (template, no offline test; live NOT RUN) |
| InvokeWithMessagesRange | template Client::invoke_with_messages_range | include/cppgram/client.h (template) | none (template) | IMPLEMENTED (template, no offline test; live NOT RUN) |
| InvokeWithReCaptcha | template Client::invoke_with_recaptcha | include/cppgram/client.h (template) | none (template) | IMPLEMENTED (template, no offline test; live NOT RUN) |
| InvokeWithTakeout | template Client::invoke_with_takeout | include/cppgram/client.h (template) | none (template) | IMPLEMENTED (template, no offline test; live NOT RUN) |
| InvokeWithoutUpdates | template Client::invoke_without_updates | include/cppgram/client.h (template) | none (template) | IMPLEMENTED (template, no offline test; live NOT RUN) |
| PingDelayDisconnect | keepalive (snake-case ping_delay_disconnect in session code) | src/mtproto | existing | IMPLEMENTED (internal) |
| ReqDHParams | handshake req_DH_params | src/mtproto/auth.cpp | existing handshake tests | IMPLEMENTED (internal) |
| ReqPqMulti | handshake req_pq_multi | src/mtproto/auth.cpp | existing handshake tests | IMPLEMENTED (internal) |
| SetClientDHParams | handshake set_client_DH_params | src/mtproto/auth.cpp | existing handshake tests | IMPLEMENTED (internal) |
| account.ChangePhone | Client::change_phone | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.FinishTakeoutSession | Client::finish_takeout_session | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.GetBotBusinessConnection | Client::get_bot_business_connection | src/business_chatlists.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.GetTmpPassword | Client::get_tmp_password | src/session.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.InitTakeoutSession | Client::init_takeout_session | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.SaveMusic | Client::save_music | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.SendChangePhoneCode | Client::send_change_phone_code | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.SendVerifyEmailCode | Client::send_verify_email_code | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.SendVerifyPhoneCode | Client::send_verify_phone_code | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.SetAuthorizationTTL | Client::set_authorization_ttl | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.SetMainProfileTab | Client::set_main_profile_tab | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.UpdateBirthday | Client::update_birthday | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.UpdateColor | Client::update_color | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.UpdateEmojiStatus | Client::update_emoji_status | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.UpdateNotifySettings | Client::update_notify_settings | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.UpdatePersonalChannel | Client::update_personal_channel | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| account.VerifyEmail | Client::verify_email | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| auth.CheckPaidAuth | Client::check_paid_auth | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| auth.ResetAuthorizations | Client::reset_authorizations | src/account_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.CheckUsername | Client::check_bot_username | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.CreateBot | Client::create_bot | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.EditAccessSettings | Client::edit_bot_access_settings | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.ExportBotToken | Client::export_bot_token | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.GetAccessSettings | Client::get_bot_access_settings | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.GetAdminedBots | Client::get_admined_bots | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.GetBotMenuButton | Client::get_bot_menu_button | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| bots.SetJoinChatResults | Client::set_join_chat_results | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.CheckUsername | Client::check_channel_username | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.DeleteChannel | Client::delete_channel | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.DeleteHistory | Client::delete_channel_history | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.DeleteParticipantHistory | Client::delete_participant_history | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.GetAdminLog | Client::get_admin_log (action decoded to the TL variant; edit and delete messages decoded to Message) | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.GetAdminedPublicChannels | Client::get_admined_public_channels | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.GetChannelRecommendations | Client::get_channel_recommendations | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.GetChannels | Client::get_channels | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.GetGroupsForDiscussion | Client::get_groups_for_discussion | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.GetSendAs | Client::get_send_as | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.InviteToChannel | Client::invite_to_channel (returns missing invitee count) | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.SearchPosts | Client::search_posts | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.SetMainProfileTab | Client::set_channel_main_profile_tab | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.UpdateEmojiStatus | Client::update_channel_emoji_status | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.UpdatePaidMessagesPrice | Client::update_paid_messages_price | src/bots_channels.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.UpdatePinnedForumTopic | NOT IN LAYER 233 SCHEMA. Kurigram's own TL lacks it too. Existing Client::pin_forum_topic (forum.cpp) uses messages.updatePinnedForumTopic | src/forum.cpp | none (existing) | NOT IMPLEMENTED: method absent from schema (documented mismatch) |
| chatlists.LeaveChatlist | Client::leave_chatlist | src/business_chatlists.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| contacts.GetTopPeers | Client::get_top_peers (every category with count and per-peer rating) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| contacts.ResolvePhone | Client::resolve_phone | src/account_reads.cpp | account_reads_fake_dc_test / remaining test | IMPLEMENTED (offline tests pass; live NOT RUN) |
| contacts.Search | Client::search_contacts | src/account_reads.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| ephemeral.DeleteMessage | Client::ephemeral_delete_message | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| ephemeral.EditMessage | Client::ephemeral_edit_message (text or media-only; media as a cached photo or document, or a local file path uploaded through save_file with progress; inline or reply markup; rich HTML/Markdown body; entities; invert media; welcome; peer). Send-only options rejected before the RPC | src/messages_more.cpp, src/markup.cpp, src/markup_detail.h | ephemeral_inline_fake_dc_test.cpp, ephemeral_upload_fake_dc_test.cpp, remaining_methods_fake_dc_test.cpp | IMPLEMENTED_WITH_LIMITATIONS: (1) A rich body file given by path is uploaded with save_file and messages.uploadMedia; that upload reports no progress. A file given as cached media must be a photo or a document. (2) Cancelling a path upload works only when the call runs under Client::async, because the options carry no stop token. (3) Which edits Telegram accepts is decided by Telegram; the client checks only its own input rules, before any RPC. Offline fake-DC tests pass; live NOT RUN. |
| ephemeral.SendMessage | Client::ephemeral_send_message (text, cached photo or document media, local file path uploaded through save_file with progress, inline or reply markup, rich HTML/Markdown body, entities, reply_to, noforwards, invert media, welcome, anchor, peer). Invalid combinations rejected before any RPC, and no upload starts for them | src/messages_more.cpp, src/markup.cpp, src/markup_detail.h | ephemeral_inline_fake_dc_test.cpp, ephemeral_upload_fake_dc_test.cpp, remaining_methods_fake_dc_test.cpp | IMPLEMENTED_WITH_LIMITATIONS: (1) A rich body file given by path is uploaded with save_file and messages.uploadMedia; that upload reports no progress. A file given as cached media must be a photo or a document. (2) Cancelling a path upload works only when the call runs under Client::async, because the options carry no stop token. (3) Telegram's own sending rules (permissions, limits, flood control) are not checked by the client. Offline fake-DC tests pass; live NOT RUN. |
| help.GetAppConfig | Client::get_app_config | src/account_reads.cpp | account_reads_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.AddChatUser | Client::add_chat_user | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.AddPollAnswer | Client::add_poll_answer | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.ComposeMessageWithAI | Client::compose_message_with_ai (named, id, slug and single-use tones; input and output entities; diff text) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.DeleteParticipantReaction | Client::delete_participant_reaction | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.DeleteParticipantReactions | Client::delete_participant_reactions | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.DeletePollAnswer | Client::delete_poll_answer | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.DeleteSavedHistory | Client::delete_saved_history | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.EditChatAbout | Client::edit_chat_about | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.EditChatCreator | Client::edit_chat_creator (2FA SRP) | src/session.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.EditChatParticipantRank | Client::edit_chat_participant_rank | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetChats | Client::get_chats (chat details) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetDiscussionMessage | Client::get_discussion_message | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetInlineBotResults | Client::get_inline_bot_results. Decodes text, web page, geo, venue, contact, invoice, widget and rich results, with entities, inline keyboards (callback, url, switch inline, copy) and reply keyboards (typed rows), thumb and content web documents with their attributes, and invoice photo | src/messages_more.cpp, src/markup.cpp | ephemeral_inline_fake_dc_test.cpp, inline_decode_fake_dc_test.cpp, remaining_methods_fake_dc_test.cpp | IMPLEMENTED_WITH_LIMITATIONS: rich result blocks are decoded into RichBlockInfo with public fields for Table (title, bordered, striped, compact; each cell's plain text, header, alignment, colspan and rowspan), RelatedArticles (url, webpage id, title, description, author, photo id, date), Map (caption, zoom, width, height, latitude, longitude) and Channel (id, title, username). Remaining limits: table cell text is plain text only, so cell formatting is dropped (it remains in raw TL); related-article photos are returned as ids, not media; ButtonRow blocks keep only their kind (buttons are in raw TL); a block the decoder does not handle fails the whole result with SerializationError. An unknown web document attribute is recorded in unknown_attribute_ids and the result is kept. Offline fake-DC tests pass; live NOT RUN. |
| messages.GetMessagesViews | Client::get_messages_views | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetMyStickers | Client::get_my_stickers (sets with covers, documents, packs, keywords) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetPeerDialogs | Client::get_peer_dialogs (dialogs, messages, update state; peer, folder and community inputs) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetPeerSettings | Client::get_peer_settings (all peerSettings flags and optional fields) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetPersonalChannelHistory | Client::get_personal_channel_history | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetPinnedDialogs | Client::get_pinned_dialogs (dialog details with top message) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetRecentStickers | Client::get_recent_stickers (stickers with emoji, packs, last-used dates) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetSavedDialogs | Client::get_saved_dialogs (messages + count) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetSavedDialogsByID | Client::get_saved_dialogs_by_id | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetSavedHistory | Client::get_saved_history | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetSearchCounters | Client::get_search_counters (caller passes MessagesFilter values) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetSplitRanges | Client::get_split_ranges | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.InstallStickerSet | Client::install_sticker_set | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.ReadMentions | Client::read_mentions | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.ReadReactions | Client::read_reactions | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.ReorderStickerSets | Client::reorder_sticker_sets | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.RequestAppWebView | Client::request_app_web_view | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.RequestMainWebView | Client::request_main_web_view | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.RequestSimpleWebView | Client::request_simple_web_view | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.RequestWebView | Client::request_web_view | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SaveDefaultSendAs | Client::save_default_send_as | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SaveGif | Client::save_gif | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SearchStickerSets | Client::search_sticker_sets (sets with covers) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SearchStickers | Client::search_stickers (stickers with emoji, next offset) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SendInlineBotResult | Client::send_inline_bot_result (reply_to, send_as, quick reply by id or name, paid stars, schedule, silent, background, clear draft, hide via) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SendPaidReaction | Client::send_paid_reaction | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SendScreenshotNotification | Client::send_screenshot_notification | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SendWebViewResultMessage | Client::send_web_view_result_message for every InlineTextResultInfo kind: text, web page, geo, venue, contact, game, photo, document, rich, invoice, widget (html or registered widget_id). Inline and typed reply keyboards. Returns the inline message ref. Validation runs before the RPC | src/messages_more.cpp, src/markup.cpp | inline_send_fake_dc_test.cpp, remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN). Invoice sending does not contact a payment provider or complete a payment. Rich body files must be cached. |
| Invoice inline result (inputBotInlineMessageMediaInvoice) | InlineTextResultInfo::Kind::Invoice through send_web_view_result_message | src/messages_more.cpp | inline_send_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN). Provider data is passed as given (empty sends "{}"). Provider photo not sent. Fields validated before the RPC. No payment is started. |
| Widget inline result (inputBotInlineMessageWidget / inputBotInlineMessageWidgetId) | InlineTextResultInfo::Kind::Widget through send_web_view_result_message | src/messages_more.cpp | inline_send_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN). html needs a positive width and height. widget_id takes only chat_data and the message. |
| Typed reply keyboards and inline button types (send and decode) | Client send paths and detail converters in src/markup.cpp | src/markup.cpp, src/markup_detail.h | keyboard_roundtrip_test.cpp, inline_send_fake_dc_test.cpp, inline_decode_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN). Every generated InlineButtonType and ButtonType variant round-trips or throws a typed error (SerializationError or invalid_argument). Unrepresentable chat admin rights throw. Ambiguous inputs throw. |
| messages.SetBotGuestChatResult | Client::set_bot_guest_chat_result (same result kinds; returns the inline message ref) | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SetBotShippingResults | Client::set_bot_shipping_results | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SetInlineGameScore | Client::set_inline_game_score | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.StartBot | Client::start_bot | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.SummarizeText | Client::summarize_text | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.ToggleNoForwards | Client::toggle_no_forwards | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.ToggleSuggestedPostApproval | Client::toggle_suggested_post_approval | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.UninstallStickerSet | Client::uninstall_sticker_set | src/messages_more.cpp | remaining_methods_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.BotCancelStarsSubscription | Client::bot_cancel_stars_subscription | src/payments_stars.cpp | payments_stars_fake_dc_test.cpp + remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.ChangeStarsSubscription | Client::change_stars_subscription | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.CheckGiftCode | Client::check_gift_code | src/payments_stars.cpp | payments_stars_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.CraftStarGift | Client::craft_star_gift | src/payments_stars.cpp | payments_stars_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.DeleteStarGift | Client::delete_star_gift | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.DeleteStarGiftCollection | Client::delete_star_gift_collection | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.ExportInvoice | Client::export_invoice | src/payments_stars.cpp | payments_stars_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.FulfillStarsSubscription | Client::fulfill_stars_subscription | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.GetCraftStarGifts | Client::get_craft_star_gifts (saved star gifts page) | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.GetPaymentForm | Client::get_payment_form (invoice, stars and star gift forms; methods, saved info, saved credentials; theme params) | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.GetResaleStarGifts | Client::get_resale_star_gifts (gifts, attributes, counters; attribute filter and hash) | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.GetStarGiftAuctionState | Client::get_star_gift_auction_state (active, finished and not-modified states; user state) | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.GetStarGiftUpgradeAttributes | Client::get_star_gift_upgrade_attributes (models, patterns, backdrops with rarity) | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.GetUniqueStarGiftValueInfo | Client::get_unique_star_gift_value_info | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.RefundStarsCharge | Client::refund_stars_charge | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.ReorderStarGiftCollections | Client::reorder_star_gift_collections | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.ResolveStarGiftOffer | Client::resolve_star_gift_offer | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.SendPaymentForm | Client::send_payment_form (saved, new, Apple Pay and Google Pay credentials; requested info and shipping option ids) | src/payments_stars.cpp | payments_stars_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.SendStarGiftOffer | Client::send_star_gift_offer | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.SendStarsForm | Client::send_stars_form | src/payments_stars.cpp | payments_stars_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.ToggleStarGiftsPinnedToTop | Client::toggle_star_gifts_pinned_to_top | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| payments.UpdateStarGiftPrice | Client::update_star_gift_price | src/payments_stars.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| phone.GetGroupParticipants | Client::get_group_call_participants (participants with peer, flags, source, volume, ratings) | src/messages_more.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| premium.GetMyBoosts | Client::get_my_boosts | src/account_reads.cpp | account_reads/remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| stickers.ChangeSticker | Client::change_sticker | src/messages_more.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| stickers.ReplaceSticker | Client::replace_sticker | src/messages_more.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| stickers.SetStickerSetThumb | Client::set_sticker_set_thumb | src/messages_more.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| stickers.SuggestShortName | Client::suggest_sticker_short_name | src/messages_more.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| upload.ReuploadCdnFile | CDN reupload retry in Client::download_via_cdn | src/client.cpp | none (needs second fake endpoint) | IMPLEMENTED internally (NOT TESTED) |
| users.GetSavedMusic | Client::get_saved_music | src/account_reads.cpp | account_reads_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| users.SuggestBirthday | Client::suggest_birthday | src/messages_more.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| wallet.GetProofChallenge | Client::get_wallet_proof_challenge | src/wallet_ton.cpp | wallet_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| wallet.GetUserAddresses | Client::get_wallet_user_addresses | src/wallet_ton.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| wallet.ReplaceWallet | Client::replace_wallet (imported wallets need an externally made signature) | src/session.cpp | wallet_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN); TON signing is outside the library |

## Session 7: folders and chatlists audit (offline tests pass; live NOT RUN)

Every row is a source change made in this pass. Tests are offline fake-DC tests; they pass. Live checks are NOT RUN.

| Kurigram function | Client method / location | Source | Test | Status |
|---|---|---|---|---|
| messages.GetDialogFilters | Client::get_folders (reads pinned peers, emoticon, color, title_noanimate, shared chatlist folders, self peer) | src/folders.cpp | folders_audit_fake_dc_test.cpp (request path only) | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.UpdateDialogFilter | Client::create_folder, edit_folder, delete_folder (shared folders refused before RPC; ids 2..255) | src/folders.cpp | folders_audit_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.UpdateDialogFiltersOrder | Client::reorder_folders | src/folders.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| messages.GetSuggestedDialogFilters | Client::get_suggested_folders (SuggestedFolderInfo with description) | src/folders.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| chatlists.CheckChatlistInvite | Client::check_folder_invite_link (filter id, missing and already-present peers, emoticon; slug strips query and fragment) | src/folder_invites.cpp | folders_audit_fake_dc_test.cpp | IMPLEMENTED (offline tests pass; live NOT RUN) |
| chatlists.JoinChatlistInvite | Client::join_folder_invite_link (returns bool; caches users and chats from the Updates) | src/folder_invites.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| chatlists.GetChatlistUpdates | Client::get_folder_updates | src/folder_invites.cpp | folders_audit_fake_dc_test.cpp (request path only) | IMPLEMENTED (offline tests pass; live NOT RUN) |
| chatlists.JoinChatlistUpdates | Client::join_folder_updates | src/folder_invites.cpp | remaining | IMPLEMENTED (offline tests pass; live NOT RUN) |
| chatlists.HideChatlistUpdates | Client::hide_folder_updates | src/folder_invites.cpp | folders_audit_fake_dc_test.cpp (request path only) | IMPLEMENTED (offline tests pass; live NOT RUN) |
| chatlists.GetLeaveChatlistSuggestions | Client::get_leave_folder_suggestions | src/folder_invites.cpp | folders_audit_fake_dc_test.cpp (request path only) | IMPLEMENTED (offline tests pass; live NOT RUN) |
| channels.UpdatePinnedForumTopic | not called: layer 233 has no channels.updatePinnedForumTopic constructor. Client::pin_forum_topic uses messages.updatePinnedForumTopic | src/forum.cpp | none | BLOCKED by layer 233 schema (no such constructor; Kurigram's own generated TL lacks it too) |
