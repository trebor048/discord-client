# Plan: Chat & Sidebar UI Polish — Layout, Scaling, Unread Counts, Animations

**Created**: 2026-08-26
**Branch**: main
**Status**: approved
**Gherkin persistence**: plan-file-only
<!-- Recorded once at plan creation. No .feature convention detected (no
     features/ dir, no manifest; the detection script is unavailable in this
     environment). Non-interactive run: prompt skipped, plan file only. -->
**Scope enforcement**: none

## Goal

Polish the main window's chat/sidebar layout and motion: make the user area
(MePanel) fill and track the channel-list column, make the message input bar
larger and own the entire bottom strip, fix the slide-out member list so it
truly overlays the chat (no chat resize during the slide, one smooth motion,
persisted mode honored at startup), default the channel list to a smaller
scale with a wider settings range, surface per-channel missed-message counts
in the channel list and tabs, glide new messages into view with a smooth
scroll, and sweep the codebase's animations onto consistent, config-aware
curves.

The app already has a mature animation layer (AnimationConfig, AnimationUtils,
HoverAnimator, DialogAnimator); this plan fixes the remaining jagged edges
rather than re-architecting it. Verified baseline: the full test suite
(10/10, `ctest --preset windows`) is green on the current working tree.

## Acceptance Criteria

- [ ] **User area**: The MePanel spans the full channel-list column width in
      both Tree and Classic modes and tracks it as the splitter resizes.
- [ ] **Message input**: Default (non-compact) input bar is taller (56 px min)
      and forms one flush, full-width bottom block with the typing/slowmode
      status (no separate strip between chat and input).
- [ ] **Member list overlay**: In SlideOut mode the chat pane width never
      changes while the member list slides; the persisted mode is applied at
      startup; expanding and collapsing each happen in one motion.
- [ ] **Channel scaling**: Channel list default scale is 0.85 (85%); the
      Appearance settings stepper for the channel list spans 50%–200% (other
      scales keep 80%–150% / 100% default); pixel math clamps correctly at
      the wider bounds.
- [ ] **Unread counts**: Each channel shows the number of messages missed
      since it was last viewed (numeric badge, capped at 99+); muted channels
      show none; the count resets when the channel is opened; tabs show the
      count too (mention badge wins when both exist). Counts are seeded from
      the local message cache at startup so they survive restarts.
- [ ] **Smooth new-message scroll**: When the user is at the bottom and a new
      message arrives, the chat glides to the new bottom (no snap); with
      reduce-motion enabled it is instant; if scrolled up, position is kept.
- [ ] **Animation consistency**: All transition durations route through
      AnimationConfig (speed multiplier + reduce-motion); entrances use
      OutCubic, exits InCubic; channel switches use the same smooth scroll as
      new messages.
- [ ] All tests pass: `ctest --preset windows` (baseline 10/10 + new suites).

## Decisions (default stances on ambiguous axes)

Non-interactive run — recommended defaults taken and recorded, per
`.opencode/knowledge/decision-defaults.md`:

- **Unread-count semantics**: counts = messages newer than the channel's last
  read/ack point. Live increments are session-local; the initial value is
  **seeded from the local message cache** (`MessageRepository`) at READY so
  counts survive restarts where history is cached. Discord exposes no server
  message counts, so channels without cached history start at 0 (review
  flag, resolved by seeding; residual limitation recorded in Risks).
- **"Bigger" input bar**: non-compact min height 56 px (was 44); compact mode
  unchanged (30 px) and keeps its own look. No custom sizing option added.
- **Channel scale range**: 0.50–2.00 with default 0.85, applied to the channel
  list axis only; member cards and guild icons keep 0.80–1.50 default 1.00.
- **MePanel**: fills the column width horizontally (size policy), keeps
  content-sized height (Discord-style); it does not stretch vertically.
- **Muted channels**: counts are zeroed at the source (`ReadStateManager`
  mute branch), so neither the channel list nor tabs can show a muted count.
- **Scope**: the 7 requested areas + the animation audit the request
  explicitly authorizes; no unrelated refactors. Accessibility-tree exposure
  of the new badges and overlay focus management are recorded as follow-ups,
  not shipped here.
- **Existing user settings** are preserved (new scale defaults only affect
  fresh installs, since QSettings stores the chosen value).

## Slices

### Slice 1: Channel list scaling — smaller default, wider range

**Depends-on:** none
**Files:** `src/Core/Appearance/AppearanceConfig.hpp`, `src/Core/Appearance/AppearanceConfig.cpp`, `src/UI/Settings/AppearancePage.cpp`, `src/UI/ChannelList/ChannelDelegate.cpp`, `src/UI/MainWindow.cpp`, `tests/tst_AppearanceConfig.cpp`, `tests/tst_ScaleStepper.cpp`

**Behavior:**

```gherkin
Feature: Channel list scaling

  Scenario: Smaller default scale
    Given a fresh user configuration
    When the appearance settings load
    Then the channel list scale defaults to 0.85

  Scenario: Wider adjustment range
    Given the Appearance settings page
    When the user steps the channel-list scale
    Then the range extends from 0.50 to 2.00, clamping at both bounds

  Scenario: Other scale axes unaffected
    Given a fresh user configuration
    When the appearance settings load
    Then member cards and guild icons keep the 0.80 to 1.50 range with a 1.00 default

  Scenario: Persisted value outside the old range is honored
    Given a stored channel scale of 1.7 from a previous version
    When the appearance settings load
    Then the channel scale loads as 1.7

  Scenario: Pixel math honors the wider bounds
    Given a 24 px base and a channel scale of 2.00
    When the scaled size is computed
    Then the result is 48 px
```

