# Build, run, and package

## Standalone installation

End users should receive `AuroraWallpaper-Setup-0.1.1-x64.exe`, run the wizard, and launch Aurora from the Start menu. They do not need the developer prerequisites or commands below. The installer bundles the staged Qt/libVLC/MSVC runtime files and Microsoft's offline WebView2 installer, creates shortcuts, optionally starts Aurora at sign-in, and registers a Windows uninstaller.

Tested installer builds are published on [GitHub Releases](https://github.com/MrBoss1017/Aurora-Wallpaper/releases). See [INSTALLER.md](INSTALLER.md) for the pipeline. Download the setup EXE to install; GitHub’s source archives are for developers.

## Prerequisites

Use an x64 Windows machine with Visual Studio 2022's **Desktop development with C++** workload, the MSVC v143 toolset, a Windows SDK, CMake 3.25+, and the NuGet CLI on PATH. Install a Qt 6.8+ **MSVC 2022 64-bit** kit. This project uses Qt Core/Gui/Widgets/Concurrent, not Qt WebEngine.

Install the [WebView2 Evergreen Runtime](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) if absent. It is separate from the C++ SDK. Do not disable Chromium's sandbox or run Aurora as administrator.

The tested-by-design compile target is Windows x64. ARM64 and MinGW targets are not wired into the dependency loader/build files. Windows 11 is the intended desktop target; Windows 10 22H2 is an additional compatibility-test target. API minimum versions do not establish that the undocumented Explorer integration works on every build.

## Build commands

Extract the source ZIP and open PowerShell in the `AuroraWallpaper` folder.

```powershell
.\tools\Setup-Dependencies.ps1
.\tools\Build.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64'
```

Use your installed Qt path. If local PowerShell policy blocks script execution, review the scripts and use your organization's approved way to run local scripts; no system policy change is required by the project itself.

The build helper runs these commands:

```powershell
$env:AURORA_QT_ROOT = 'C:\Qt\6.8.3\msvc2022_64'
cmake --preset windows-release
cmake --build --preset windows-release --parallel
ctest --preset windows-release
cmake --install build/windows --config Release
.\dist\bin\AuroraWallpaper.exe
```

The `dist` folder contains the program, Qt runtime/plugins, libVLC runtime/plugins, samples, and documentation. The program in `build/windows/Release` may still require Qt's DLLs on PATH; running the installed/staged copy avoids that ambiguity. The WebView2 SDK loader is statically linked; the Evergreen browser runtime is external.

To make a distributable runtime ZIP after building:

```powershell
cpack --config build/windows/CPackConfig.cmake -C Release
```

This CPack command stages a portable runtime archive. To produce the setup wizard, the Windows job runs `tools/Build-Installer.ps1` after staging. The wizard embeds WebView2 installation and an optional startup entry. Neither route signs binaries or produces an MSI. Run the validation matrix and review dependency redistribution requirements before distribution. Use your own publisher certificate if signing a release.

## Dependency handling

| Dependency | Build input | Acquisition |
| --- | --- | --- |
| Qt | 6.8+ MSVC x64 kit | Your Qt installation; CI uses 6.8.3 for a reproducible minimum-version build |
| WebView2 SDK | 1.0.4314.35 | Official NuGet package; author/repository signatures verified with the NuGet CLI |
| VLC/libVLC | 3.0.23 x64 | Official VideoLAN ZIP; SHA-256 is pinned in the setup script |
| D3D11, DXGI, DComp, D3DCompiler | Windows SDK/system | Linked Windows import libraries |
| WebView2 Runtime | Evergreen | Install/maintain through Microsoft's runtime distribution |

No third-party source or binary is bundled in the delivered source archive. `third_party/` is generated locally and ignored by git. Downloads are cached in versioned files. Failed checksum checks abort. The script removes/recreates only its project-owned dependency directories.

The VLC SHA-256 is `992d19dbd0b8a7cde9167d2f7780b1ef6f92acc8a71acfa736101a21f35181e1`, as published for the 3.0.23 Windows x64 ZIP. Frozen versions make the build reproducible; they are not a promise of perpetual freshness or security. Upgrade SDK/runtime inputs deliberately, review changes, and rerun Windows validation.

## Using the app

1. Launch the staged executable. The first launch imports Aurora Ribbon and Quiet Orbit.
2. Select a monitor using its name, physical resolution, and refresh rate.
3. Select a wallpaper and choose **Apply wallpaper**. Double-clicking a card also applies it.
4. Use **Import wallpaper** for MP4/WebM, an HLSL file, or a local web folder. Web folders are copied recursively; extract ZIP bundles yourself before selecting the folder.
5. To span, enable **Span all displays** and apply a wallpaper. Independent assignments remain saved.
6. Enable audio or desktop mouse mirroring only when wanted. Mute-all overrides every per-display setting.
7. Close/minimize goes to tray. Tray offers Library, Pause, Next, Mute, Settings, Exit.

On next launch, saved assignments are restored. The installer optionally adds a per-user Windows Run entry when the startup checkbox is selected; it is unchecked by default. Clearing a display removes its assignment, leaving the underlying static Windows desktop wallpaper.

## Local data

Qt resolves the actual per-user app-data path; with the supplied application identity it is normally `%LOCALAPPDATA%\MrBoss\AuroraWallpaper`.

| Item | Contents |
| --- | --- |
| `library.json` | Metadata and relative entries for owned wallpaper copies |
| `settings.json` | Rules, volume, mute, monitor assignments and spanning selection |
| `wallpapers/<uuid>/` | Copied wallpaper files, independent of their original location |
| `WebView2/` | Browser runtime user-data/profile cache |
| `aurora.log`, `aurora.log.old` | Bounded diagnostic logs |
| `instance.lock` | Single-instance guard |

Back up the directory while the app is stopped. Invalid JSON produces a visible error and is preserved rather than silently overwritten. To recover a corrupted configuration, stop Aurora, rename the affected JSON to a backup name, and restart. To remove all local state, stop Aurora and delete its app-data directory manually. Original imported source files are never modified.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| CMake cannot find Qt | QtRoot must be the MSVC 2022 x64 kit with `lib/cmake/Qt6/Qt6Config.cmake` |
| Missing WebView2 headers/libVLC DLL | Run the dependency setup script successfully before configuring |
| Qt platform plugin error | Run `dist/bin/AuroraWallpaper.exe` after the install step, with its adjacent plugins |
| WebView2 environment creation fails | Install/repair Evergreen Runtime; check user-data directory permissions |
| No validated WorkerW | Wait for Explorer, ensure same privilege level, reapply; inspect current Windows build/layout |
| Shader compile error | Entry point must be `PSMain`; no `#include` or external texture resources; see format guide |
| Video has high CPU use | Hardware decode depends on codec, GPU, and driver; test a supported H.264 MP4 |
| Web FPS or volume does not follow settings | Bundle must consume `wallpaperstate` for custom rendering/WebAudio; global mute remains enforced |
| Wallpaper pauses with snapped windows | This is the coverage rule; adjust threshold or choose Keep playing |
| Desktop clicks trigger icons | Mirroring intentionally keeps normal shell interactions intact |
| Web wallpaper stopped after a pause | Browser suspension failed and the instance was disposed; reapply and inspect the local log |
| Black/incorrect placement on mixed DPI | Record Windows build, monitor layout, scales, and renderer; this requires Windows validation |

## Portable checks

Without CMake:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Werror -pedantic -I src tests/policy_tests.cpp -o policy-tests
./policy-tests
node --check samples/web-orbit/scene.js
```

With CMake on Linux/macOS (only the portable test target is enabled):

```sh
cmake --preset portable-tests
cmake --build --preset portable-tests
ctest --preset portable-tests
```

Optional Chromium sample smoke test:

```sh
npm install --no-save playwright
npx playwright install chromium
node tests/web_smoke.cjs
```

The Chromium test checks Canvas rendering, input dispatch, cooperative pause/resume/FPS, resizing, and JavaScript errors. It does not test the native WebView2 controller, shell input routing, video decoding, or HLSL.

## References

- [Qt 6.8 Windows toolchains](https://doc.qt.io/qt-6.8/windows.html)
- [Qt deployment with CMake](https://doc.qt.io/qt-6/qt-generate-deploy-app-script.html)
- [WebView2 SDK package](https://www.nuget.org/packages/Microsoft.Web.WebView2/1.0.4314.35)
- [Official VLC archive and digest](https://get.videolan.org/vlc/3.0.23/win64/vlc-3.0.23-win64.zip)
