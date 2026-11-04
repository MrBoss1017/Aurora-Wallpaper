#include "core/Policy.h"
#include <cstdlib>
#include <iostream>
#include <random>
#include <stdexcept>
using namespace aurora;
static int checks;
static void check(bool value, const char* message) {
    ++checks;
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        Rect monitor{-1920,0,0,1080};
        check(covers({-1920,-1,0,1080},monitor),"fullscreen at negative origin");
        check(!covers({0,0,1920,1080},monitor),"other monitor must not pause");
        check(coverage(monitor,{{-1920,0,-960,1080},{-960,0,0,1080}})==1,"two snapped windows");
        check(coverage(monitor,{monitor,monitor})==1,"no overlap double counting");
        check(coverage(monitor,{{0,0,100,100}})==0,"off-monitor window");
        check(coverage({}, {monitor})==0,"empty monitor");
        // Compare the sweep-line union against an independent pixel-grid oracle.
        std::mt19937 rng(42);
        for (int trial=0;trial<500;++trial) {
            std::vector<Rect> windows;
            for (int j=0;j<8;++j) {
                int x=static_cast<int>(rng()%30)-15, y=static_cast<int>(rng()%30)-15;
                windows.push_back({x,y,x+static_cast<int>(rng()%20),y+static_cast<int>(rng()%20)});
            }
            int pixels=0;
            for (int y=-10;y<10;++y) for (int x=-10;x<10;++x) {
                bool hit=false;
                for (auto r:windows) hit |= r.contains(x,y);
                pixels += hit;
            }
            check(std::abs(coverage({-10,-10,10,10},windows)-pixels/400.0)<1e-10,"union oracle");
        }
        Rules rules; Environment env;
        check(!evaluate(Kind::Video,rules,env).paused,"normal playback");
        env.fullscreen=true;
        check(evaluate(Kind::Web,rules,env).paused,"fullscreen web pause");
        env={}; env.battery=true; rules.battery=ResourceMode::Eco;
        check(evaluate(Kind::Shader,rules,env).fps==15,"shader eco fps");
        check(!evaluate(Kind::Shader,rules,env).paused,"shader eco keeps playing");
        check(evaluate(Kind::Video,rules,env).paused,"video eco pauses decoding");
        check(evaluate(Kind::Web,rules,env).paused,"web eco suspends");
        env={}; env.refreshHz=59.94; rules.fps=144;
        check(evaluate(Kind::Shader,rules,env).fps==59,"refresh-rate cap");
        env.displayMute=true;
        check(evaluate(Kind::Video,rules,env).muted,"per-display mute");
        env={}; env.locked=true; env.manualPause=true;
        auto p=evaluate(Kind::Video,rules,env);
        check(p.paused && p.muted && p.reasons==(Manual|Locked),"composed reasons");
        env={}; env.covered=.89;
        check(!evaluate(Kind::Video,rules,env).paused,"coverage below threshold");
        env.covered=.90;
        check(evaluate(Kind::Video,rules,env).paused,"coverage threshold inclusive");
        std::cout << checks << " checks passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return EXIT_FAILURE; }
}
