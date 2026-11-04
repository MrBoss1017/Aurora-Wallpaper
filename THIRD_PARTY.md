# Third-party notices and source access

Aurora's MIT license covers its original application code and sample wallpapers. Dependencies retain their own copyright and license terms.

| Component | Version | License / source |
| --- | --- | --- |
| Qt Core, Gui, Widgets, Concurrent and deployed Qt plugins | 6.8.3 | LGPLv3 / applicable module and bundled-library licenses; https://github.com/qt/qtbase/tree/v6.8.3 |
| Qt SVG, image formats and translations | 6.8.3 | Module-specific licenses; https://github.com/qt/qtsvg/tree/v6.8.3 and https://github.com/qt/qtimageformats/tree/v6.8.3 |
| libVLC and VLC codec/output plugins | 3.0.23 x64 | libVLC LGPLv2.1-or-later; VLC/plugin licenses include GPL and third-party terms. https://www.videolan.org/legal.html |
| Microsoft WebView2 SDK | 1.0.4314.35 | Microsoft package license, copied into installed licenses/WebView2 |
| Microsoft WebView2 Runtime | Evergreen, version in build receipt | Microsoft runtime terms; https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution |
| Microsoft Visual C++ runtime | Build toolchain version | Microsoft redistributable runtime terms |

The installer contains vendor license texts under `licenses/`, plus VLC's original notices under `bin/vlc/`. Aurora dynamically links Qt and loads libVLC; those DLLs can be replaced with compatible modified versions. No Aurora license restriction prohibits debugging or reverse engineering modifications to LGPL libraries. Aurora's source and build scripts are available in this repository.

## Corresponding upstream source

The same GitHub release download page supplies the exact-version Qt module archives and the VLC 3.0.23 source archive separately from the installer, with SHA-256 checksums. You do not need these archives to install or use Aurora.

- Qt module source and licenses: `qtbase-6.8.3.zip`, `qtsvg-6.8.3.zip`, `qtimageformats-6.8.3.zip`, `qttranslations-6.8.3.zip`.
- VLC source: `vlc-3.0.23.tar.xz`, also available from https://download.videolan.org/pub/videolan/vlc/3.0.23/vlc-3.0.23.tar.xz .
- VLC dependency source download URLs, patches, and build recipes are in that archive's `contrib/src/`; Windows build instructions are under `extras/package/win32/`.
- Official VLC dependency archives: https://download.videolan.org/pub/contrib/ .
- Qt Windows build configuration is documented at https://doc.qt.io/qt-6.8/windows-building.html .

Qt and VLC are unmodified vendor binaries in this distribution. Codec support and redistribution conditions are component-specific; Aurora's MIT license does not replace them. Microsoft components are redistributable vendor binaries, not MIT-licensed source.