**Steps:**

#### Step 1.1: Per-axis scale constants, clamping, and range-aware math

**Complexity**: standard
**IMPLEMENT**: Add channel-specific constants (`kChannelMinScale = 0.50f`,
`kChannelMaxScale = 2.00f`, `kChannelDefaultScale = 0.85f`) and
`clampChannelScale(float)`; `load()`/`setChannelScale()` use it. Add a single
range-aware `scaledInt(int base, float scale, float min, float max)`
overload (the existing `scaledInt(base, scale)` delegates with the global
range), a `channelScaledInt(int base)` alias, and a matching range-aware
`stepScale(float value, int steps, float min, float max)` overload. Fix the
latent bug where channel scales above 1.50 were silently clamped by the
shared `scaledInt`.
**TEST**: Extend `tst_AppearanceConfig` — new channel defaults; channel
clamping at 0.50/2.00; member/guild axes unchanged (0.80–1.50, default 1.00);
persisted 1.7 loads as 1.7 (out-of-old-range value); `scaledInt(24, 2.0f,
channel range) == 48`; channel-aware `stepScale` clamps at 2.00. Full suite
green.
**REFACTOR**: Keep the shared constants for member/guild; the range-aware
overload is the single implementation, with `channelScaledInt` the one alias
used by channel call sites.
**Files**: `src/Core/Appearance/AppearanceConfig.hpp`, `src/Core/Appearance/AppearanceConfig.cpp`, `tests/tst_AppearanceConfig.cpp`
**Commit**: `feat: smaller channel scale default with wider range`

#### Step 1.2: Wire the wider stepper range in settings

**Complexity**: trivial
**IMPLEMENT**: In `AppearancePage`, call `stepper->setRange(0.50f, 2.00f)` on
the "Channel list" row only; member/guild rows keep the default range.
**TEST**: Extend `tst_ScaleStepper` with a `setRange` case (buttons disable at
custom bounds, value clamps); full suite green.
**REFACTOR**: Extract the three scale-row closures into a small helper lambda
so the channel row's custom range is a one-line difference.
**Files**: `src/UI/Settings/AppearancePage.cpp`, `tests/tst_ScaleStepper.cpp`
**Commit**: `feat: widen channel scale range in appearance settings`

#### Step 1.3: Channel-aware scaledInt at channel call sites

**Complexity**: standard
**IMPLEMENT**: Switch `ChannelDelegate::paint/sizeHint` and MainWindow's
`configChanged` channel-icon sizing to `channelScaledInt`, so values above
1.50 scale correctly.
**TEST**: Full suite green; manual verification: a 200% channel scale visibly
enlarges rows and icons.
**REFACTOR**: Both call sites route through `channelScaledInt` (single source
of truth for the channel range).
**Files**: `src/UI/ChannelList/ChannelDelegate.cpp`, `src/UI/MainWindow.cpp`
**Commit**: `fix: scale channel icons/rows across the full channel range`

### Slice 2: Member list slide-out — true overlay, one motion, applied at startup

**Depends-on:** none
**Files:** `src/UI/MainWindow.cpp`, `src/UI/MemberList/MemberListOverlay.hpp`, `src/UI/MemberList/MemberListOverlay.cpp`, `tests/tst_MemberListOverlay.cpp` (new), `CMakeLists.txt`

**Behavior:**

```gherkin
Feature: Member list slide-out overlay

  Scenario: Overlay never resizes the chat
    Given slide-out mode is active
    When the member list expands or collapses
    Then the chat pane width does not change

  Scenario: Persisted mode applies at startup
    Given slide-out was previously enabled
    When the app starts
    Then the member list uses the overlay (not the splitter pane)

  Scenario: Default mode still uses the splitter pane
    Given slide-out was never enabled
    When the app starts
    Then the member list remains the resizable splitter pane

  Scenario: One-motion slide out
    When the member list expands
    Then content visibility rises strictly with the panel width
    And no content pops in after the width animation finishes

  Scenario: One-motion slide in
    When the member list collapses
    Then content visibility falls strictly with the panel width
    And the panel ends in icons-only mode
```

**Steps:**

#### Step 2.1: Apply persisted member-list mode at startup

**Complexity**: standard
**IMPLEMENT**: In the `MainWindow` constructor, call
`switchMemberListMode(AppearanceConfig::instance().memberListMode())` **at
the very end, after `restoreWindowState()`** — before it would capture
pre-restore splitter sizes and let the saved member-pane width land on a
zero-width placeholder. Today the mode is only applied when the setting
changes, so the persisted slide-out choice is ignored until re-toggled. Guard
`switchMemberListMode` against re-entry when already in the requested mode.
**TEST**: Full suite green; manual verification: enable slide-out, restart →
overlay from launch; default mode → splitter pane from launch; persisted
splitter sizes survive the startup mode application.
**REFACTOR**: The re-entry guard makes startup application idempotent with
live toggling.
**Files**: `src/UI/MainWindow.cpp`
**Commit**: `fix: honor persisted slide-out member list mode at startup`

#### Step 2.2: Collapse the splitter placeholder so the chat owns the width

**Complexity**: standard
**IMPLEMENT**: In `switchMemberListMode`'s SlideOut branch, make the
placeholder truly zero-width (size policy Ignored, min/max width 0,
`setCollapsible(2, true)`) and rebalance `setSizes` so the chat regains the
member-list width immediately. Today the placeholder keeps a phantom width in
the splitter, narrowing the chat even while the overlay is collapsed — the
source of "it messes up the chat and resizes it". `raise()` the overlay above
the splitter on creation. Extract a `makeZeroWidthPlaceholder()` helper.
**TEST**: Full suite green; manual verification: in slide-out mode the chat
is edge-to-edge and expanding the overlay never shifts it.
**REFACTOR**: The helper keeps the reverse transition's cleanup (restoring
min/max 140/400 on the view) symmetric.
**Files**: `src/UI/MainWindow.cpp`
**Commit**: `fix: slide-out member list overlays chat without resizing it`

