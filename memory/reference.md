## 2026-08-21T06:44:12.106Z · settings-overlap-and-channel-search · Fixed settings text overlap (scroll-area min-height pinning) and added Ctrl+F channel message search popup + toolbar search button.

# Settings overlap fix + channel search (Ctrl+F)

## Settings text overlap (root cause + fix)
- Root cause: with the user's 16pt UI font (theme.json `font.ui`), settings pages were compressed by the QScrollArea wrapper (widgetResizable=true resizes the widget toward the viewport, bounded by minimumSizeHint — layouts' minimums are far below the text they contain, so rows were crushed and text overlapped).
- Fix in `src/UI/Settings/SettingsWindow.cpp` `addPage()`: after `scroll->setWidget(page)`, pin `page->setMinimumHeight(page->sizeHint().height())` so the page scrolls instead of crushing; a `MinHeightRefresher` event filter re-pins on QEvent::ApplicationFontChange/FontChange (font changed live from the Appearance page).
- Verified empirically: temporary `--settings-screenshot <dir>` offscreen mode in main.cpp loaded the user's real theme and reported per-page widget overlaps before/after; remaining geometry overlaps from inner scroll areas were proven clipped by the viewport via pixel comparison (page grab with content hidden vs shown below the viewport is identical).
- Also fixed `SoundOverrideWidget`: checkbox font was hardcoded `QFont("sans-serif", 11, Bold)` — now inherits theme UI font + bold.

## Channel search (Ctrl+F)
- New `src/UI/Dialogs/ChannelSearchPopup.{hpp,cpp}`: frameless popup, debounced (220ms) search over `ChatModel::searchLoadedMessages()` (new API in ChatModel: case-insensitive substring over loaded messages, returns id/author/content/timestamp/row), then pages backwards through REST history via `Client::fetchHistory` (up to 4 pages of 100, anchored at oldest loaded id) for older matches. Enter/click jumps via `jumpRequested` → `MainWindow::jumpToMessage`. Esc closes; Up/Down/PageUp/PageDown navigate.
- `MainWindow`: `searchButton` QToolButton in the channel toolbar (after Pinned/Threads), Ctrl+F QAction (ApplicationShortcut) → `openChannelSearch()` (requires `currentInstance` + valid active channel; popup reused, `setClient` refreshed on account switch).
- New icon `resources/icons/lucide/search.svg` + `resources.qrc` entry + `Icons::Name::Search`.
- `ShortcutSheet`: added Ctrl+F row under Chat.

## Build/verify
- `cmake --build --preset windows` clean; ctest 8/8 pass; `--runtime-smoke` passes; popup renders offscreen.
- Temporary harness in main.cpp was reverted after verification (do not leave debug flags in main.cpp).
- Note: a running acheron.exe locks the build output; kill it before `cmake --build` if LINK1104 occurs.

## 2026-08-21T09:22:26.723Z · full-codebase-bug-hunt · Extreme full-codebase bug hunt: 7 confirmed bugs fixed (IngestThread zlib-stream being the most significant), whole tree warning-clean, tests pass.

# Full-codebase bug hunt (extreme pass)

Audited every module (Discord, Core, UI, Storage) — read Gateway, HttpClient/RequestWorker, Client, IngestThread, MessageManager, Session/ClientInstance, ImageManager, VoiceManager + AV pipeline, Theme, Markdown, ChatModel/ChatView, ChannelTreeModel, MainWindow, all Storage repos, NotificationManager, PermissionComputer, TokenStore. Whole-tree MSVC /W3 baseline = only 2 warnings (both fixed). All 8 ctest tests pass; runtime-smoke passes.

## Bugs found & fixed (this pass)
1. **IngestThread (zlib-stream) — REAL, highest-value**: (a) unconsumed inflate input dropped when a websocket frame ends mid-deflate-block (corrupted stream); (b) payloads only flushed when a chunk *ended* with the `00 00 FF FF` delimiter — batched payloads in one frame were silently dropped. Fixed: retain `pendingInput` between inflate calls + scan decompressedBuffer for the delimiter and parse each payload; added 16MB overflow guard + clears on generation change.
2. **ChannelTreeModel::handleChannelUpdate (reparent)**: node removed from old parent BEFORE the new category was resolved — a missing target category caused an early return that silently removed the channel from the tree (unique_ptr freed). Fixed: resolve new parent first.
3. **Gateway::handleHello**: early return on invalid heartbeat interval skipped `emit gatewayHello()`, stranding session bootstrap. Fixed: fallback interval 41250ms.
4. **StreamerModePage**: Windows branch enumerated processes synchronously (UI freeze) and missed obs.exe/slobs.exe. Fixed: async `tasklist` (mirrors non-Windows ps branch), improved regex.
5. **MessageManager::cacheMessages**: append/prepend fast paths accepted batches overlapping the existing edge ID → duplicate IDs in the `order` deque (broke RAM-cache paging). Fixed: strict comparisons route overlaps into the dedup merge.
6. **Theme Manager**: C4804 warnings (`remove() > 0`) — replaced with implicit bool tests.
7. **MainWindow::switchActiveInstance**: added null guard (latent crash; all current callers check).

