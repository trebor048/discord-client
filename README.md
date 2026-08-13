### Acheron
---
Alternative Discord client made in C++ with Qt 6

<img width="1528" height="864" alt="acheron_vofHKu0r4B" src="https://github.com/user-attachments/assets/f2a1bce5-4170-4207-86ce-3b35974f0f1b" />

<a href="https://discord.gg/wkCU3vuzG5"><img src="https://discord.com/api/guilds/858156817711890443/widget.png?style=shield"></a>

Current features:
* Not Electron
* No, not Tauri either
* Windows support (x64)
* Voice support (E2EE & noise suppression)
* Push-to-talk
* Per-user voice volumes
* Multi-account support
* Browser impersonation to avoid spam filter
* Per-channel tabs
* Discord-compatible markdown parsing
* Code-block syntax highlighting
* Link auto-detection
* Embed support
* Sticker support
* File upload support
* Attachment gallery
* Unread and mention indicators
* Date separators
* Do-Not-Disturb-aware notifications
* Notification sounds
* System tray
* Guild folders and per-folder colors
* Channel-list drag-and-drop reorder
* Edit, delete, pin, reply, react
* Pinned messages panel
* Quick reactions on hover
* Emoji support
* Custom status
* Friends
* Image viewer
* Typing indicators
* DMs and group DMs
* Threads
* Forums
* Settings pages
* QR code login

Planned features:
* Screen sharing / streaming
* Plugin system
* Custom CSS themes
* Split view
* Message translation
* Spoiler reveal animation
* Custom guild emoji in the shared picker

### Downloads:

Latest nightly Windows build: https://nightly.link/ouwou/acheron/workflows/build/main/acheron-windows.zip

### Dependencies:

* Qt 6.10+
* libcurl-impersonate (technically just libcurl is supported but you should use libcurl-impersonate)
* OpenSSL
* nayuki/QR-Code-generator (vendored)
* zlib (either via Qt ZlibPrivate or system)
* QtKeychain
* emoji-segmenter (vendored)
* libsodium (optional, voice support)
* libopus (optional, voice support)
* libdave (optional, voice support, vendored)
* mlspp (optional, voice support, vendored)
* miniaudio (optional, voice support, vendored)
* rnnoise (optional, noise suppression, vendored)

### Windows build instructions

The supported build is Windows x64 with Visual Studio 2022 (the Desktop
development with C++ workload and a Windows SDK) and Qt 6 for MSVC 2022 x64
(including the Multimedia module and windeployqt). From a PowerShell prompt at
the repository root, run:

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

That configures, builds, runs the test suite, and deploys the application. The
wrapper locates Visual Studio automatically, finds a complete Qt 6 MSVC 2022
kit (under `C:\Qt`, via `Qt6_DIR`, or in the `tools\.cache\Qt` download cache),
bootstraps a shared vcpkg checkout if needed (`VCPKG_ROOT` or `C:\vcpkg`), and
downloads curl-impersonate on first run into the ignored `tools\.cache`
directory. On a machine with Visual Studio, git, and Python 3.7+ but no Qt, it
downloads Qt 6.10.3 into `tools\.cache\Qt` automatically (first run only).

Sub-steps can be run individually:

```powershell
.\build.ps1 -Action configure   # configure only
.\build.ps1 -Action build       # build only (assumes configured)
.\build.ps1 -Action test        # run CTest
.\build.ps1 -Action deploy      # build + run windeployqt
.\build.ps1 -Action clean       # remove the build directory
.\build.ps1 -Clean              # clean, then configure + build + test + deploy
```

The deployable application is written to `build\deploy\RelWithDebInfo`; use
that directory to run or package the app. Validate the deployed runtime without
opening a normal application session:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\windows-runtime-smoke.ps1 `
  -DeploymentDirectory .\build\deploy\RelWithDebInfo
```

Troubleshooting:

* If the wrapper reports a missing prerequisite, install the named Visual Studio
  C++/Windows SDK component, then rerun it. Qt is downloaded automatically when
  no kit is found; set `Qt6_DIR` to pin a specific installed kit.
* If configuration fails after changing compilers, Qt kits, or vcpkg state,
  remove the `build` directory (`.\build.ps1 -Action clean`) and rerun. Do not
  reuse an incompatible CMake cache.
* If the runtime smoke check reports a missing dependency or fails before the
  readiness marker, rerun the wrapper to regenerate the deployment directory.
  Check its reported missing DLL or plugin first; the package must contain the
  deployment output, including `Qt6Multimedia.dll` and the `platforms` plugins,
  not only `acheron.exe`.