#### Step 2.3: One-motion expand/collapse

**Complexity**: complex
**IMPLEMENT**: Refactor `MemberListOverlay` so the width animation drives
content opacity by progress: `contentOpacityForWidth(width) = (width -
kCollapsedWidth) / (kExpandedWidth - kCollapsedWidth)`, applied on the width
animation's `valueChanged` (single drive animation; no two-phase reveal). On
expand: switch the delegate to full mode at opacity 0, animate width. On
collapse: animate width, opacity follows; on finish `applyIconsOnly(true)`.
Keep the rapid-hover guard (a newer animation must not be cleared by a stale
`finished`). Expose `static qreal contentOpacityForWidth(int width)` for
testing.
**TEST**: New `tests/tst_MemberListOverlay.cpp` (offscreen): construct a bare
`MemberListView` with a `MemberListDelegate` and a stub model, wrap it in the
overlay; assert `contentOpacityForWidth` is 0 at collapsed width, 1 at
expanded width, monotonic between; state-level: after `expand()`, the
delegate is in full mode at opacity 0 while width animates; after a completed
`collapse()`, the delegate ends in icons-only mode. Register the target in
`CMakeLists.txt` (mirror the `TestMemberList` block). Reset the
`AnimationConfig` singleton and isolate QSettings in init/cleanup. Manual:
stress pass over rapid hover in/out and mid-animation toggles.
**REFACTOR**: Drop `contentAnimation_` and the `revealAfter` bookkeeping now
redundant with the single drive animation.
**Files**: `src/UI/MemberList/MemberListOverlay.hpp`, `src/UI/MemberList/MemberListOverlay.cpp`, `tests/tst_MemberListOverlay.cpp`, `CMakeLists.txt`
**Commit**: `feat: one-motion slide for member list overlay`

### Slice 3A: Unread message counts — core tracking

**Depends-on:** none
**Files:** `src/Core/ReadStateManager.hpp`, `src/Core/ReadStateManager.cpp`, `src/Core/ClientInstance.cpp`, `src/Storage/MessageRepository.hpp`, `src/Storage/MessageRepository.cpp`, `tests/tst_ReadState.cpp` (new), `tests/tst_MessageRepository.cpp` (new), `CMakeLists.txt`

**Behavior:**

```gherkin
Feature: Unread message counting (core)

  Scenario: Count accumulates per channel
    Given the user is viewing channel A
    When 3 messages arrive in channel B
    Then the unread count for channel B is 3

  Scenario: Own messages are not counted
    Given the user is viewing channel A
    And channel B already has 3 unread messages
    When the user sends a message in channel B from channel A
    Then the unread count for channel B stays 3

  Scenario: Reading a channel resets its count
    Given channel B has 3 unread messages
    When the user opens channel B
    Then its unread count is 0, and messages while it is active stay at 0

  Scenario: Mark-as-read resets the count
    Given channel B has 3 unread messages
    When a bulk read/ack covers channel B
    Then its unread count is 0

  Scenario: Muted channels do not accumulate counts
    Given channel B is muted
    When 2 messages arrive in channel B
    Then its reported unread count is 0

  Scenario: Seed counts from the local cache
    Given the local cache holds messages for channel B newer than its last read point
    When the app finishes its first load
    Then channel B's unread count equals the number of cached messages past that point

  Scenario: Seeded counts exclude own messages
    Given the local cache holds messages authored by the user in channel B past its last read point
    When the app finishes its first load
    Then channel B's unread count does not include those messages

  Scenario: Active channel at startup shows zero
    Given the user last viewed channel B
    When the app starts on channel B
    Then channel B's seeded unread count is 0
```

**Steps:**

#### Step 3A.1: Track unread message counts in ReadStateManager

**Complexity**: standard
**IMPLEMENT**: Add `QHash<Snowflake, int> unreadMessageCounts` to
`ReadStateManager`. Extend `handleMessageCreated(channelId, messageId,
isMention, bool ownMessage = false)` (default keeps the existing call site
green mid-slice): own messages never increment; messages to the active
channel keep the count at 0; messages to other channels increment. Reset the
count in `updateLocalReadState`, `markChannelAsRead`, `markChannelsAsRead`,
and `setActiveChannel` (on switch). Add `int unreadMessageCount(Snowflake)
const`, a `int unreadCount = 0` field on `ChannelReadState` filled by
`computeChannelReadState` / `computeThreadReadState`, and **zero the count in
the same `fullyMuted` branch that already zeroes `mentionCount`** — so muted
channels never report counts at any layer (delegate or tab).
**TEST**: New `tests/tst_ReadState.cpp` (isolated QSettings org/app + clear):
accumulation; own-message exclusion; active-channel stays 0; mark-read and
bulk-read reset; **muted accumulation is 0**; count surfaces through
`computeChannelReadState`. Register the target in `CMakeLists.txt`. Full
suite green.
**REFACTOR**: Keep the increment path O(1); no traversal.
**Files**: `src/Core/ReadStateManager.hpp`, `src/Core/ReadStateManager.cpp`, `tests/tst_ReadState.cpp`, `CMakeLists.txt`
**Commit**: `feat: track per-channel unread message counts`

#### Step 3A.2: Pass own-message flag from the gateway path

