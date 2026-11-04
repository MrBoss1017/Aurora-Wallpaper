#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace aurora {
struct Rect {
    int left{}, top{}, right{}, bottom{};
    int width() const { return std::max(0, right - left); }
    int height() const { return std::max(0, bottom - top); }
    bool contains(int x, int y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
};
inline Rect intersect(Rect a, Rect b) {
    return {std::max(a.left,b.left),std::max(a.top,b.top),
            std::min(a.right,b.right),std::min(a.bottom,b.bottom)};
}
inline bool covers(Rect window, Rect monitor, int tolerance = 2) {
    return window.left <= monitor.left+tolerance && window.top <= monitor.top+tolerance
        && window.right >= monitor.right-tolerance && window.bottom >= monitor.bottom-tolerance;
}
// Exact union area: overlapping windows are counted once. Negative monitor origins work.
inline double coverage(Rect monitor, const std::vector<Rect>& windows) {
    if (!monitor.width() || !monitor.height()) return 0;
    std::vector<Rect> clipped;
    std::vector<int> xs;
    for (auto w: windows) {
        auto r = intersect(w, monitor);
        if (r.width() && r.height()) { clipped.push_back(r); xs.push_back(r.left); xs.push_back(r.right); }
    }
    std::sort(xs.begin(), xs.end());
    xs.erase(std::unique(xs.begin(),xs.end()),xs.end());
    std::int64_t area = 0;
    for (std::size_t i=1;i<xs.size();++i) {
        std::vector<std::pair<int,int>> ys;
        for (auto r: clipped) if (r.left < xs[i] && r.right > xs[i-1]) ys.emplace_back(r.top,r.bottom);
        std::sort(ys.begin(),ys.end());
        std::int64_t height = 0;
        int end = monitor.top;
        for (auto [a,b]: ys) { if (b > end) height += b-std::max(a,end); end = std::max(end,b); }
        area += static_cast<std::int64_t>(xs[i]-xs[i-1])*height;
    }
    return static_cast<double>(area)/(static_cast<std::int64_t>(monitor.width())*monitor.height());
}
enum class Kind { Video, Web, Shader };
enum class ResourceMode { Ignore, Eco, Pause };
struct Rules {
    bool pauseFullscreen = true;
    ResourceMode battery = ResourceMode::Pause;
    ResourceMode overlap = ResourceMode::Pause;
    double overlapThreshold = 0.90;
    int fps = 60;
    int ecoFps = 15;
    int volume = 50;
};
struct Environment {
    bool manualPause{}, fullscreen{}, battery{}, locked{}, displayOff{}, globalMute{}, displayMute{};
    double covered{};
    double refreshHz = 60;
};
enum Reason : unsigned {
    Manual = 1, Fullscreen = 2, Battery = 4, Covered = 8, Locked = 16, DisplayOff = 32
};
struct Playback {
    bool paused = true;
    bool muted = true;
    int fps = 60;
    int volume = 50;
    unsigned reasons{};
    bool operator==(const Playback&) const = default;
};
inline Playback evaluate(Kind kind, const Rules& rules, const Environment& env) {
    Playback out;
    out.paused = false;
    out.volume = std::clamp(rules.volume,0,100);
    out.fps = std::clamp(std::min(rules.fps,static_cast<int>(std::floor(env.refreshHz))),1,240);
    auto pause = [&](unsigned reason) { out.paused = true; out.reasons |= reason; };
    auto resource = [&](ResourceMode mode, unsigned reason) {
        if (mode == ResourceMode::Pause || (mode == ResourceMode::Eco && kind != Kind::Shader)) pause(reason);
        else if (mode == ResourceMode::Eco) out.fps = std::min(out.fps,std::clamp(rules.ecoFps,1,60));
    };
    if (env.manualPause) pause(Manual);
    if (env.locked) pause(Locked);
    if (env.displayOff) pause(DisplayOff);
    if (env.fullscreen && rules.pauseFullscreen) pause(Fullscreen);
    if (env.battery) resource(rules.battery,Battery);
    if (env.covered >= std::clamp(rules.overlapThreshold,0.01,1.0)) resource(rules.overlap,Covered);
    out.muted = env.globalMute || env.displayMute || out.paused || out.volume == 0;
    return out;
}
} // namespace aurora
