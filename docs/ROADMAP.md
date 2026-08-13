# Acheron — Roadmap

**Updated**: 2026-08-12 · **Author**: build orchestrator

---

## Status

The tree is repaired and green. Baseline commit `738558c` ("reconcile uncommitted
delta + untrack scratch") landed the tree repair (merge corruption resolved,
uncommitted delta reconciled, EOL renormalized, `.env` hygiene applied), and units
U1–U10 committed on top of it. All 6 test suites plus the RuntimeSmoke check pass,
and the deploy directory is populated.

---

## Completed (Run 3)

| Unit | Commit   | Delivered |
|------|----------|-----------|
| U1   | `738558c` | Green baseline: reconciled uncommitted delta, EOL renormalization, `.env` hygiene, full compile + 6 suites + RuntimeSmoke. |
| U2   | `aecfd67` | Virtualized emoji picker grid (fixed ~200-widget pool + scroll recycling, ≤8 concurrent network fetches). |
| U3   | `f827f07` | Push-to-talk wiring through SettingsWindow/MainWindow into the voice manager. |
| U4   | `8a94be9` | Pinned messages panel (list, unpin action, live refresh). |
| U5   | `3bcd275` | Attachment gallery (image preview, zoom, save-as, copy link). |
| U6   | `6140d45` | Message hover reaction bar (quick emoji + shared picker). |
| U7   | `22a9c76` | Chat channel crossfade transition. |
| U8   | `87dbf03` | Code-block syntax highlighting (cpp/python/js/json). |
| U9   | `c683fec` | Do-Not-Disturb-aware notifications (DND suppresses sound/notification noise). |
| U10  | `8fa5a5e` | MainWindow monolith refactor into focused controllers. |

---

## Remaining work / non-goals

Explicitly out of scope for this run (not implemented — do not claim these):

- Screen share / streaming
- Plugin system
- Custom CSS themes
- Split view
- Message translation
- Spoiler reveal animation
- Custom guild emoji in the shared picker
- Linux/macOS/Qt5 support — Windows-only + Qt6-only is the confirmed direction

---

## Direction

- **Windows-only, Qt6-only** (Qt 6.10.3, MSVC 2022 x64).
- vcpkg + curl-impersonate; SQLite storage.
- One supported build path: `build.ps1` (configure / build / test / deploy).

---

## Verification checklist

- [x] `build.ps1` (configure + build) succeeds.
- [x] All 6 test suites pass: `TestMarkdown`, `TestDeserialization`, `TestEmojiCatalog`, `TestLinkification`, `TestProtoReader`, `TestEmojiSegmenter`.
- [x] `windows-runtime-smoke.ps1` passes against `build\deploy\RelWithDebInfo`.
- [x] Deploy directory populated (`acheron.exe` + Qt plugins).
- [x] No `.env` tracked; no secrets in the tree.
- [x] `git status` clean of stray/source changes at each unit boundary.
