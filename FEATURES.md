# Acheron Feature Roadmap

A running wishlist of features for the Acheron client. Organized into
**requested** (explicitly wanted) and **brainstorm** (pick-and-choose) sections.

---

## Requested

### 1. Toast Notifications (overhaul)

Currently toasts are implemented (`src/UI/Widgets/ToastNotification.*`,
`ToastContainer.*`) but they don't reliably show. Goals:

- **In-window toasts** — render inside the main window (overlay anchored to the
  window's corner), AND/OR
- **On-monitor toasts** — frameless, always-on-top OS-level windows pinned to a
  screen corner (current behavior).
- Both modes should coexist, with a mode selector per notification source. (If window is NOT focused, show monitor toasts, if window is focused, show in-app/in-window toasts)

Mimic the following feature set (keep extras we already have / that make sense):

**Message notifications**
- Separate handling for DMs, Group DMs, and server messages.
- Notify selectively by mention status (all / mentions only / none).
- Per-server notification level (All, Mentions, Specific Channels, None).
- Friend-only server message notifications option.
- Allowlist (specific users/channels) and ignore list.
- Batching + deduplication to prevent spam.
- Respect server notification settings.
- Context menu options to add, remove channels/servers/users from lists.

**Voice channel notifications**
- Alerts when friends join/leave voice.
- Per-user debounce to reduce spam.
- Per-channel join/leave toggles.

**Friend request notifications**
- Incoming friend request alerts.
- Friend-accepted notifications.
- Streamer-mode redaction.

**Smart filtering**
- Ignore specific users.
- Allowlist channels/servers.
- Friend-network notifications.
- Streamer-mode compliance.
- Do Not Disturb (DND) mode.
- Per-channel/server muting with expiration cleanup.

**Display modes**
- overlay-style (in-window) with corner placement + pixel offsets.
- Native/OS-window toasts with separate placement rules for DMs vs server.
- "Auto" mode: in-window when focused, OS toasts when unfocused.

**Positioning / layout**
- 4 corners + optional center.
- Pixel-perfect horizontal/vertical offsets.
- Max visible toasts limit.
- Toast duration (per notification type).
- Stack size / coalesce window for grouping rapid messages.
- Always-on-top option (OS mode).
- Multi-monitor: select which display, with different rules for DMs vs servers.

**Animations**
- Entrance: fade, slide (4 directions), pop, zoom, none.
- Exit: fade, slide (up/down), pop, zoom, none.
- Timing/easing options.

**Appearance**
- Fonts (Nunito, Inter, Poppins, Roboto, Open Sans, Lato, Segoe UI, Arial).
- Adjustable title / channel / body font sizes.
- Light/Dark themes.
- Badge colors per channel.
- Image display + attachment indicators.
- Gradient backgrounds with opacity control.
- DM accent color + server accent color.

**Media display**
- Show/hide message images.
- Configurable max image dimensions.
- Attachment indicator badges.

**Sounds**
- Per-sound-type overrides (message, mention, user message, etc.).
- Default / Discord sounds / custom file per type.
- Volume per sound type.
- Per-user custom sounds (unique sound for specific users).
- Behavior rules: notify-list users / DMs / notify-list channels trigger custom
  sounds; FIFO lookup (first-message-first).
- Right-click context menu to configure per-user sounds.
- Custom audio file upload (WAV/MP3/etc.) + validation.
- Import/export of sound config.

**Privacy / streaming**
- Redact message content when streaming.
- Hide image attachments / mentions.
- Option to disable notifications entirely.
- Three modes: Ignore / Redact / Normal.
- Suppress native system notifications option.

**Testing / debugging**
- "Preview notification" button.
- Dismiss-all command.
- Example/test toasts.

### 2. Developer Mode

- Toggle in Settings.
- When on, show **"Copy channel / user / server / emoji ID"** entries in the
  corresponding context menus (hidden when off).

### 3. Slash Command System

- Full slash-command support in the chat input, like Discord.
- `/` opens a command list; typing filters suggestions.
- Suggested commands with autocomplete for command arguments:
  - Channel/user/role/emoji mentions as arguments.
  - Command descriptions and inline preview.
- Support built-in commands and bot/application commands.
- Keyboard navigation (arrows + Enter/Tab to accept).
- Registry of commands per channel/guild; fallback behavior for unknown commands.

---

## Brainstorm (pick from)

### Chat & messaging
- Message search within a channel (and global search). (popup into separate window) ✅ DO
- "Jump to message" deep links.✅ DO
- Pinned messages panel (partially exists — finish).✅ DO
- Replies/threads UX parity with Discord (exists — polish).✅ DO
- Message editing history.
- Delete-for-me vs delete-for-everyone clarity.
- Spoiler syntax support everywhere (exists for images — extend to text).✅ DO
- Code block language detection + syntax highlighting (exists — extend).✅ DO
- Timestamps in multiple formats (relative, absolute, short).✅ DO
- Forwarding messages across channels.✅ DO
- Quick reactions bar (exists — extend with per-server emoji).✅ DO
- Frequently-used emoji row in the picker.✅ DO
- Emoji search with aliases.
- Sticker favorites / recently used.✅ DO
- typing indicators (exists — extend).✅ DO( (able to turn off your own typing indicator in settings or chat bar)
- Unread message count badges + jump-to-first-unread. ✅ DO
- Mark channel/guild as read. ✅ DO
- Message drafts (persist unsent text per channel).
- Send-on-enter / send-on-shift-enter configurable.
- Automatic translation of messages (opt-in).
- Spell-check integration.
- Markdown rendering in chat/chatbar ✅ DO

### Voice & video ❌ NO VIDEO/VIDEO CHANGES YET
- Screen share (window + full screen).
- Video calls.
- Noise suppression / echo cancellation (exists — expose more controls).
- Voice activity sensitivity slider.
- Push-to-talk with per-app global hotkey (exists on Windows — port to Linux).
- Voice disconnect/reconnect handling.
- Voice activity indicators on avatars.
- Krisp-style AI noise suppression toggle.
- Volume mixer per user.
- Stream watch party (click-to-join a screen share).
- Stage channels.
- Speaking role highlight / "who's talking" overlay.

### Media & attachments
- Image gallery with keyboard navigation (exists — polish). ✅ DO
- GIF/video autoplay toggles (GIF autoplay done — add per-user/settings). ✅ DO
- Save attachment as... (exists — extend to bulk). ✅ DO
- Copy image / copy link / open in browser (exists — extend). ✅ DO
- Inline video player with seek + volume (exists — polish). ✅ DO
- Audio message playback with seek. ✅ DO
- Drag-and-drop paste images from clipboard (exists — extend).✅ DO
- EXIF/orientation handling. 
- Preview before upload with size warnings.
- Attachment size/format limits feedback. ✅ DO
- Animated webp/avif support. ✅ DO

### Embeds & previews
- Rich embeds for all link types (exists — extend). ✅ DO
- Bot embed rendering (done — extend with more field types). ✅ DO
- Link previews for Twitter/X, YouTube, Spotify, etc.
- OpenGraph fallback for generic links.
- In-app video playback for embeds (done — extend to more sites). ✅ DO
- "Open in browser" fallback when a site blocks embeds. ✅ DO
- Poll rendering + voting. ✅ DO
- Application command / interaction previews. ✅ DO

### Search
- Global search (messages, channels, users, servers). ✅ DO
- Search filters (from:, in:, before:/after:, has: link/image/file). ✅ DO
- Recent searches. ✅ DO
- Search within a thread/channel with jump. ✅ DO
- Fuzzy search for channels/servers. ✅ DO

### Servers & channels
- Server folders / grouping.
- Channel categories collapse/expand (exists — polish).
- Channel reordering.
- Server discovery / join by invite.
- Invite management UI (create/revoke).
- Server settings (overview, roles, emoji, stickers, webhooks — exists — finish). ✅ DO
- Audit log (exists — polish).
- Server boost / perks display.
- Channel slowmode indicator (exists — extend). ✅ DO
- Threads: create, browse, archive (exists — polish).
- Forum channels (exists — polish).
- Announcement channels.
- Server template import/export.

### Moderation (power user)
- Bulk delete.
- Ban/kick with reason + duration (temp ban). ✅ DO
- Timeout with duration picker.  
- Role management (exists — polish). ✅ DO
- Member list role badges. ✅ DO
- Permission overview per role.
- Mute/deafen server members (voice). ✅ DO
- Message report flow.
- Moderation queue / recent actions.

### User experience / UI
- Compact mode (exists — polish). ✅ DO
- Font size / zoom settings. ✅ DO
- Custom accent color + full theming (exists — extend). ✅ DO
- Dark/light/AMOLED themes. ✅ DO
- Per-server notification override quick toggle. ✅ DO
- Collapsible sidebar. ✅ DO
- Multi-account switching (exists — polish). ✅ DO
- Account switcher with avatars. ✅ DO
- Custom status (exists — polish).
- Presence (online/idle/dnd/invisible) picker. ✅ DO
- User profile popup (exists — polish): mutual servers, roles, banner.
- Server banner + animated avatars display.
- Command palette (Ctrl+K) to jump anywhere. 
- Split view (two channels side by side). ✅ DO

### Theming & customization
- Custom CSS/QSS themes.
- Theme import/export + gallery.
- Per-theme accent colors.
- Font family choice for the whole app. ✅ DO
- Custom emoji font fallback.
- Transparency/blur effects for the window. ✅ DO (togglable)
- Custom notification sounds beyond the notification system. 

### Notifications
- Desktop notifications via native APIs (Linux: DBus; Windows: existing). ✅ DO
- Notification action buttons (Reply, Mark read). ✅ DO
- Notification click → focus channel. ✅ DO
- Sound + badge behavior per channel. 
- Quiet hours / schedule.
- Do Not Disturb with exceptions.

### Privacy & streaming
- Streamer mode (exists — extend: redact more).
- Hide personal info (email/phone/tags).
- Hide invite links.
- Block/allowlist for message content.
- Incognito/offline presence.
- Disable read receipts for DMs.
- Local data clearing (cache, message history).

### Performance
- Lazy image loading + disk cache (exists — extend). ✅ DO
- Message virtual list (exists — extend to channels/members). ✅ DO
- Animated GIF memory caps (exists — extend). ✅ DO
- Background fetch batching. ✅ DO
- Reduce startup time. ✅ DO
- Optional hardware acceleration toggle. ✅ DO

### Accessibility
- High-contrast theme.
- Keyboard navigation for everything.
- Screen-reader labels (ARIA/accessibility names).
- Focus indicators.
- Reduce motion toggle (disable animations).
- Font scaling up to very large.

### Power user / QoL
- Keybind remapping UI.
- Command palette (Ctrl+K).
- Quick switcher (Ctrl+T) for channels/DMs.
- Message bookmarking / "later".
- Message quotes.
- Export chat history.
- Markdown shortcuts in the composer (bold/italic/etc. via keyboard). 
- "Copy message link" (exists — extend). ✅ DO
- Auto-complete for emoji, users, channels, roles in the composer (exists — extend). ✅ DO
- Duplicate-tab / multi-window. ✅ DO

### Account & login 
- QR login (exists — polish).✅ DO
- Token input (exists).
- 2FA / captcha flows (exists — polish).
- Multiple account sessions (exists — polish).
- Account status/health indicators.

### Extensibility / plugins ❌
- Plugin system (native or scripted) with a plugin store.
- User scripts / CSS.
- Webhook sender utility.
- API access for bots/devs.

### Misc / nice-to-have
- Updater with auto-update (check for new release).
- Crash reporter + diagnostics bundle.
- Language/localization for the UI.
- Data import from Discord (settings/blocked lists).
- Rich presence on the OS (playing/status).
- Server-side mute/sound sync. 
- "New server" onboarding wizard.
- In-app help + shortcut sheet (exists — polish).