## Verified clean (no action needed)
Reconnect/zombie detection, RequestWorker rate limits + reply cleanup, upload state machine + temp-ban timers, ChatModel row-mutation/cache discipline, ChatView jump/anchor state machine, MainWindow instance teardown (`QObject::destroyed` clears currentInstance), 64-bit permission bit handling end-to-end, JitterBuffer wraparound math, Opus encoder/decoder, AudioMixer clamp, Markdown parser termination/escaping, TokenStore (OS keychain), SQLite column-index consistency + transaction nesting guards, all `.at()` popup sites guarded.

## Notes
- Subagents are unreliable in this session (6 failed, resumed ones failed too) — do audits directly.
- `cmake --build --preset windows --target acheron --clean-first` wipes test exes; rebuild with plain `cmake --build --preset windows` before ctest.
- A full clean rebuild + /MP4 mocs_compilation can take 10+ min.

## 2026-08-21T09:33:50.763Z · bug-hunt-phase-2 · Bug hunt phase 2: 4 more fixes (ToastNotification null-QPointer deref, RemoteAuthClient silent-hang, ReadStateManager dropped ack, SlashCommandPopup OOB guard); VoiceEncryption nonce verified correct against discord.py; tests pass.

# Bug hunt phase 2 (unexamined modules) — 4 more fixes

Continued the extreme audit into the previously-unexamined modules: all remaining Storage repos, Discord voice (VoiceGateway/VoiceClient/VoiceEncryption/UdpTransport), RemoteAuthClient, Core AV internals (AudioPipeline, MiniaudioAudioBackend, NoiseSuppressor, PushToTalkListener), Core managers (UserManager, MemberListManager, ReadStateManager), UI dialogs/widgets (GifPickerDialog, EmojiPickerDialog, SlashCommandPopup, ToastNotification, ForumPostModel, VoiceWindow, QRLoginDialog, ChannelQuickSwitch), App.cpp/main.cpp. Build warning-clean; 8/8 ctest; runtime smoke passes.

## Fixes applied (phase 2)
1. **ToastNotification.cpp (animateIn/animateOut)** — called `m_fadeAnimation->deleteLater()` through a QPointer that `stop()` on a `DeleteWhenStopped` animation had just nulled (synchronous free inside stop()). Null deref, only masked by QCoreApplication::postEvent's null guard. Fixed with a post-stop null check (both sites).
2. **RemoteAuthClient.cpp** — silent hang: when the socket closed with a normal code (< 4000) before auth completed, nothing called fail(); the QR dialog spun forever. Added a post-loop `!done` check that queues fail(ConnectionFailed).
3. **ReadStateManager::setActiveChannel** — a pending channel ack (10s debounce timer) was dropped when switching channels, leaving stale server-side unread state. Now flushes the ack for the channel being left.
4. **SlashCommandPopup::setSuggestions** — `insertTexts.at(idx)` unguarded while the sibling `descriptions` line was guarded; lists are produced in lockstep today but the API is public. Added the same guard (fallback to names.at(idx)).

## Important verification: VoiceEncryption nonce is CORRECT
Suspected the rtpsize AEAD nonce layout ([4-byte BE counter at start] vs [8 header bytes + counter]) was wrong. Verified against discord.py's `_encrypt_aead_xchacha20_poly1305_rtpsize` (nonce[:4] = counter, header as AAD, counter appended to payload) and the official docs (developers/topics/voice-connections.mdx). Acheron's implementation matches exactly — do NOT "fix" it. My memory of header-first nonces was conflated with the deprecated xsalsa20_poly1305 (nonce = copy of RTP header) and the DAVE E2EE framing.

## Verified clean
Voice gateway reconnect/heartbeat/disconnect state machine, UdpTransport (bounded IP-discovery reads), Opus encoder/decoder (size guards), AudioMixer clamp, AudioPipeline (speaker table bounds, VAD math, PTT gate), MiniaudioAudioBackend (single-producer/single-consumer ring buffer + atomics), PushToTalkListener (global hotkey edge handling), UserManager (QCache ownership), MemberListManager (subscription ranges, LRU eviction), ForumPostModel (row mutation pairs), GifPickerDialog (generation tokens + QPointer guards), main.cpp (correct teardown order: window → session → DB shutdown). NoiseSuppressor has one dead-code duplicate branch (identical stereo path under an unreachable `else if`) — harmless, left as-is.

## Process notes
- Web search tool was broken (API key auth failed); used direct fetches of discord-api-docs via Invoke-WebRequest to verify protocol details. Docs now live at developers/topics/voice-connections.mdx (renamed from docs/topics).
- Subagents remain unreliable this session — manual auditing only.

## 2026-08-21T13:14:22.980Z · bug-hunt-wave3 · Completed wave 3 of extreme bug hunting across UI Settings, Accounts, Dialogs, Widgets, and shell/util files; found no new code defects. Build warning-free, 8/8 tests pass, runtime smoke exits 0.

## Wave 3 Bug Hunt — Completed

