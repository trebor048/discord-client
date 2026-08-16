# Acheron Implementation Plan

> Canonical execution plan. Feature IDs (`F1…F66`) map 1:1 to the `✅ DO` items in
> [`FEATURES.md`](../FEATURES.md). Three verticals use separate IDs:
> **N** (Notifications), **D** (Developer Mode), **S** (Slash Commands).

---

## Global rules (apply to every stage)

**Definition of Done (DoD) — a stage is "done" only when ALL are met:**
1. Builds clean with warnings treated as errors on the touched TUs (`-Werror` / `/WX`).
2. Unit tests added for any new pure logic (decider, parsers, registries, matchers).
3. Full `ctest` passes (both `linux` and `windows` presets).
4. No new leaks (ASan/LSan on touched paths).
5. Perf guardrail: image/scroll paths measured (no worse than +10%).
6. Manual checklist pass on Windows (primary); compile-verified on Linux.

**Bug-hunting discipline (continuous):**
- Read neighbors + callers before editing; grep all callers of changed symbols after.
- End every stage with a regression sweep of touched files + reverse-dependencies.
- Log via `qCInfo/qCWarning(LogCore/LogUI)` at decision points.
- Prefer pure/testable helpers over logic embedded in widgets.

**Optimization discipline:**
- Reuse `ImageManager` (images/GIF), `docCache` (markdown); add bounded caches for new hot paths.
- Debounce typing-driven work (follow `ChatView`'s 200 ms `searchDebounceTimer`).
- Never fetch on every paint (follow `suppressImageFetch` / `getIfCached`).

---

## File ownership & dependency map (conflict avoidance)

| File(s) | Primary owner phase | Notes |
|---|---|---|
| `Core/Settings.*` (new) | 0.1 | Consumed by N, D, 4.12–4.14, 7.x |
| `NotificationTypes.hpp`, `NotificationManager.*`, `ToastContainer/ToastNotification.*`, `NotificationsPage.*`, `SoundManager.*` | **N only** | Single owner |
| `Core/Notification/NotificationDecider.*` (new) | 0.3, used by N + 7.5 | Single source of truth |
| `ContextMenuFactory.*`, `ChatView::contextMenuEvent` | D + N.1.10 | D first, then N adds Notify/Ignore |
| `MessageInput.*`, `ChatTextEdit` | S + 4.15 + 8.8 | Sequence S then 8.8 |
| `ChatView.*` search panel | 4.1 + 4.7 | Move search to popup before filters |
| `ChatModel.*`, `ChatDelegate.*`, `ChatLayout.*`, `ImageManager.*` | 5.x + 8.1/8.3 | Media first, perf after |
| `ReadStateManager.*`, `ChannelTreeModel/View.*` | 4.13, 4.14 | Logic exists; UI wiring only |
| `Discord/Client.*`, `Entities.hpp`, `Events.hpp` | S, 4.8, 5.12, 6.3/6.6 | One REST PR per feature |
| `Storage/MessageRepository.*`, `DatabaseManager.*` | 4.1, 5.14 | One schema migration point |
| `Theme/*` | 7.2–7.4, 7.10, 7.11 | Centralize color/font resolution |

**Sequencing hard constraints:**
- 0.1 before N, D, 4.12/4.13/4.14, 7.x.
- 0.3 before N.1.3 and 7.5.
- N.1.1 before any N appearance work.
- 4.1 before 5.14.
- 5.10/5.11 before 8.1/8.3.

---

## Phase 0 — Foundations

### 0.1 — `Core::Settings` service + per-channel/server overrides
- New `src/Core/Settings.{hpp,cpp}`: `class Settings : QObject` with `AppSettings` struct (`ui`, `notifications`, `privacy`, `streamer`, `dev`, `sound`), `load()`, `save()`, `setValue()`, `value()`, `changed()` signal.
- `ChannelNotificationOverride { level; muted; muteUntilMs; }` + `QHash<Snowflake, ChannelNotificationOverride>` and server overrides (keys `overrides/channels/<id>/...`).
- Migrate existing keys: `ui/channelListMode`, `ui/compactMessages`, `ui/showTimestamps`, `ui/compactInput`, `ui/newTabBehavior`, `general/in_memory_cache`, `notifications/*`, `privacy/*`, `streamer/*`.
- Refactor `SettingsWindow::addPage` to pass `Settings*`; update every `*Page.cpp`.

### 0.2 — Developer-mode flag helper
- `dev/developerMode` (default false); `bool Settings::isDeveloperMode() const`; toggle in `GeneralPage`.

### 0.3 — `NotificationDecider` (pure engine)
- New `src/Core/Notification/NotificationDecider.{hpp,cpp}`; free function
  `NotificationDecision decide(const Discord::Message&, const Discord::Channel&, const AppSettings&, const overrides&, bool isActiveChannel, bool isStreaming, bool isDnd)`.
- Move logic from `NotificationManager::shouldShowNotification/isMentioned/getChannelNotificationLevel/isOwnPresenceDnd/evaluateStreamerMode`.
- `struct NotificationDecision { bool notify; bool sound; NotificationLevel level; bool redact; }`.
- Tests `tests/tst_NotificationDecider.cpp`.

---

## Phase N — Notification Overhaul

- **N.1.1 Make toasts show (bug fix first):** instrument `displayToast`/`onMessageCreated`; verify container parenting/raise/z-order; confirm `MESSAGE_CREATE` wiring; check `activeChannel` suppression.
- **N.1.2 Dual-mode + auto:** in-window overlay + monitor frameless + `Auto` (focused→in-window, unfocused→monitor); multi-monitor via `QGuiApplication::screens()`.
- **N.1.3 Positioning/layout/animation/coalescing:** `EntranceAnimation/ExitAnimation` (fade/slide/pop/zoom/none), stackSize, coalesceWindowMs, per-type durations, DM-vs-server placement.
- **N.1.4 Appearance:** fonts (Nunito/Inter/Poppins/Roboto/Open Sans/Lato/Segoe UI/Arial), title/channel/body sizes, light/dark, per-channel badge, gradients+opacity, DM/server accents.
- **N.1.5 Media display:** show/hide images, max image dims, attachment badge.
- **N.1.6 Sounds + per-user sounds:** sound-type overrides, per-user FIFO, volume, upload (app data dir), import/export.
- **N.1.7 Voice + friend notifications:** join/leave (per-channel, debounce), friend request/accepted.
- **N.1.8 Privacy/streamer + native suppression:** Ignore/Redact/Normal, disable-all, `NativeMode::Never`.
- **N.1.9 Settings UI + testing/debug:** complete `NotificationsPage`, preview + dismiss-all buttons.
- **N.1.10 Context-menu list management:** "Notify/Ignore" for user/channel/server via existing `addToNotifyList`/`removeFromIgnoreList` etc.

---

## Phase D — Developer Mode
- Gate all "Copy ID" entries (`ContextMenuFactory` user menu already has one; add channel/server/emoji) behind `Settings::isDeveloperMode()`.

## Phase S — Slash Command System
- `ApplicationCommand` entities; `Client::fetchApplicationCommands` + send; `SlashCommandPopup` (mirror `EmojiAutocompletePopup`); `MessageInput` `/` trigger + arg autocomplete (channel/user/role/emoji); per-guild registry.

---

## Phase 4 — Chat & Messaging
4.1 Search popup + global (F1) · 4.2 Jump to message (F2) · 4.3 Pinned messages (F3) · 4.4 Replies/threads parity (F4) · 4.5 Text spoilers (F5) · 4.6 Code language detection (F6) · 4.7 Timestamp formats (F7) · 4.8 Forward messages (F8) · 4.9 Quick reactions + per-server emoji (F9) · 4.10 Frequently-used emoji row (F10) · 4.11 Sticker favorites/recent (F11) · 4.12 Typing indicators + self-disable (F12) · 4.13 Unread badges + jump (F13) · 4.14 Mark as read (F14) · 4.15 Markdown in composer (F15)

## Phase 5 — Media, Embeds, Search
5.1 Gallery keyboard nav (F16) · 5.2 Autoplay toggles (F17) · 5.3 Bulk save (F18) · 5.4 Copy/link/open (F19) · 5.5 Inline video seek/volume (F20) · 5.6 Audio message seek (F21) · 5.7 Clipboard paste (F22) · 5.8 Attachment feedback (F23) · 5.9 Animated webp/avif (F24) · 5.10 Rich embeds (F25) · 5.11 Bot field types (F26) · 5.12 In-app video + fallback (F27/F28) · 5.13 Polls (F29) · 5.14 App-command previews (F30) · 5.15 Global search + filters + recent + jump + fuzzy (F31–F35)

## Phase 6 — Servers, Moderation, Roles
6.1 Server settings finish (F36) · 6.2 Slowmode (F37) · 6.3 Ban/kick + temp ban (F38) · 6.4 Role management (F39) · 6.5 Member role badges (F40) · 6.6 Mute/deafen (F41)

## Phase 7 — UX, Theming, Presence
7.1 Compact mode (F42) · 7.2 Font size/zoom (F43) · 7.3 Accent/theming (F44) · 7.4 Dark/light/AMOLED (F45) · 7.5 Per-server override toggle (F46) · 7.6 Collapsible sidebar (F47) · 7.7 Multi-account + avatars (F48/F49) · 7.8 Presence picker (F50) · 7.9 Split view (F51) · 7.10 Font family (F52) · 7.11 Transparency/blur (F53)

## Phase 8 — Performance & Power-QoL
8.1 Lazy image + disk cache (F57) · 8.2 Virtual lists (F58) · 8.3 GIF caps (F59) · 8.4 Fetch batching (F60) · 8.5 Startup (F61) · 8.6 HW accel toggle (F62) · 8.7 Copy message link (F63) · 8.8 Mention autocomplete (F64) · 8.9 Multi-window (F65)

## Phase 9 — Account & Login
9.1 QR login polish (F66)

---

## Cross-cutting risks & API notes
- Temp bans: no native duration → client-side scheduled unban.
- Polls: verify Discord poll/interaction API.
- Linux native notifications: DBus `org.freedesktop.Notifications` backend.
- Audio upload: app data dir (not IndexedDB).
- Search perf: SQLite FTS/index; cap + debounce results.

## Suggested sprint order
1. 0.1 + 0.3 + N.1.1 + N.1.2 (foundation + toasts show + dual-mode).
2. D + N.1.3/N.1.4/N.1.6 (developer mode + toast polish + sounds).
3. S + 8.8 (slash commands + mention autocomplete).
4. 4.1 + 5.15 + 4.13/4.14 (search + unread/mark-read).
