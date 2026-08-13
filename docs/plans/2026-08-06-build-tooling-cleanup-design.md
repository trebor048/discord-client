# Build Tooling Cleanup - Design

Date: 2026-08-06
Status: Approved

## Goal

Clean up the Acheron repo root and rebuild the build tooling/pipeline to be
clean and simple: Windows-only, Qt6-only, shared/global vcpkg, a single build
script, a single CMake preset, and a single CI job.

## Decisions

1. **Windows-only.** Drop the Linux, AppImage, macOS, and Qt5 CI jobs.
2. **Qt6-only.** Remove `qt.cmake`, `src/qt5_compat.h`, and all Qt5 fallbacks
   in `CMakeLists.txt`.
3. **Shared/global vcpkg.** Remove the bundled `vendor/vcpkg` submodule
   (2.9GB). Builds resolve a global vcpkg via `VCPKG_ROOT` (or
   `C:\vcpkg`). `vcpkg.json`, `vcpkg-overlay-ports/`, and `vcpkg-triplets/`
   stay and continue to drive the manifest.
4. **curl-impersonate auto-download.** The build script downloads the prebuilt
   release tarball into a cache dir (same URL pattern CI uses) and extracts it.
5. **Single config.** `RelWithDebInfo` everywhere; no MinSizeRel matrix.
6. **Single entry point.** `build.ps1` at repo root with
   `-Action configure|build|test|deploy|all` (default `all`) and `-Clean`.
7. **Secrets hygiene.** `git rm --cached .env` (keeps the file on disk) and
   add `.env` to `.gitignore`.

## Repo root cleanup

Delete (untracked): `EXACT_FIX.md`, `FIX_BUILD.txt`, `QUICK_BUILD.txt`,
`RUN_THIS.txt`, `config_output.log`, `env_check.txt`, `build_output.log`,
`aqtinstall.log`, `diagnose-env.ps1`, `fix-build-env.bat`, `build-clean.bat`,
`build-ninja.bat`, `build.bat`, `build.ps1`, `custom-build2/`, `build-win/`,
`build-ninja/`, `vcpkg_installed/`, `curl-impersonate/`, `vendor/vcpkg/`.

`git rm`: `check-env.ps1`, `simple-build.bat`, `custom-build2/certs/cacert.pem`,
`.env`.

Delete: `BUILD_INSTRUCTIONS.md`, `qt.cmake`, `src/qt5_compat.h`,
`vcpkg.toolchain.cmake`.

Update: `.gitmodules` (remove vcpkg), `.gitignore` (drop vcpkg cache entries,
prune stale `/build*` wildcard to explicit `/build/`, drop deleted script
exceptions, add `.env`).

Keep: `scripts/generate_emoji_catalog.py`, `scripts/windows-runtime-smoke.ps1`,
`cmake/DeployQtRuntime.cmake`, `certs/cacert.pem`, `vcpkg.json`,
`vcpkg-overlay-ports/`, `vcpkg-triplets/`.

## Build tooling

- **CMakeLists.txt**: drop `include(qt.cmake)` and Qt5 detection; drop the
  `/FI qt5_compat.h` flags; keep `USE_VCPKG` but point at the shared toolchain
  `${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake` (fallback
  `C:/vcpkg`); voice deps go through the vcpkg path only (`unofficial-sodium`,
  `Opus::opus`); keep all existing target/source/deploy logic unchanged.
- **CMakePresets.json**: one `windows` configure preset (VS 17 2022, x64,
  binaryDir `build`, `x64-windows-static-md`, `BUILD_TESTS=ON`,
  `ENABLE_RNNOISE=OFF`) + matching build/test presets. Drop ninja preset,
  Qt5 bits, and the `VCPKG_CHAINLOAD_TOOLCHAIN_FILE` hack.
- **build.ps1**: locates VS via `vswhere`, validates `cl/rc/mt`; resolves the
  global vcpkg (bootstrap via `VCPKG_ROOT`/`C:\vcpkg`); locates the Qt6 MSVC
  kit (Multimedia + windeployqt + qwindows/qoffscreen); ensures
  curl-impersonate (download prebuilt tarball); then configures/builds/
  tests/deploys. `-Clean` removes `build/`. Deployable output stays at
  `build/deploy/RelWithDebInfo`.

## CI pipeline

One `build-windows` job replacing the 793-line `build.yml`:
checkout (recursive submodules for vendored libs), install Qt 6.10.3
(qtmultimedia, qtimageformats), clone + bootstrap a global vcpkg into the
runner toolcache, cache `build/vcpkg_installed`, download curl-impersonate,
configure (VS 2022 x64, static-md triplet, Qt6_DIR, CURL_DIR, RelWithDebInfo),
build, ctest, windeployqt, upload one `acheron-windows` artifact.

## Verification

- `python -m json.tool` on `CMakePresets.json` and `vcpkg.json`.
- `actionlint` on `.github/workflows/build.yml`.
- PowerShell parse check on `build.ps1` via `pwsh -NoProfile -Command
  "[System.Management.Automation.PSParser]::Tokenize((Get-Content -Raw build.ps1), [ref]$null)"`.
- `git status` clean of stray files after cleanup.
