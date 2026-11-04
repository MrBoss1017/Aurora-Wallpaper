# Third-party components

The MIT license covers Aurora's original source and samples only. It does not
relicense Qt, WebView2, VLC, codec plugins, Windows components, or optional test
tools. Vendor binaries are downloaded by the build scripts, not included in this
source archive.

| Component | Official licensing/source information |
| --- | --- |
| Qt | https://www.qt.io/licensing/ and the licenses shipped with the selected Qt distribution |
| libVLC/VLC and plugins | https://www.videolan.org/legal.html and COPYING/AUTHORS files from the exact runtime distribution |
| WebView2 | SDK package license and https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/distribution |
| Windows SDK/runtime | Applicable Microsoft SDK/runtime terms |
| Optional Playwright test runner | https://github.com/microsoft/playwright |

Aurora dynamically loads libVLC and dynamically links Qt. The dependency staging
script preserves the VLC distribution's top-level attribution/license files.
Qt's deployment tool handles runtime files; it does not replace a redistribution
review. Before distributing compiled binaries, inventory the exact Qt modules,
VLC plugins/codecs, licenses, attribution, relinking/source obligations, and
runtime distribution terms. This file is an inventory aid, not a legal opinion.
