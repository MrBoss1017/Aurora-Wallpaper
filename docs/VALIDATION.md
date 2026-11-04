# Validation record and Windows release matrix

Validation is split between portable tests, automated Windows packaging checks, and hands-on desktop testing. The [Windows workflow](https://github.com/MrBoss1017/Aurora-Wallpaper/actions/workflows/build.yml) is the source of truth for each revision. A release is published only when its policy and Windows jobs succeed.

## Checks and evidence

| Check | Result |
| --- | --- |
| GCC C++20 portable policy compile with `-Wall -Wextra -Werror -pedantic` | Passed |
| Portable policy tests | **517 checks passed** |
| AddressSanitizer + UndefinedBehaviorSanitizer portable policy run, leak detection disabled | **517 checks passed**, no reported address/UB errors |
| LeakSanitizer | Unavailable: the executor uses tracing/ptrace, which LeakSanitizer rejects |
| `node --check` on sample JavaScript | Passed |
| JSON, manifest XML, local C++ include paths, local documentation links | Passed |
| Chromium sample smoke test | Not executed: Playwright was installed but no Chromium executable was present |
| Windows C++ compile/link | Required by the Windows CI job before packaging |
| Real Explorer/GPU rendering and multi-monitor matrix | **Not yet executed** on physical Windows desktops |
| GitHub Actions workflow | Active; inspect the run linked to the release commit |
| Offline installer compilation and install/startup/uninstall test | Required by CI; automatic publication is blocked on failure |

The 517 policy checks comprise 500 deterministic randomized comparisons against an independent pixel-grid oracle and 17 focused checks for negative monitor origins, fullscreen monitor separation, overlapping/snapped windows, empty bounds, pause reasons, per-display mute, battery Eco behavior, refresh-rate caps, and coverage thresholds.

A successful CI run establishes compilation and packaging success for that exact revision. It does not establish native rendering correctness on every user desktop. Releases remain previews until the physical desktop matrix is complete.

Installer update: `Build-Installer.ps1` validates the staged files and Microsoft's runtime signature, compiles an Inno Setup wizard, and emits a checksum/receipt. `Test-Installer.ps1` installs into a temporary folder, runs `--smoke-test`, and uninstalls. The application's smoke mode checks Qt loading, WebView2 availability, bundled libVLC exports, bundled HLSL compilation, and basic hidden UI startup using a test data location. The test removes build-machine Qt paths before startup so it checks the installed payload. Full desktop rendering still needs the matrix below, even after they pass.

## Required Windows acceptance matrix

Run the Windows build first. Record OS build number, Qt/WebView2/VLC versions, GPU/driver, monitor topology/scales/refresh rates, and renderer for every failure. Preserve `aurora.log`.

| Area | Procedure | Expected result |
| --- | --- | --- |
| Clean build | Run dependency/build scripts on clean Windows x64 | Compiles, links, policy CTest passes, installed executable starts |
| First launch | Open library with empty app data | Two samples present; no wallpaper applied before selection; audio muted |
| Classic shell | Apply each renderer under Windows 10/11 classic layout | Wallpaper below icons; taskbar and desktop operations work |
| New Explorer layouts | Test each intended Windows 11 build, including modern Progman child layouts | Validated parent below icons, or explicit unsupported-layout error |
| Explorer restart | Restart Explorer from Task Manager | No topmost stray window; assignments reconstruct after shell returns |
| Show desktop | Win+D repeatedly; open/close Task View | No permanent lost wallpaper or broken icon layer |
| DPI | 100%, 125%, 150%, 200%, including mixed scales | Full coverage, no offsets/cropping, correctly mapped mouse |
| Virtual coordinates | Monitor left/above primary and portrait monitor | Correct placements and per-monitor fullscreen decisions |
| Refresh | 60/144 Hz combination | Each shader capped to its display; span capped by slowest display |
| Hotplug | Disconnect/reconnect an assigned monitor, change primary | Remaining instances valid; stable assignments restore |
| Span | Span across offset/unequal-resolution displays | One correctly bounded scene; documented gaps and policy behavior |
| Video codecs | H.264 MP4 and VP9 WebM supported by target GPU | Playback loops repeatedly, no growing memory/thread count |
| Hardware video | Inspect GPU decode engine and CPU while playing | Acceleration where supported; no unsupported zero-copy claim |
| Loop soak | Run each video for 100+ loops | No stopped player, volume reset, runaway threads, or crashes |
| Web lifecycle | Rapid fullscreen/pause toggles while loading and capturing | No stale callback resurrection; final desired pause wins |
| Web suspend failure | Use a page that prevents normal suspend | Visible error; instance disposed, no hidden active renderer left running |
| Web input | Move/click desktop, then app windows and taskbar | Desktop forwarding only; shell clicks remain functional |
| Input release | Hold mouse, change focus/pause, release elsewhere | No stuck web mouse-down state after resume |
| Web content | Local Canvas, CSS, and locally packaged Three.js | Assets load; external ordinary resources/navigation denied |
| Fullscreen | Borderless game/video on each monitor independently | Only applicable independent instance pauses; span follows any constituent |
| Coverage | Two snapped windows; minimize one; drag through threshold | Union area correct; resume hysteresis prevents flapping |
| Power | AC/battery transition, sleep/resume, display off/on, lock/unlock | Appropriate pause/Eco/mute; unrelated pause reasons persist |
| Audio | Global mute, per-display mute, slider on multiple players | Video volume/mute exact; WebView mute exact; WebAudio contract verified |
| Renderer errors | Invalid HLSL, missing video codec/file, WebView crash | Visible fault; no silent repeated restart; reapply retries |
| GPU loss | Exercise supported driver/device reset scenario | Present device-loss recovery bounded; other failures reported |
| Import | Unicode names, large videos, web symlinks/junctions/path traversal | UI responsive; valid copies persist; invalid imports rejected/cleaned |
| Persistence | Exit/relaunch; corrupt JSON intentionally in test profile | Assignments restore; corrupt file preserved with explicit error |
| Tray | Minimize, close, pause, next, mute, settings, exit | App accessible; exit stops renderers and leaves Explorer healthy |
| Shutdown stress | Reapply repeatedly then exit during asynchronous web initialization | No use-after-free, deadlock, orphan browser/audio, or HWND leak |

## Performance measurements before release

Measure idle, video, web, and shader separately at 1080p/4K and one/two displays. Record CPU percentage, working/private set, GPU engine utilization, frame cadence, power impact, browser subprocess count, and paused steady state. Compare static desktop to each workload. Confirm paused shaders sleep, videos stop progressing, and suspended web content stops normal animation/script activity. A low CPU thread priority alone says nothing about GPU priority or power consumption.

Use Windows Performance Recorder/Analyzer, Task Manager GPU-engine views, and an appropriate graphics debugger/profiler. Set concrete hardware-specific budgets from measurements; none have been measured in this environment.

Before declaring a stable production release, complete the physical desktop matrix, profile representative hardware, test dependency upgrades, sign the app and installer, and validate the package on a clean consumer Windows installation. Preview installers include vendor notices and matching source downloads but are currently unsigned.
