# Wallpaper authoring

## Video

Import a local `.mp4` or `.webm`. Aurora copies it into its owned library and loops it. libVLC selects the available hardware decoder and falls back when needed. The OS property handler supplies width/height metadata when available; otherwise the card says `Display-sized / auto`. Put an optional `preview.png` or `preview.jpg` alongside the video for the library card. Without it, the UI uses a labeled type illustration, not a claimed screenshot.

A video has its native frame cadence; changing the shader frame cap does not re-encode or arbitrarily drop video frames. Resource rules pause the player when required.

## Web folder

Provide a folder with `index.html`, local assets, and optionally `wallpaper.json`:

```json
{
  "title": "My Wallpaper",
  "entry": "index.html",
  "width": 1920,
  "height": 1080,
  "thumbnail": "preview.png"
}
```

`width` and `height` are descriptive design dimensions, not a fixed browser viewport. The viewport follows the host display. Use relative paths, with `/` separators. Entry paths cannot be absolute, contain `..`, or traverse symlinks/junctions. A bundle is limited to 20,000 entries and 2 GiB. Zip extraction is intentionally outside the importer.

Each wallpaper is mapped to its own `https://<uuid>.wallpaper.invalid/` origin. Ordinary external resource requests and off-origin navigation are denied. Package Three.js or other dependencies locally; CDN links will not load. Use a restrictive CSP such as the example in `samples/web-orbit/index.html`. Do not assume the host's filters replace an OS network sandbox.

The bootstrap defines `window.wallpaperState` and emits a `wallpaperstate` event:

```javascript
const state = window.wallpaperState;
// { paused: boolean, muted: boolean, volume: 0..1, fps: 1..240 }
addEventListener('wallpaperstate', ({ detail }) => {
  // Pause your simulation, apply a frame cap, and update WebAudio master gain.
});
```

Use a time accumulator that excludes paused time. For Canvas/WebGL, gate drawing by `1000 / state.fps`, and use bounded time deltas after resume. CSS animations and arbitrary third-party render loops do not automatically follow a numeric FPS value. Aurora uses browser suspension for hard pause requests. A successful suspend stops normal page activity; browser failure causes that renderer instance to stop and requires reapply.

Native mute covers the whole WebView. HTML `<audio>` and `<video>` elements receive volume updates. For WebAudio, route sources through a master `GainNode` and update its gain from the event. The host does not claim universal WebAudio volume control. Browser autoplay policies still apply.

When desktop mouse mirroring is enabled, the page receives native movement and left-button events through the composition controller. Use normal `pointermove` and `click` handlers. No keyboard, wheel, right-click, or touch contract is provided. Desktop icon activation continues normally.

Use only trusted bundles. Host objects, native message APIs, permissions, downloads, default script dialogs, devtools, new windows, and browser shortcut handling are disabled, but this is not a security review of every Chromium/network primitive.

## HLSL

Import a self-contained `.hlsl` file, at most 1 MiB. The renderer supplies a fullscreen triangle with `SV_POSITION` and `TEXCOORD0`. Define `PSMain` and this exact constant buffer:

```hlsl
cbuffer Wallpaper : register(b0) {
    float2 resolution; // Host client width and height
    float time;        // Accumulated active simulation seconds
    float deltaTime;   // Bounded active frame delta, <= 0.1 seconds
    float2 mouse;      // Host client coordinates; zero until input arrives
    float2 padding;    // Reserved; makes the buffer 32 bytes
};

float4 PSMain(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    float wave = 0.5 + 0.5 * sin(uv.x * 8 + time);
    return float4(uv.y * 0.2, wave * 0.5, 0.35, 1);
}
```

Target is `ps_5_0`. Source compilation has no include handler. This implementation provides procedural pixel shaders, not an arbitrary scene graph: textures, compute shaders, model loading, an editor, audio analysis, and shader hot reload are not implemented. Reimport/reapply to change a shader. GLSL is supported only through a WebGL bundle; there is no GLSL translator in the native renderer.

The worker uses below-normal CPU scheduling priority. This does **not** reduce GPU queue priority or constrain malicious/expensive shaders. Expensive pixel code can still saturate the GPU or trigger a driver timeout. Application-level shader process isolation is not part of this version.
