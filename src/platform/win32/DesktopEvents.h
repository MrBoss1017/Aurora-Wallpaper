#pragma once
#include "Win32.h"
#include <functional>
#include <vector>
namespace aurora::win {
struct Monitor {
    std::wstring id, name, gdiName;
    Rect bounds;
    double refreshHz=60;
    bool primary{};
};
std::vector<Monitor> enumerateMonitors();
struct WindowSnapshot { std::vector<Rect> windows; Rect foreground{}; bool hasForeground{}; };
WindowSnapshot sampleWindows();
bool onBattery();
enum class DesktopEvent { Activity, Topology, ShellRestart, Session, Power };
struct MouseEvent { POINT physical{}; bool leftDown{}, leftUp{}; };
// Owns a hidden top-level window, not HWND_MESSAGE: it must receive broadcasts.
class DesktopEvents {
    HWND hwnd_{};
    UINT taskbarCreated_{};
    std::vector<HWINEVENTHOOK> hooks_;
    HPOWERNOTIFY powerNotify_{};
    bool registeredSession_{}, inputEnabled_{};
    static HWND callbackWindow_;
    static void CALLBACK eventProc(HWINEVENTHOOK,DWORD,HWND,LONG,LONG,DWORD,DWORD);
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
    void rawInput(HRAWINPUT);
public:
    std::function<void(DesktopEvent)> changed;
    std::function<void(MouseEvent)> mouse;
    bool locked{}, displayOff{}, suspended{};
    DesktopEvents();
    ~DesktopEvents();
    DesktopEvents(const DesktopEvents&)=delete;
    void enableInput(bool);
};
}
