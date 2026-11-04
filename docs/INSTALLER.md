# Windows setup wizard

## User experience

The release output is one executable: `AuroraWallpaper-Setup-0.1.1-x64.exe`.
Open it, choose the install location/shortcut options, install, and launch Aurora.
The user does not install Visual Studio or Qt, run a PowerShell script, or compile
source code. The application is installed locally and runs as an ordinary desktop
program.

The installer includes:

- The compiled app, Qt libraries/plugins, libVLC and its plugins, app-local MSVC
  runtime DLLs, and the sample wallpapers.
- Microsoft's signed offline WebView2 Runtime installer, executed automatically
  when the required runtime is missing.
- Start menu and optional desktop shortcuts.
- Optional start-at-sign-in, unchecked by default; the `--tray` app switch keeps
  that startup unobtrusive.
- A Windows uninstaller. Imported wallpapers and user preferences are retained.

Aurora installs per user under `%LOCALAPPDATA%\Programs\AuroraWallpaper` by
default. It does not ask for elevation. The WebView2 runtime is a shared Microsoft
component and is not removed when Aurora is uninstalled. Managed Windows policies
can still prohibit installing the required runtime; in that case the wizard
reports the failure instead of claiming success.

## Implementation

| File | Purpose |
| --- | --- |
| `installer/AuroraWallpaper.iss` | Inno Setup wizard, prerequisites, shortcuts, startup option, uninstall registration |
| `tools/Build-Installer.ps1` | Validate staged payload, verify Microsoft signature, compile installer, emit SHA-256 and receipt |
| `tools/Test-Installer.ps1` | Disposable Windows runner install/startup/uninstall smoke test with logs |
| `.github/workflows/build.yml` | Windows compilation, staging, installer generation, smoke test, artifact upload |
| `src/app/main.cpp` | Installer mutex, `--tray` startup, test-only `--smoke-test` |

The CMake install step preserves `bin/`, `plugins/`, and `qt.conf` relative paths
and adds app-local C++ runtime DLLs. The Inno script refuses to compile without the
actual application executable, C++ runtime, and offline WebView2 payload. It is not
a downloader wrapped around uncompiled source.

## Release pipeline

The GitHub Actions workflow in this repository builds on Windows Server 2022,
executes the portable policy suite, compiles the setup wizard, and verifies
installation, dependency loading, hidden application startup, and uninstallation.
Only successful builds on `main` publish the versioned preview release. The release
includes the installer, SHA-256 checksum, and dependency build receipt.

The installer and app are currently unsigned. Full Explorer/GPU testing is separate
from the automated startup check; see [VALIDATION.md](VALIDATION.md).

## Technical references

- [WebView2 offline distribution](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution)
- [Inno Setup per-user mode](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm)
- [CMake app-local runtime installation](https://cmake.org/cmake/help/latest/module/InstallRequiredSystemLibraries.html)