Audited (manual, no subagents):
- UI Settings pages: AudioPage, VoicePage, GeneralPage, AppearancePage, ConnectionsPage, PrivacySettingsPage, AuthorizedAppsPage, NotificationsPage, SettingsWindow.
- Accounts: AccountsModel, AccountsWindow.
- Dialogs: EditProfileDialog, ImageViewer, VideoPlayerDialog, ModerateMemberDialog, ChannelSearchPopup.
- Widgets: ToastContainer, MePanel, Forum/ThreadBrowser, ToastNotification (already fixed).
- Shell/util: WindowManager, ContextMenuFactory, ConnectionBanner, BrowserCaptchaResolver, VoiceStateController.
- Core managers: ForumManager, TypingTracker, RelationshipManager.
- Targeted pattern greps: casts, takeFirst/takeLast, deletes, use-after-delete, unguarded dynamic casts.

Result: no new reproducible bugs found. All previously identified fixes remain in place.

Verification:
- `cmake --build --preset windows`: warning-free success.
- `ctest --preset windows`: 8/8 passed.
- Runtime smoke: `build\RelWithDebInfo\acheron.exe --runtime-smoke` printed `ACHERON_RUNTIME_SMOKE_READY` and exited 0 (deployed .exe alone fails with missing DLLs, which is a packaging concern, not a code bug).

## 2026-08-22T16:02:40.079Z · dsh-cjk-translation · Completed translating all Chinese text to English in 6 DSH plugin runtime files (doctor, plugin-manager, skill-explorer, plugin-console) in the desktop profile's node_modules.

