# AGENTS.md

AltTab is a fork-continued, single-project Win32 C++ app — raw Win32 message loop with `CommCtrl` (no Qt/MFC/WTL). Windows-only; built with Visual Studio 2022 (v143 toolset, Windows 10 SDK). All UI strings and most buffers use wide/TCHAR strings.

## Build
- `compile.cmd` (repo root) is the easy way to rebuild: `compile.cmd` → `Release|x64`, `compile.cmd Debug`, `compile.cmd ReleaseNoLogger`, `compile.cmd all`. It locates MSBuild automatically (vswhere, VS2022/VS18 fallbacks), prefers the amd64-hosted MSBuild, and auto-selects the newest installed C++ toolset — so it also works on machines where the project default `v143` isn't installed.
- Solution `AltTab.sln` contains exactly one project (`AltTab`). Build:
  `msbuild /m /p:Configuration=<cfg> /p:Platform=x64 AltTab.sln`
  CI (`msbuild.yml`) runs `nuget restore AltTab.sln` first, then builds `Release|x64` (with its default `v143` toolset).
- Configurations: `Debug` (opens a console for logging), `Release`, `ReleaseNoLogger`. Output: `x64\<Configuration>\AltTab.exe`; intermediates under `IntermediateFiles\` (both gitignored). x86 (`Win32`) configs exist but CI never builds them.
- No unit tests. `AltTabTester\` is a standalone manual-testing harness and is NOT part of the solution — building the sln does not build it.

## Source layout (`source/`)
The code is split into focused modules (each has a matching `.h`):
- `AltTab.cpp/.h` — `wWinMain`, keyboard hook, tray icon + main window. Cross-module globals live in `GlobalData.h`.
- `AltTabWindow.cpp/.h` — AltTab window class, lifecycle/`WM_*` handlers, window activation, About box.
- `AltTabListView.cpp/.h` — search-string edit + ListView creation, refresh, selection, owner-draw, fonts, key handling.
- `AltTabContextMenu.cpp/.h` — tray + list-item context menus (single handlers).
- `AltTabTooltip.cpp/.h` — custom ToolTip control (create/show/hide).
- `ProcessExplorer.cpp/.h` — window/process enumeration + PE metadata (icons, title, version).
- `AutoStart.cpp/.h` — registry "Run" key + Task Scheduler logic.
- `AltTabSettings.cpp/.h` — settings struct, `AltTabSettings.ini` read/write, similar-process groups.
- `SettingsDialog.cpp/.h` — settings dialog UI.
- `Utils.cpp/.h` — string/window helpers, COM/GDI+ setup, elevation, restart.
- `CheckForUpdates.cpp` — update check + dialog; `Logger.cpp`, `fuzzywuzzy.cpp` — unchanged helpers.
- `GlobalData.cpp` no longer exists (its globals were redistributed into the modules above).
- Add any new `.cpp`/`.h` to both `ClCompile`/`ClInclude` in `AltTab.vcxproj`.

## Logging (`_AT_LOGGER`)
- `AT_LOG_*` macros (`source/Logger.h`) are compiled in only when `_AT_LOGGER` is defined (Debug and Release configs); in `ReleaseNoLogger` they compile to no-ops. Code that logs must remain valid when log4cpp isn't available.
- log4cpp is vendored prebuilt under `libraries\log4cpp\` (`include\`, plus `lib\<Debug|Release>\log4cpp.lib`), linked per-config in `AltTab.vcxproj`. `lib\ReleaseWithLog\` is orphaned — no config references it.
- At runtime `CreateLogger()` (`source/Logger.cpp`) hardcodes appenders; the committed root `log4cpp-config.properties` is NOT loaded yet (open task in `Tasks.md`). Release logs to `%LOCALAPPDATA%\AltTab\AltTab.log`; Debug logs to a console via `EnableConsoleWindow()`.

## Versioning — `source/version.h` is GENERATED
- Do not edit `source/version.h` by hand. Source of truth is `Scripts\VersionInfo.txt` (`MAJOR.MINOR.PATCH`); regenerate via `python Scripts\CreateVersionH.py` (fills `Scripts\version.h.template`).
- `VersionHelper.read_version_info()` returns `VersionInfo.txt` verbatim. Set the full `MAJOR.MINOR.PATCH` there; bump what you want.
- Version text is also mirrored to root `version.txt` and `ReleaseNotes.txt` (check-for-updates / changelog); keep them in sync.
- Packaging: `python Scripts\CreatePackages.py` builds via root `compile.cmd` (so MSBuild/toolset are auto-detected, no hardcoded VS paths) and zips the redistributables (exe + `AltTab.chm`, `ReadMe.txt`, `ReleaseNotes.txt`) with Python's stdlib. Outputs `Releases\AltTab_<ver>_<config>_x64.zip`; `ReleaseNoLogger` also gets an extra `AltTab_<ver>_x64.zip` copy. Use `--skip-build` to only package existing binaries. It refuses to package unless `Scripts\VersionInfo.txt` is `N.N.N` (the update checker's release-tag format).

## Release checklist
1. Bump `Scripts\VersionInfo.txt` (source of truth for `MAJOR.MINOR.PATCH`); nothing writes it automatically.
2. `python Scripts\CreateVersionH.py` regenerates `source/version.h`.
3. Manually update root `version.txt` and append to `ReleaseNotes.txt` — no script does this (easy to forget; keep them in sync).
4. `python Scripts\CreatePackages.py` builds + zips (verifies the tag format itself).
5. Commit, then `git tag <N.N.N>` matching `VersionInfo.txt` exactly, and push the tag.
6. Create a GitHub release at `github.com/kerryland/AltTab/releases` on that tag, upload the zips, and write the changelog — its **body** is what AltTab shows in the update dialog, and the **tag name** is what the in-app update check compares against. Wrong tag shape ⇒ update check silently finds nothing.

## Conventions & gotchas
- Format with clang-format (`.clang-format`): 4-space indent, 120-col limit, compile-time-standard C++11, `PointerAlignment: Left`, braces on the same line, no tabs. Run `clang-format -i` on edited files.
- Runtime config `AltTabSettings.ini` is read/written next to the exe (`ATSettingsFilePath` in `source/AltTabSettings.cpp`) and only reloaded on demand (tray menu "Reload AltTabSettings.ini" or Settings dialog). The root `AltTabSettings.ini` is the in-repo default/sample.
- Every request AltTab makes of another window/process must stay time-limited — use `SendMessageTimeout` + `SMTO_ABORTIFHUNG` (see `kWindowReplyTimeoutMs` in `ProcessExplorer.cpp` for enumeration, `kCloseReplyTimeoutMs` in `AltTabListView.cpp` for close). Plain `SendMessage`/`GetWindowText` to foreign windows blocks the UI/hook thread and lets Windows' own switcher win; don't "simplify" these back.
- Search string filtering matches window title, process name, **and** the exe's file description (`Item.Description`, from the version resource). The search string is cleared each time the AltTab window closes (in `AltTabListViewShutdown`), so Alt+Tab always starts fresh.
- Known platform limitation (do not try to fix in-process): hotkeys don't work in elevated applications unless AltTab itself also runs elevated (documented in `ReadMe.txt` "Known Limitations").