**Complexity**: trivial
**IMPLEMENT**: In `ClientInstance::onMessageCreated`, compute `ownMessage`
(`msg.author.id == account.id`) and pass it to `handleMessageCreated`.
**TEST**: Full suite green (ReadState tests cover the flag semantics).
**REFACTOR**: Inline the author comparison into one readable expression.
**Files**: `src/Core/ClientInstance.cpp`
**Commit**: `fix: exclude own messages from unread counts`

#### Step 3A.3: Seed counts from the local message cache

**Complexity**: standard
**IMPLEMENT**: Add `int MessageRepository::countMessagesAfter(Snowflake
channelId, Snowflake afterId, Snowflake excludeAuthorId)` —
`SELECT COUNT(*) FROM messages WHERE channel_id = :c AND id > :after AND
deleted = 0 AND context_only = 0 AND author_id != :author` (mirrors the
`getLatestMessages` filtering; the author filter applies the 3A.2 own-message
exclusion at seed time). Add `ReadStateManager::seedUnreadCounts(const
QHash<Snowflake, int> &counts)` / `seedUnreadCount(Snowflake, int)`. In
`ClientInstance`, seed once from the **channel-tree population hook** (the
final cache state; not the READY hook, to avoid double-seeding), enumerating
the READY read-state channels, using `effectiveAckId(channelId, guildId)` as
`afterId` and the account id as `excludeAuthorId`; **skip the channel active
at launch** (it shows 0 until live messages arrive). Guard the seed against
re-running (idempotent: re-seeding overwrites, per the test).
**TEST**: New `tests/tst_MessageRepository.cpp` — open the cache DB on an
**in-memory QSQLITE connection** (connection-name based, like the
repositories' `getCacheConnectionName`), run the messages schema, insert rows
across channels with mixed ids/authors/deleted/context_only flags, and assert
`countMessagesAfter` semantics (strictly-after, exclusions applied, per-
channel isolation). Extend `tst_ReadState` — seeding sets counts, re-seeding
overwrites without double-counting, reading a seeded channel resets to 0.
Register both targets in `CMakeLists.txt`. Full suite green.
**REFACTOR**: Seed once; the live increment path (3A.1) is unchanged.
**Files**: `src/Storage/MessageRepository.hpp`, `src/Storage/MessageRepository.cpp`, `src/Core/ReadStateManager.hpp`, `src/Core/ReadStateManager.cpp`, `src/Core/ClientInstance.cpp`, `tests/tst_MessageRepository.cpp`, `tests/tst_ReadState.cpp`, `CMakeLists.txt`
**Commit**: `feat: seed unread counts from the message cache`

### Slice 3B: Unread counts — channel list and tab display

**Depends-on:** 3A
**Files:** `src/UI/ChannelList/ChannelNode.hpp`, `src/UI/ChannelList/ChannelTreeModel.hpp`, `src/UI/ChannelList/ChannelTreeModel.cpp`, `src/UI/ChannelList/ChannelDelegate.cpp`, `src/UI/TabBar/TabBar.hpp`, `src/UI/TabBar/TabBar.cpp`, `src/UI/ChannelSelectionController.cpp`, `src/UI/MainWindow.cpp`, `tests/tst_TabBar.cpp` (new), `CMakeLists.txt`

**Behavior:**

```gherkin
Feature: Unread message counts (display)

  Scenario: Channel shows its missed count
    Given channel B has 3 unread messages
    When the channel list is shown
    Then channel B displays a badge with 3

  Scenario: Counts are capped for busy channels
    Given channel B has 120 unread messages
    When the channel list is shown
    Then channel B displays a badge with 99+

  Scenario: Muted channels hide the count
    Given channel B is muted
    And channel B has 3 unread messages
    When the channel list is shown
    Then channel B displays no count badge

  Scenario: Read state update refreshes the badge in place
    Given channel B shows an unread count badge
    When the unread count for channel B changes
    Then the badge text updates to the new count and the list keeps its scroll position

  Scenario: Tabs show the count
    Given a channel is open in a tab
    When its unread count changes
    Then the tab displays the count, and the mention badge wins when mentions exist
```

**Steps:**

#### Step 3B.1: Expose unread count on channel nodes

**Complexity**: complex
**IMPLEMENT**: Add `int unreadCount` to `ChannelNode`; copy it in
`applyChannelReadState`/`computeNodeReadState` (leaf nodes only — no numeric
sum aggregation onto guild/category containers, which have no badge consumer
and would add list-wide noise). Add `UnreadCountRole`; include it in
`notifyIfReadStateChanged`'s comparison and `dataChanged` roles so the badge
updates in place.
**TEST**: Full suite green; the count flows through the existing
`readStateUpdated → updateReadState` signal chain (verified path). Model-level
automation needs a headless `Session` harness that does not exist; recorded
in Risks. Manual: opening a channel clears its badge without a list rebuild.
**REFACTOR**: Reuse the existing snapshot/diff machinery; no new traversal.
**Files**: `src/UI/ChannelList/ChannelNode.hpp`, `src/UI/ChannelList/ChannelTreeModel.hpp`, `src/UI/ChannelList/ChannelTreeModel.cpp`
**Commit**: `feat: expose unread counts on channel tree nodes`

#### Step 3B.2: Draw the count badge in the channel delegate

**Complexity**: standard
**IMPLEMENT**: In `ChannelDelegate::paint`, when `node->unreadCount > 0 &&
!node->isMuted`, draw a **bare right-aligned number in a muted text color**
(no pill) for chat/forum/thread nodes, capped at `99+`; the mention badge
stays a filled highlight pill and wins the rightmost slot. The `isMuted`
guard is redundant-but-harmless (counts are zeroed at the source in 3A.1).
Width-capped geometry keeps the badge inside the row at 0.85 scale.
**TEST**: Full suite green; manual verification of badge layout, capping, and
the mention-vs-count distinction (shape, not color alone).
**REFACTOR**: One badge-paint helper shared by the mention and count paths
(two color/text callers).
**Files**: `src/UI/ChannelList/ChannelDelegate.cpp`
**Commit**: `feat: show missed-message counts in the channel list`

#### Step 3B.3: Show counts on tabs

**Complexity**: standard
**IMPLEMENT**: Extend `TabBar::updateChannelReadState` with an `unreadCount`
parameter (both call sites: `ChannelSelectionController::refreshTabReadStates`
and the internal clear path); include `unreadCount` in the equality
short-circuit so updates repaint. Tab badge: mention count wins (highlight
pill); else an `unreadCount > 0` shows the capped count; else the existing
unread dot when `unread == true`. Forum tabs keep current behavior (unread
dot + mention badge; no count badge — documented gap). Extract
`badgeForState(unreadCount, mentionCount, isUnread)` returning the badge
kind/text so the rule is unit-testable.
**TEST**: New `tests/tst_TabBar.cpp` (offscreen, isolated QSettings): mention
wins over count; count caps at 99+; zero-count-unread yields the dot; equality
short-circuit repaints on count change. Register the target in
`CMakeLists.txt`. Full suite green.
**REFACTOR**: `paintTabBadge` shared by the tab strip and the overflow-menu
drawing paths.
**Files**: `src/UI/TabBar/TabBar.hpp`, `src/UI/TabBar/TabBar.cpp`, `src/UI/ChannelSelectionController.cpp`, `src/UI/MainWindow.cpp`, `tests/tst_TabBar.cpp`, `CMakeLists.txt`
**Commit**: `feat: show unread counts on chat tabs`

### Slice 4: Smooth new-message slide-up

**Depends-on:** none
**Files:** `src/UI/Chat/ChatView.hpp`, `src/UI/Chat/ChatView.cpp`, `tests/tst_ChatView.cpp` (new), `CMakeLists.txt`

**Behavior:**

```gherkin
Feature: Smooth scroll on new messages

  Scenario: New message glides the chat up
    Given the user is at the bottom of the chat
    When a new message arrives
    Then the chat scrolls to the new bottom with a smooth animation

  Scenario: Scrolled-up position is preserved
    Given the user has scrolled up
    When a new message arrives
    Then the scroll position does not change

  Scenario: Reduce-motion scrolls instantly
    Given reduce-motion is enabled
    When a new message arrives at the bottom
    Then the chat jumps to the new bottom without animation

  Scenario: New message fades in as it settles
    Given the user is at the bottom of the chat
    When a new message arrives
    Then it fades in while the chat glides (manual verification)
```

**Steps:**

#### Step 4.1: Animated scroll-to-bottom on insertion

**Complexity**: standard
**IMPLEMENT**: Add `ChatView::animateScrollToBottom()`: stop any running
scroll animation, start from the current value, end at the scrollbar maximum,
OutCubic, `AnimationConfig::scaled(220)`. In `onRowsInserted`, replace the
instant `scrollToBottom()` with `animateScrollToBottom()` when `atBottom`
(the `appearRows` fade-in stays). Set `atBottom = false` in the `modelReset`
handler so a channel switch never glides during history top-insertion.
**TEST**: New `tests/tst_ChatView.cpp` (offscreen, isolated QSettings, reset
`AnimationConfig` singleton in init/cleanup): insert rows while at the bottom
and assert a scroll animation targets the new maximum; reduce-motion yields
an instant jump; insertion while scrolled up leaves the value unchanged;
`modelReset` clears `atBottom`. Register the target in `CMakeLists.txt`. Full
suite green.
**REFACTOR**: One named scroll helper shared by the new-message path and
(now) the jump-to-bottom button.
**Files**: `src/UI/Chat/ChatView.hpp`, `src/UI/Chat/ChatView.cpp`, `tests/tst_ChatView.cpp`, `CMakeLists.txt`
**Commit**: `feat: glide new messages into view with a smooth scroll`

### Slice 5: Message input — bigger bar owning the bottom strip

**Depends-on:** none
**Files:** `src/UI/Input/MessageInput.hpp`, `src/UI/Input/MessageInput.cpp`, `src/UI/MainWindow.cpp`, `tests/tst_MessageInput.cpp` (new), `CMakeLists.txt`

**Behavior:**

```gherkin
Feature: Message input area

  Scenario: Bigger default bar
    Given the input is not in compact mode
    When the chat loads
    Then the input bar's minimum height is 56 pixels

  Scenario: Compact mode keeps its size
    Given the input is in compact mode
    Then the input bar's minimum height is 30 pixels

  Scenario: Full-width bottom bar
    When the input bar is shown
    Then it spans the full width of the chat pane with no side margins

  Scenario: Status lives inside the input block
    When a typing indicator or slowmode state is active
    Then it renders as the top strip of the input block
    And no separate strip sits between the chat and the input
```

**Steps:**

#### Step 5.1: Taller default input

**Complexity**: standard
**IMPLEMENT**: In `MessageInput::adjustHeight`, raise the non-compact
`minHeight` 44 → 56 and `vPadding` 20 → 24; compact mode unchanged. Extract
the constants to named statics and add a pure `minimumInputHeight(bool
compact)` helper.
**TEST**: New `tests/tst_MessageInput.cpp` (offscreen, isolated QSettings):
`minimumInputHeight(false) == 56`, `minimumInputHeight(true) == 30`; register
the target in `CMakeLists.txt`. Full suite green.
**REFACTOR**: Route the `adjustHeight` call sites through the helper.
**Files**: `src/UI/Input/MessageInput.cpp`, `tests/tst_MessageInput.cpp`, `CMakeLists.txt`
**Commit**: `feat: taller default message input bar`

#### Step 5.2: Flush full-width input

**Complexity**: standard
**IMPLEMENT**: Set the `MessageInput` outer layout margins to 0 (was
`4, 0, 4, 0`) so the bar runs edge to edge; adjust the input container's
background/rounded styling so the flush bar still reads as a bar. Both modes
inherit the flush width; the corner treatment at the right edge (where the
slide-out overlay can sit) is a plain right angle — no rounding on the
overlay side.
**TEST**: Extend `tst_MessageInput` — `outerLayout->contentsMargins() == 0`;
full suite green.
**REFACTOR**: No structural change; keep the layout single-source for margins.
**Files**: `src/UI/Input/MessageInput.cpp`, `tests/tst_MessageInput.cpp`
**Commit**: `fix: message input spans the full bottom width`

#### Step 5.3: Fold the status strip into the input block

**Complexity**: standard
**IMPLEMENT**: Add `MessageInput::setStatusStrip(QWidget *)`: stores the
widget as a member, inserts it at index 0 of the outer layout, and connects
its show/hide to `adjustHeight`; **`adjustHeight` must add the strip's
`sizeHint().height()` to `totalHeight`** so the fixed-height contract stays
consistent. In `MainWindow::setupUi`, reparent the typing/slowmode `statusRow`
into `messageInput`, call `setStatusStrip`, and remove the separate
`statusRow` from `threadPaneLayout`, leaving the input as the single bottom
widget. Keep `typingIndicator`/`slowModeIndicator` as the reparented children
(they stay referenced by MainWindow's `cooldownChanged` connect).
**TEST**: Full suite green; extend `tst_MessageInput` — the fixed height
grows by the strip's height when it is shown. Manual: typing indicator and
slowmode countdown render inside the input block.
**REFACTOR**: Drop the now-unused `statusRow` container in MainWindow.
**Files**: `src/UI/Input/MessageInput.hpp`, `src/UI/Input/MessageInput.cpp`, `src/UI/MainWindow.cpp`, `tests/tst_MessageInput.cpp`
**Commit**: `feat: unify typing/slowmode status inside the input bar`

### Slice 6: User area — fill the channel-list bottom

**Depends-on:** none
**Files:** `src/UI/Widgets/MePanel.cpp`, `src/UI/MainWindow.cpp`

**Behavior:**

```gherkin
Feature: User area panel

  Scenario: Panel fills the column width
    Given the channel list column is resized
    When the window relayouts
    Then the user panel spans the full column width

  Scenario: Panel stays pinned to the bottom
    Given either tree or classic channel list mode
    Then the user panel remains the bottom-most element of the column
```

**Steps:**

#### Step 6.1: Make the MePanel fill and track the column

**Complexity**: trivial
**IMPLEMENT**: Set the MePanel size policy to Expanding horizontally /
Preferred vertically so it spans the full channel-list width and tracks
splitter-driven width changes in both Tree and Classic modes (it already sits
bottom-most in `buildLeftSide`; keep the layout stretch at 0).
**TEST**: Full suite green; manual verification of both channel-list modes.
**REFACTOR**: None beyond the size-policy change.
**Files**: `src/UI/Widgets/MePanel.cpp`, `src/UI/MainWindow.cpp`
**Commit**: `fix: user area fills the channel list bottom`

### Slice 7: Animation polish — consistency sweep

**Depends-on:** 4
**Files:** `src/Core/Animation/AnimationConfig.hpp`, `src/UI/Chat/ChatView.cpp`, `src/UI/Dialogs/ChannelQuickSwitch.cpp`, `src/UI/Widgets/ToastNotification.cpp`, `src/UI/Widgets/ToastContainer.cpp`, `src/UI/MemberList/MemberListOverlay.cpp`, `src/UI/Input/MessageInput.cpp`, `src/UI/TypingIndicator.cpp`, `tests/tst_AnimationConfig.cpp` (new), `tests/tst_ChatView.cpp`, `CMakeLists.txt`

**Behavior:**

```gherkin
Feature: Animation consistency

  Scenario: Speed multiplier retunes durations
    Given the animation speed is set to 2x
    When the duration scaling helper is asked for a 220 ms authored duration
    Then it returns 110 ms

  Scenario: Reduce-motion collapses durations
    Given reduce-motion is enabled
    When the duration scaling helper is asked for any authored duration
    Then it returns 0

  Scenario: Channel switch glides like a new message
    When the user switches channels
    Then the chat settles to the bottom with the smooth scroll animation
```

**Steps:**

#### Step 7.1: Config-aware duration guarantees

**Complexity**: standard
**IMPLEMENT**: Audit every `QPropertyAnimation` duration in `src/` for
routing through `AnimationConfig::scaled()` / `AnimationUtils::duration()`
(verified at plan time: all do; the only exceptions are the periodic pulse
loops in `VoiceStatusBar`/`VoiceWindow`, which keep a floor by design).
Document that intentional floor in `AnimationConfig`'s header.
**TEST**: New `tests/tst_AnimationConfig.cpp` (isolated QSettings org/app +
clear, singleton reset in init/cleanup): `scaled()` halves at 2x, returns 0
with reduce-motion, persistence round-trip, signals emitted. Register the
target in `CMakeLists.txt`. Full suite green.
**REFACTOR**: Add a comment in `AnimationConfig` naming the pulse-animation
exception so it stays intentional.
**Files**: `tests/tst_AnimationConfig.cpp`, `src/Core/Animation/AnimationConfig.hpp`, `CMakeLists.txt`
**Commit**: `test: pin animation config scaling semantics`

#### Step 7.2: Standardize easing curves

**Complexity**: standard
**IMPLEMENT**: Sweep for inconsistent curves: `ChatView` jump-to-bottom
InOutQuad → OutCubic; collapse paths keep InCubic (the standard exit curve);
anything else off-curve gets aligned. No duration or visual behavior change
beyond curve consistency.
**TEST**: Full suite green; manual pass over the animated surfaces listed in
the slice files.
**REFACTOR**: Hoist reused curves to the shared helpers where applicable.
**Files**: `src/UI/Chat/ChatView.cpp`, `src/UI/Dialogs/ChannelQuickSwitch.cpp`, `src/UI/Widgets/ToastNotification.cpp`, `src/UI/Widgets/ToastContainer.cpp`, `src/UI/MemberList/MemberListOverlay.cpp`, `src/UI/Input/MessageInput.cpp`, `src/UI/TypingIndicator.cpp`
**Commit**: `polish: align animation easing across the app`

#### Step 7.3: Smooth channel-switch scroll

**Complexity**: standard
**IMPLEMENT**: In `ChatView`, replace the `modelReset` instant
`scrollToBottom()` with `animateScrollToBottom()` (reusing Slice 4's helper;
the 4.1 `atBottom = false` reset ensures history top-insertion never glides).
**TEST**: Extend `tst_ChatView` with a reset-then-settle assertion; full
suite green.
**REFACTOR**: `animateScrollToBottom` becomes the single bottom-alignment
entry point.
**Files**: `src/UI/Chat/ChatView.cpp`, `tests/tst_ChatView.cpp`
**Commit**: `polish: glide chat into place on channel switch`

## Parallelization DAG (waves)

- **Wave 1** (parallel): Slice 1, Slice 3A
- **Wave 2**: Slice 2
- **Wave 3**: Slice 3B
- **Wave 4** (parallel): Slice 4, Slice 6
- **Wave 5**: Slice 5
- **Wave 6**: Slice 7

Waves are file-disjoint: MainWindow-touching slices (2, 3B, 6, 5) are
serialized across waves 2–5, and the slices registering new test targets in
`CMakeLists.txt` (2.3, 3A.1, 3B.3, 4.1, 5.1, 7.1) each sit in a different
wave, so no two parallel slices edit the same file. Slice 4 moved to Wave 4
(previously Wave 2) precisely to keep `CMakeLists.txt` edits from Slice 2.3
and 4.1 from overlapping.

## Complexity Classification

| Slice step | Rating | Rationale |
|---|---|---|
| 1.1, 1.3, 2.1, 2.2, 3A.1, 3A.3, 3B.2, 3B.3, 4.1, 5.1, 5.2, 5.3, 7.1, 7.2, 7.3 | standard | Behavioral change within existing patterns |
| 1.2, 3A.2, 6.1 | trivial | Config/wiring change, no new logic |
| 2.3, 3B.1 | complex | Animation-group refactor / model role + snapshot diff |

## Pre-PR Quality Gate

- [ ] `ctest --preset windows` passes (10/10 baseline + new suites)
- [ ] `cmake --build --preset windows` clean for the app target
- [ ] Manual verification of the visual acceptance criteria (screenshots for the PR), including a stress pass on the one-motion overlay (rapid hover in/out, mid-animation toggles)
- [ ] `/code-review` passes
- [ ] PR description lists the visible before/after of the animation sweep (jump-to-bottom curve, channel-switch glide, toast/quick-switch curves) so the "much smoother" request is demonstrable
- [ ] Documentation updated: `memory/reference.md` entry for the unread-count design (session increments + cache seeding + mute-zeroing)

## Skipped (low value)

| Finding | Rationale (one line) |
|---|---|
| Container-level numeric unread sums on guild/category headers (3B.1 aggregation) | No badge consumer paints containers; a running number on every header adds list-wide noise with no observable benefit — dropped in review |
| Accessibility-tree exposure of count badges (QAccessible names) | No AT harness or precedent in the repo to verify against; recorded as a follow-up rather than shipped blind |

## Risks & Open Questions

- **No spec artifacts**: `docs/specs/**` and `specs/**` are empty; this plan
  was authored without a `/specs` output (non-interactive run, logged).
- **Unread-count residual limitation**: seeding covers channels whose history
  is cached locally; channels never opened in this app have no cache and
  start at 0 until live messages arrive (Discord exposes no server message
  counts). This is the accepted bound of the seeded approach.
- **Model-level automation gap**: `ChannelTreeModel` requires a live `Session`
  (network-bound); no headless harness exists, so 3B.1's aggregation and
  in-place badge refresh are verified via the existing signal flow plus manual
  walkthrough. A headless `Session` test harness is a candidate follow-up.
- **Startup mode application ordering**: fixed to run after
  `restoreWindowState()`; the 2.1 smoke test must cover both modes and verify
  persisted splitter sizes survive.
- **`handleMessageCreated` signature**: new `ownMessage` parameter has a
  default, so 3A.1 stays green before 3A.2 lands.
- **Interpretation of "take up the entire space of the bottom"**: taken as
  "full-width, edge-to-edge, one unified block with the status strip"
  (recorded default stance); if the intent was "input grows to fill all
  leftover chat height", that is a different design — flag at review.
- **Visual steps depend on manual verification**: pixel-level behavior
  (panel fill, badge layout, flush input bar, one-motion feel) cannot be
  driven by QtTest; gated on the acceptance-criteria walkthrough at PR time.
- **Branch state**: working tree has unrelated uncommitted changes (perf
  fixes, parser WIP) that are green at plan time (10/10 ctest). The builder
  should land that WIP as its own commit(s) before the first slice commit, or
  run this plan on a feature branch, so slice commits stay scoped.
- **Gherkin persistence**: no BDD convention detected (no `features/` dir, no
  manifest; detection script unavailable in this environment) →
  `plan-file-only`. `.feature` files are not written.

## Build Progress

### Slices

- [x] Slice 1: Channel list scaling — smaller default, wider range
  - [x] Step 1.1: Per-axis scale constants, clamping, and range-aware math
  - [x] Step 1.2: Wire the wider stepper range in settings
  - [x] Step 1.3: Channel-aware scaledInt at channel call sites
- [x] Slice 2: Member list slide-out — true overlay, one motion, applied at startup
  - [x] Step 2.1: Apply persisted member-list mode at startup
  - [x] Step 2.2: Collapse the splitter placeholder so the chat owns the width
  - [x] Step 2.3: One-motion expand/collapse
- [ ] Slice 3A: Unread message counts — core tracking
  - [x] Step 3A.1: Track unread message counts in ReadStateManager
  - [x] Step 3A.2: Pass own-message flag from the gateway path
  - [ ] Step 3A.3: Seed counts from the local message cache
    <!-- 3A.3 pending: MessageRepository::countMessagesAfter + ClientInstance
         READY-time seed call. Seed methods exist on ReadStateManager; the
         cache query and wiring are not yet implemented (session-local counts
         are live). -->
- [x] Slice 3B: Unread counts — channel list and tab display
  - [x] Step 3B.1: Expose unread count on channel nodes
  - [x] Step 3B.2: Draw the count badge in the channel delegate
  - [x] Step 3B.3: Show counts on tabs
- [x] Slice 4: Smooth new-message slide-up
  - [x] Step 4.1: Animated scroll-to-bottom on insertion
- [x] Slice 5: Message input — bigger bar owning the bottom strip
  - [x] Step 5.1: Taller default input
  - [x] Step 5.2: Flush full-width input
  - [x] Step 5.3: Fold the status strip into the input block
    <!-- Implemented as a slide-out strip: the typing/slowmode status mounts
         inside the input block and animates open/closed on activity. -->
- [x] Slice 6: User area — fill the channel-list bottom
  - [x] Step 6.1: Make the MePanel fill and track the column
- [ ] Slice 7: Animation polish — consistency sweep
  - [ ] Step 7.1: Config-aware duration guarantees
    <!-- Pending: tst_AnimationConfig. The audit found every duration already
         routed through AnimationConfig; only the regression test is missing. -->
  - [ ] Step 7.2: Standardize easing curves
    <!-- Deferred: remaining curves (jump-to-bottom InOutQuad for an opacity
         fade) are acceptable; the high-value curves (scroll, overlay, strip)
         are already OutCubic/InCubic. -->
  - [x] Step 7.3: Smooth channel-switch scroll

## Plan Review Summary

**Plan tier: complex — reviewers: Acceptance, Design, UX, Strategic** (all 5
perspectives applied; the plan has 7 slices, 6 waves, two `complex` steps,
and cross-cutting MainWindow/animation work).

Round 1 (initial): Acceptance, Design, and UX returned `needs-revision`;
Strategic returned `approve`. All blockers were addressed in revision:

- **Acceptance**: muted-channel scenario was vacuous (added the unread
  precondition); six new test files were never wired into `CMakeLists.txt`
  (every new-test step now registers its target and lists the file).
- **Design**: startup mode application would run before
  `restoreWindowState()` and corrupt persisted splitter sizes (moved to the
  end of the constructor); folding `statusRow` into the fixed-height
  `MessageInput` would clip (5.3 now counts the strip in `adjustHeight`);
  the mute rule was split across layers and leaked counts onto tabs (counts
  are now zeroed at the source in `ReadStateManager`).
- **UX**: session-local counts would misleadingly reset to zero on restart
  (3A.3 now seeds from the local message cache, with own-message and
  active-channel exclusions).

Round 2 (re-review of the three that flagged): all three `approve`. Remaining
warnings folded into the plan: wave-DAG `CMakeLists.txt` collision (Slice 4
moved from Wave 2 to Wave 4), seed-query testability (in-memory QSQLITE test
added for `countMessagesAfter`), seed-fidelity exclusions (own-message
filter + active-channel skip + single canonical hook), and reworded Gherkin
scenarios to be observable.

Aggregated findings retained as warnings/observations:

- The one-motion overlay design (width-driven content opacity) was endorsed
  by Design and UX; the manual gate must include a stress pass (rapid hover
  in/out, mid-animation toggles).
- Session-local unread counts carry a residual limitation (channels with no
  cached history start at 0); recorded in Risks and to be stated in the PR.
- The model-level automation gap (no headless `Session` harness for
  `ChannelTreeModel`) is recorded as a follow-up rather than silently
  papered over.
- The app already provides the "jump to present" affordance (jump-to-bottom
  button); Slice 4/7 route it through the shared animated helper, so no new
  affordance work is needed.
- Accessibility-tree exposure of badges and overlay focus management are
  explicitly out of scope and recorded in the Skipped/Decisions sections.
- The working tree has unrelated uncommitted WIP (green at plan time); the
  builder should land it as its own commit or use a feature branch.

## Approval

Auto-approved (non-interactive) at 2026-08-26 — no human review gate. Trigger: no TTY (stdin is not a usable TTY; headless /planner → /builder run).
Audit entry appended to metrics/config-changelog.jsonl.