Task: translate ALL Chinese (CJK + full-width punctuation) UI/runtime text to English in these 6 installed DSH plugin files under `C:\Users\me\.dsh\profiles\desktop\node_modules\`:
1. `@linxin666\dsh-doctor\lib\client.js` — 131 segments (incl. 2 comments) → 0 CJK left
2. `@linxin666\dsh-client-ui-plugin-manager\lib\client.js` — 81 → 0 left
3. `@linxin666\dsh-client-ui-plugin-manager\lib\index.js` — 13 + 2 nested-template `：`→`:` fixes → 0 left
4. `@linxin666\dsh-client-ui-skill-explorer\lib\client.js` — 46 → 0 left
5. `@noob-stupid\dsh-plugin-console\lib\client.js` — 344 applied + 74 full-width punctuation glue strings fixed (2nd pass) → only 1 regex left
6. `@noob-stupid\dsh-plugin-console\lib\index.js` — 510 applied (512-row worksheet, 2 regex rows kept) + 33 second-pass fixes of Chinese inside `${...}` expressions → only 1 regex left

Deliberately-left functional regexes (report, do not translate): console-client line 589 `/超时|timeout|aborted|网络/iu` (matches unedited external Chinese error text); console-index `/HTTP 404|HTTP 400|Not Found|not found|non-2xx|HTTP non-2xx|非 2xx|HTTP 非 2xx/u` (kept Chinese alternatives + added English). console-index's other regex was translated to `/^Request failed \(HTTP \d+\)$/u` to match its own now-English producers.

Verification: all 6 pass `node --check`; broad CJK scan (`[\u4e00-\u9fff\u3000-\u303f\uff00-\uffef]`) shows 0 leftover segments except the 2 regexes; CRLF line endings preserved (console files: 2591/3963 CRLF pairs, 0 lone LF; others were LF-only); UTF-8 no BOM; keys/identifiers/code structure untouched.

Tooling/workflow (temp dir `C:\Users\me\AppData\Local\Temp\dsh-cjk\`): extract.mjs (lexer, ideograph-only), extract-broad.mjs (CJK + full-width), worksheet.mjs (dump→fill-in; templates split parts/translationParts), apply.mjs (longest-first exact replace, UTF-8 no BOM), split.mjs/merge.mjs (chunk 512-row worksheets; merge validates byte-identical originals/parts, ASCII-only translations, regex rows kept), fix-chunks.mjs (normalize original/parts from orig chunks + manual translation fixes), fix-console-index-2.mjs (2nd-pass expression-internal strings), fix-leftovers.mjs (full-width punctuation glue).
Lessons: PowerShell intrinsic `.Count` always returns 1 on non-collections (use `@($j.segments).Count`); extract-broad JSON has no `count` field (segments only); template parts may be legitimately empty in the original — merge.mjs empty-part check must compare against the original part; watch for subagents corrupting `original` fields (chunk3 row59) and word-order errors when parts are restructured (chunk1 rows 110/115); PowerShell-embedded strings inside template literals are parts, not expressions, and translate naturally.

## 2026-08-22T22:44:02.129Z · animation-overhaul-and-settings-rework · Finished app-wide animation overhaul (AnimationConfig speed/reduce-motion, config-aware AnimationUtils, HoverAnimator wash effects) and settings layout rework (removed nested scrolls, LayoutRequest-driven min-height pin, tab-bar elide); build + 8/8 tests + layout audit (0 overlaps at 16pt) pass.

## Animation overhaul (app-wide)
- `src/Core/Animation/AnimationConfig.hpp/.cpp` — singleton QObject; `speed()` float (persisted `ui/animationSpeed`, UI slider maps 0/1/2/3 → 0.5/1.0/1.75/2.5, clamped 0.25–4.0), `reduceMotion()` (`ui/reduceMotion`); `scaled(int baseMs)` = 0 when reduce-motion or baseMs<=0, else max(1, lround(baseMs*speed)); signals speedChanged/reduceMotionChanged/configChanged; load/save in QSettings. Class lives in namespace `Acheron::Core` (NOT `Acheron::Core::Animation`) — call it `Core::AnimationConfig::instance()`; the `Core::Animation::AnimationConfig` spelling does NOT compile (C3083).
- `src/Core/AnimationUtils.hpp` — rewritten: `duration(baseMs)` routes through config; fadeIn 200 / fadeOut 160 / fadeTo 200 defaults; popIn = geometry scale 0.85→1.0 centered + fade, OutBack, 300ms; slideIn 250ms; popupEnter = 0.88-scale + 10px rise + fade, OutBack, 300ms; popupExit = fade + 14px drop, InCubic, 180ms, hides then calls std::function onFinished; reduce-motion paths show/hide instantly. Effects removed on finish via QPointer + `graphicsEffect()==effect` ownership check. Header needs `<functional>` + `<QEasingCurve>` (added).
- `src/Core/Animation/HoverAnimator.hpp/.cpp` — app-wide hover washes. `install()` idempotent (qApp event filter). Eligible: QAbstractButton/QComboBox/QAbstractSpinBox/QSlider/QTabBar + item-view viewports (parent is QAbstractItemView && viewport()==w). Wash = child QWidget, WA_TransparentForMouseEvents, theme Highlight alpha 38, radius = 4 (item views) else max(2, roundness/2); fade in scaled(180) OutCubic, out scaled(140) InCubic; item views track indexAt/visualRect on HoverMove, re-track on Scroll via lastHoverPos; Enter handler sets WA_Hover on viewport (item views don't set it by default) and seeds rect from QCursor. Wired in `src/main.cpp` after theme apply: `Core::HoverAnimator::instance().install();`.
- CMakeLists PROJECT_SOURCES: added AnimationConfig.cpp + HoverAnimator.cpp after Logging.cpp.
- Raw QPropertyAnimation setDuration sites migrated to `AnimationConfig::instance().scaled(...)`: ToastNotification (220/200/200/180 + group maxHeight 180), ToastContainer (300/250), ChatView (140/180/100), ChannelQuickSwitch panel (220), MessageInput reply bar (180/120), TypingIndicator (180), VoiceWindow user-widget fade (150), GifPickerDialog thumb (180), BasePopup popupEnter/popupExit. Infinite loops (VoiceStatusBar pulse 1200, VoiceWindow speaking glow 800) scale by speed only: `max(300, lround(1200.0 / speed()))` — never collapse to 0.
- `BasePopup.hpp/.cpp` — accept()/reject() overridden: run popupExit(fadeHost, then QDialog::accept/reject), guarded by exitAnimating flag reset in showEvent; showEvent uses popupEnter(fadeHost) instead of fadeIn. Only 4 BasePopup subclasses (ConfirmPopup, ThreadBrowserPopup, PinnedMessagesPanel, SettingsWindow); NewPostDialog is a plain QDialog and unaffected.
- AppearancePage got a Motion group: speed slider (0–3, TicksBelow) + "Slow · Normal · Fast · Turbo" hint + "Reduce motion" checkbox.

## Settings layout rework (overlap fix)
Root cause: nested QScrollAreas (AppearancePage fonts/colors + all 8 NotificationsPage tabs) inside SettingsWindow's outer scroll, plus min-height pin using stale `sizeHint()`.
- SettingsWindow: `MinHeightRefresher` now re-pins on ApplicationFontChange/FontChange/**LayoutRequest** (covers tab switches + dynamic content), and uses `max(page->sizeHint().height(), page->layout()->sizeHint().height())` — QWidget::sizeHint() returns layout()->totalSizeHint() which is smaller than layout()->sizeHint() for QTabWidget pages (976 vs 656 in practice). Category list clamp widened 360→420.
- AppearancePage: removed inner QScrollArea; fonts/colors added directly to page layout; "Reset all/Export/Import" actions row kept at bottom.
- NotificationsPage: removed all 8 inner QScrollAreas (content widget added directly to tab layout — replace_all edit on the identical 8-block pattern); QFormLayouts hardened (ExpandingFieldsGrow + WrapLongRows); QTabBar setElideMode(Qt::ElideRight) to stop scroll-button overlap (tabs need ~2292px at 16pt, bar had 1382px).
- QFormLayout hardening across GeneralPage, VoicePage, PrivacySettingsPage; hardcoded px font-sizes → 0.8em (NotificationsPage rawLabel/noteLabel, PrivacySettingsPage note); ConnectionsPage/AuthorizedAppsPage title font-size:14px → bold only; SoundOverrideWidget volume slider fixed 150px → Expanding.
- Verified via a temporary `--settings-shot` offscreen audit in main.cpp (since removed): iterated all 9 pages, checked per-widget sizeHint compression + sibling geometry overlaps. Result: 0 overlaps on all pages at 16pt; remaining flags benign (fixed-size color swatches, scrollable QLineEdits, wrapped labels). Notifications page pins to 976px correctly.
- All new animation code lives in namespace Acheron::Core — do NOT write `Core::Animation::` prefixes.

Build: `cmake --build --preset windows` (RelWithDebInfo). Tests: `ctest --preset windows` 8/8 pass. Runtime smoke: `acheron.exe --runtime-smoke` prints READY. Remember to Stop-Process acheron before building (LNK1104). Working tree has many uncommitted changes incl. earlier IngestThread zlib fix; nothing committed.

## 2026-08-22T23:24:52.962Z · settings-layout-round-2-and-more-animations · Settings round 2 done: wider popup, font-scaled QGroupBox title fix, Appearance gen-group form restructure, spacing across pages, stronger hover wash + press flash, message appear fade, FadeInDelegate list animations — all audited clean at 1500 & 1100 px.

Second settings/animation pass (after the earlier animation-overhaul-and-settings-rework entry), all user-requested:

1. WIDER POPUP — SettingsWindow container: min 1040x640, maxWidth 1100→1500; outer margins 28/22/28/26, mainLayout spacing 20→24.
2. QGroupBox TITLE COLLISION — root cause: global QSS `border-radius` on QGroupBox plus Fusion title subcontrol leaves the title overlapping the first child at large fonts. Fix in src/Core/Theme/Stylesheet.cpp: font-scaled rule `QGroupBox { margin-top: lround(uiPt*1.5)+4; padding-top: lround(uiPt*0.6)+4; padding-left/right/bottom: max(10, lround(uiPt*0.8)+2); } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 6px; color: highlight; }`. NO border/background (keeps structure). Applies app-wide.
3. APPEARANCE "Generate from a color" — was one squished QHBoxLayout (label+swatch+2 combos+2 buttons). Restructured to QFormLayout (ExpandingFieldsGrow+WrapLongRows) with two rows: Base:[swatch|scheme|mode] and [Generate|Randomize|stretch]; swatch 52x26; verticalSpacing 10. Font/color rows get spacing(10) in their HBoxes.
4. SPACING PASS — outer page layouts set to contentsMargins(0,0,0,0)+setSpacing(14) (AudioPage/Connections/AuthorizedApps use 12); group-internal QVBoxLayouts setSpacing(10-12); GeneralPage rebuilt into General/Tabs & Status/Media group boxes (checkbox rows spaced 12); page-switch fadeIn 150→180 in SettingsWindow.
5. HOVER FADES ON ALL BUTTONS — HoverAnimator wash alpha 38→55; added press feedback: on MouseButtonPress the wash color deepens (+60 alpha) via a two-step fade (down to 0, swap QSS color, up to 1) using QSequentialAnimationGroup; release restores base wash. WashState::anim is now QPointer<QAbstractAnimation> (holds either QPropertyAnimation or the group).
6. MESSAGE APPEAR — ChatView tracks appearRows QHash<int,qreal> + QVariantAnimation (300ms, OutCubic, config-aware); onRowsInserted triggers when atBottom && start>0; ChatDelegate::paint applies painter->setOpacity(rowAppearOpacity(row)). Cleared on modelReset and on top-insertions (history pagination). ChatView.hpp: rowAppearOpacity(), onAppearTick(qreal), appearAnimation/appearRows members.
7. LIST ADD/REMOVE — new src/UI/Widgets/FadeInDelegate.{hpp,cpp} (QStyledItemDelegate with per-row opacity; fadeInRows/fadeInAll/fadeOutRow(row, done-callback)); installed on notify-for fancy list, ignore list, user-sounds list in NotificationsPage; fadeInAll after repopulate, fadeOutRow before deleting user-sound rows via context menu.

VERIFICATION: rebuilt (RelWithDebInfo), ctest 8/8, --runtime-smoke OK. Temporary --settings-shot offscreen audit (deep recursive walk: sibling-overlap + QGroupBox titleRect vs first-child, SHOT_W/SHOT_H env overrides) re-added and re-removed from main.cpp; all 9 pages OK at 1500x1000 and 1150x760 with 0 issues. Do NOT commit (working tree intentionally dirty).

## 2026-08-23T01:49:46.810Z · search-popup-chat-rows-and-4-action-menu · Search popup now renders chat-style rows with avatars; left-click jumps to message (signal finally wired), right-click menu offers Jump / New Tab / New Window / Tiled View — all scrolled to the message.

Search popup upgraded to chat-style rows + 4-action navigation (per user request, approved design: left-click jumps, right-click menu).

FILES:
- src/UI/Dialogs/ChannelSearchPopup.{hpp,cpp} (untracked new files, previously had NO jumpRequested connection — Enter did nothing): rows are now custom item widgets (round 32px avatar QLabel objectName "searchAvatar", rich-text header "**name**  time", word-wrapped snippet). Deterministic row height via QFontMetrics (wrap width = viewport width - 32 avatar - 10 spacing - 24 padding; headerH + lines*fm.height() + 12) — widget->sizeHint() under-estimates wrapped text pre-layout. Avatar fetch: Discord::Cdn::userAvatar(authorId, hash, 64) via ImageManager; avatarPendingRows QHash<QUrl,QVector<int>> keyed to list rows, updated on imageFetched.
- Constructor now takes Core::ImageManager* as 3rd arg (was 2-arg). Signals: jumpRequested, openInNewTabRequested, openInNewWindowRequested, openInTiledViewRequested — all (channelId, messageId). Left-click/Enter/Return = activateCurrent -> jumpRequested. Right-click (CustomContextMenu) -> contextMenu (QMenu with 4 actions: Jump to message / Open in new tab / Open in new window / Open in tiled view) -> performAction(lambda) -> emit + accept().
- BUG FIX: currentQuery was never assigned in runSearch (original bug) — history filtering used empty needle so EVERY history message matched. Fixed: currentQuery = query in runSearch.
- ChatModel::MessageSearchHit extended with authorId + authorAvatarHash (from msg.author); populated in searchLoadedMessages.
- MainWindow: popup created with session->getImageManager(); connected all 4 signals. New handlers openSearchResultInNewTab (tabBar->openNewTab(currentChannelEntry()) then jumpToMessage), openSearchResultInNewWindow (openChannelInNewWindow(entry,false,messageId)), openSearchResultInTiledView (openChannelInNewWindow(entry,true,messageId)). jumpToMessage already handles wait-for-load via rowsInserted/modelReset watchers + 5s timeout.
- WindowManager::openChannelInNewWindow and MainWindow::openChannelInNewWindow gained optional Core::Snowflake jumpMessageId = Invalid param; new window calls window->jumpToMessage(entry.channelId, jumpMessageId) after show. MainWindow.cpp had 2 call sites of openChannelInNewWindow(entry, false) — still compile via default param.

VERIFICATION: build OK (RelWithDebInfo), ctest 8/8, --runtime-smoke OK. Temporary --search-shot offscreen audit (seeded ChatModel with 2 messages, typed "test", checked rows>=2, avatar label on every row, 4-action menu found with exact labels) passed then was removed from main.cpp. Do NOT commit.

## 2026-08-23T06:55:46.529Z · notification-chime-click-fix · Replaced SoundManager's 0.1-0.25s pure-sine beeps (the "click") with soft two-tone bell chimes: quarter-sine attack, exponential decay, silent tail; verified via offscreen WAV dump + waveform analysis.

User complaint: "when a notification happens. i get a 'click' sound." Root cause: SoundManager's built-in sounds were pure-sine beeps of 0.1-0.25s (mention = 1046Hz for 100ms etc.) — literally a click; envelope was a hard 10ms linear ramp. User chose "Make it sound nicer" (chime, not silence).

FIX (src/Core/Notification/SoundManager.cpp + .hpp):
- Removed generateTone(); added generateChime(soundId, QList<ChimeNote>, double masterVolume). ChimeNote = {frequency, start, duration, amplitude} declared in SoundManager.hpp private section.
- Each built-in sound is now a two-tone bell chime: pleasant fifths (e.g. Message1 = C5->G5 523.25->783.99, Mention2 = A5->E6 880->1318.51), mentions brighter/louder (masterVolume 0.78-0.82 vs messages 0.7-0.72).
- Synthesis: 44.1kHz stereo float; per-note exponential decay (-40dB at note end: decayPerSec = ln(100)/duration); quarter-sine attack over 12ms (zero slope at t=0 -> no click); 50ms linear fade of the final tail to exact zero; overallGain = masterVolume * 0.22.
- WAV conversion (audioBufferToWav) unchanged.

VERIFICATION: temporary --sound-dump flag in main.cpp + env-gated ACHERON_DUMP_SOUNDS dump in generateChime; offscreen run wrote all 7 .wav files; PowerShell waveform analysis: first sample = 0, first 10 samples ramp 0,0,1,1,7,7,16,16... (smooth attack, no onset click), max sample-to-sample jump 846-1057 (normal 14-16% of peak sine slope at 44.1kHz, not a click spike), last 100 samples all zero (silent tail), peaks ~15-20% full scale. All diagnostics REMOVED afterwards (grep confirms no trace). Build green, ctest 8/8, --runtime-smoke OK. Do NOT commit.

## 2026-08-23T08:07:25.225Z · UI audit 2024 · Audited Acheron UI (chat/main window/dialogs/input/channel list/tabs/forum/member list): 7 bugs + 3 incomplete features found, 0 P0/P1, no code changed.

## UI code audit (chat, main window, dialogs, input, channel list, tabs, forum, member list) — C:\Users\me\GIT\acheron

Read-only audit; 10 findings delivered. Key verified facts for future work:

- P2 bugs:
  1. ChatModel::cleanupStickerMovies()/cleanupGifAnimations() (src/UI/Chat/ChatModel.cpp:1925/2029) have no call sites anywhere in src/ → animated stickers/GIFs accumulate + frameChanged→dataChanged repaint storms; only cleaned on channel switch (setActiveChannel qDeleteAll at :1698).
  2. Created phase-2 nonce replacement (ChatModel.cpp:1288-1293) drops size/attachment/embed/reaction caches for the placeholder id but omits invalidateDocCacheForMessage(oldId) → stale QTextDocument + retained docCacheSubIds entry per sent message.
  3. activateChannel always calls requestLoadChannel (ChannelSelectionController.cpp:788); RAM-cache path always emits Latest (MessageManager.cpp:108-141); ChatModel Latest always beginResetModel + ChatView modelReset → scrollToBottom (ChatView.cpp:386) → every tab switch truncates history to last 30 msgs and loses scroll position.
  4. MainWindow.cpp:1296-1300 pinnedMessagesRequested Q_UNUSED(channelId) → opens pins for the active channel, not the requested one.
- P3 bugs: jumpToBottomButton fade race (ChatView.cpp:985-997); jumpToMessage 5s timeout never emits messageJumpLoadFinished(false) → input stays send-blocked on silent jump failure (MainWindow.cpp:2312-2360); dead members stickerCache/docCacheWidth/lastNewMessageStart/lastNewMessageCount/isFetchingBottom.
- INCOMPLETE: VoicePage.cpp:97-99 echo cancellation "not yet implemented" (disabled); VoiceWindow.cpp:1076 privacy code stub; TypingIndicator.cpp:19-20 no avatar thumbnails (TyperInfo lacks avatar field).
- Verified FIXED (task-named): ChannelSearchPopup currentQuery assigned before fetchHistory (ChannelSearchPopup.cpp:257); all 4 search-popup signals connected (MainWindow.cpp:2394-2401); jumpToMessage 5s timeout uses QTimer::singleShot(5000, this, cleanup) + shared_ptr guards.
- Verified NON-bugs (do not re-report): ChannelSearchPopup raw Discord::Client member is safe (client created once per ClientInstance, never recreated; popup is modal so teardown is blocked while open); WindowManager newTabBehaviorChanged no-op lambda (behavior read on demand MainWindow.cpp:2081); poll vote pollId==messageId; doc-cache theme/font staleness invalidated on theme/metrics change.

## 2026-08-23T08:18:41.657Z · codebase-audit-and-bugfixes · Full codebase audit found 34 bugs + 14 incomplete features across 4 subsystems; fixed 8 safe isolated bugs (2 P1 protocol, 1 P1 sound-override, 1 P1 emoji-page, 4 P2), build green ctest 8/8 smoke OK.

CODEBASE AUDIT (4 parallel read-only subagents across Core, Discord/Proto, UI chat/main/dialogs, UI settings/guildsettings/storage).

TOTAL FINDINGS: 34 BUGS (0 P0, 6 P1, 25 P2, 3 P3) + 14 INCOMPLETE FEATURES.

FIXED (8, all verified build+ctest+smoke):
1. P1 Proto skipField infinite-misparse: ProtoReader.cpp skipField returns false without advancing for group/unknown wire types; 3 callers (readInt64Value/readStringValue/readUInt64Value) ignored return. FIX: callers now break on false.
2. P1 Gateway 4006/4007 resume-loop → permanent disconnect: isFatalCloseCode didn't include SESSION_NO_LONGER_VALID/INVALID_SEQ; canResume/sessionId only cleared by INVALID_SESSION opcode, never by close codes → client looped RESUME→4006 until attempts exhausted. FIX: clear sessionId/canResume on 4006/4007 close frames (mirrors INVALID_SESSION pattern, Gateway.cpp ~line 975).
3. P2 Gateway double-disconnect: close-frame emitted disconnected(cc) then exhausted-attempts emitted disconnected(INTERNAL) — second reason overrode first. FIX: added alreadyEmittedDisconnect flag.
4. P1 SoundOverrideWidget::changed() never connected — sound override edits silently lost. FIX: connected changed()→onSettingChanged() in NotificationsPage::setupSoundTab.
5. P1 EmojiPage dead UI — setEnabled() never called, Upload/Rename/Delete permanently disabled. FIX: added MANAGE_EXPRESSIONS permission check mirroring StickersPage pattern (using Core::Snowflake, includes Discord/Client.hpp, Discord/Enums.hpp, Core/PermissionComputer.hpp).
6. P2 AuditLog filter combo mislabeled: {60,"Message Delete"} but actionTypeToString(60)="Sticker Create"; missing 51 "Emoji Update", 61/62 Sticker Update/Delete. FIX: corrected labels and added missing entries.
7. P2 Duplicate imageFetched connections: NotificationsPage::setNotificationManager reconnected on every settings open. FIX: Qt::UniqueConnection.
8. P2 ChatModel nonce-replacement missing invalidateDocCacheForMessage(oldId) — stale QTextDocument + retained docCacheSubIds for each sent message. FIX: added call alongside other cache removes.

UNFIXED — RISKIER/NEEDS DESIGN (presented to user for selection):
P1: subscribedGuilds never cleared after reconnect (stale member lists/typing); DaveSession ssrcMap snapshot (DAVE breaks after epoch transitions); per-channel sound override dead branch (keyed by sound-ID not channel-ID).
P2: Gateway stop() during backoff (31s delay); RemoteAuthClient stop() joins thread (20s UI freeze); VoiceGateway sendBinaryPayload malformed DAVE frames; VoiceGateway recv-error spin (100Hz); VoiceGateway handleHello early-return (heartbeat-less); VoiceClient RTP extension AAD inconsistency; tab switch truncates history + scroll reset; pinnedMessagesRequested opens wrong channel; FadeInDelegate shared animation loses removals; voice device settings not applied at startup; Reset All sounds no-op; GIF unload doesn't abort download; VoiceManager terminate path skips stop(); HiDPI blurry avatars; guild fetch widgets discard REST callbacks; statusMessage never connected; guild settings Invites/Integrations ungated; cache DB no migration.

INCOMPLETE FEATURES (14): echo cancellation (disabled); privacy code stub; typing indicator avatars; role drag-reorder cosmetic; webhook channel picker empty; audit-log user filter empty; built-in sound preview stub; NotificationDecider dead code; AnimationConfig signals unconnected; SoundManager signals unconnected; forward pagination missing; VoiceClient resume loses DAVE; m_lastNotificationContext stale slot; voice leave/move notification gating.

## 2026-08-23T14:25:14.931Z · bugfix-pass-2 · Completed full bug-fix pass: 27 code fixes across Gateway/Client/Voice/DAVE/UI/Storage/Notification subsystems + verified 3 false positives + 1 feature-deferral; build green, ctest 8/8, smoke OK.

SECOND (EXHAUSTIVE) BUG-FIX PASS — all findings from the 4-agent audit now triaged and addressed. Build green, ctest 8/8, runtime smoke OK.

FIXED IN THIS PASS (18 code changes beyond the 8 from the first pass):
- Gateway waitInterruptible now also observes wantToClose → stop() during backoff no longer blocks ~31s.
- Gateway RESUMED dispatch added (Enums.hpp enum+parse map, Gateway.cpp handleResumed emits gatewayResumed, resets reconnectAttempts; Gateway.hpp signal). Client onGatewayResumed() sets Connected + re-applies presence; Client.hpp signal+method. Also subscribedGuilds.clear() in onGatewayReady so a fresh IDENTIFY re-issues lazy subscriptions.
- VoiceGateway sendBinaryPayload now prepends [2B seq] to match the receive parser framing ([2B seq][opcode][payload]); binarySeq atomic added.
- VoiceGateway recv loop: CURLE_GOT_NOTHING / !meta now break→reconnect instead of busy-looping (mirrors main Gateway).
- VoiceGateway handleHello clamps invalid heartbeat interval to 41250ms default instead of early-returning heartbeat-less.
- VoiceClient onDatagram uses rtpHeaderSize() for full RTP-header AAD (fixed extension-header AAD mismatch).
- VoiceManager stopVoiceThread terminate-path now calls ap->stop()/vc->stop() synchronously before delete.
- RemoteAuthClient::stop() detaches httpThread instead of joining (kills 20s curl UI freeze).
- MessageManager::requestLoadChannel RAM-cache path returns the FULL cached history, not just last 30 (fixes tab-switch truncation + scroll loss).
- pinnedMessagesRequested now threads channelId through MainWindow→ChannelSelectionController::openPinnedMessages(channelId); toolbar button falls back to active channel.
- FadeInDelegate: replaced per-call `finished` SingleShot connections (accumulated stale callbacks) with pendingDone QHash + completion in onTick().
- VoicePage::setVoiceManager applies saved input/output device + noise suppression at startup.
- NotificationsPage::onResetSounds now calls widget->loadFromJson({}) to reset widget state, not just item data.
- GifAnimation::unload() aborts in-flight m_activeReply (defeats LRU cap bypass).
- ImageManager: non-proxy CDN images now get HiDPI dpr scaling (both disk-cache and storeFetchedPixmap paths).
- BanListWidget/InvitesListWidget/WebhooksWidget/IntegrationsWidget/AuditLogWidget: fetch callbacks now handle Result error → show error text instead of "Loading…" forever.
- GuildSettingsDialog: statusMessage from 6 pages now connected to a status QLabel (m_statusLabel + onStatusMessage slot).
- DatabaseManager: applyCacheMigrations() stamps PRAGMA user_version=1 on per-account cache DB (versioning foundation).
- NotificationManager: removed dead channel-keyed sound override branch; selectSoundForNotification now applies per-sound-TYPE override (m_soundOverrides keyed by "mention1" etc.). Replaced shared m_lastNotificationContext with shouldUseUserSound() computing context from data + notifyForList(). Removed NotificationContext struct + 5 set-sites + reset.
- ChatView/ChatModel dead members removed: isFetchingBottom, stickerCache, docCacheWidth, lastNewMessageStart/Count.
- SoundManager error lambda now qWarning()s playback errors (signal had no receivers).

VERIFIED FALSE POSITIVES (no change — would regress):
- DaveSession::ssrcMap is a `const&` bound to VoiceClient::ssrcToUserIdMap (live reference, not a construction snapshot).
- VoiceClient::onGatewayResumed should NOT recreate daveSession on RESUME (session/MLS state is intact).
- MainWindow jumpToMessage 5s timeout does NOT leave input blocked (scrollToMessage never called on that path).
- Voice leave/move gated by notifyVoiceChannelJoins is a single-toggle design choice (independent toggles = feature, deferred).

REMAINING INCOMPLETE FEATURES (not bugs, not fixed): echo cancellation stub, E2EE privacy-code stub, typing-indicator avatars, role drag-reorder cosmetic, webhook channel picker empty, audit-log user filter, built-in sound preview, NotificationDecider dead code, AnimationConfig live-retune signals, forward pagination, voice leave/move independent toggles.

