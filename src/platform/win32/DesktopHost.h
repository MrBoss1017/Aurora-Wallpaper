#pragma once
#include "Win32.h"
#include <functional>
namespace aurora::win {
// HWNDs are borrowed from Explorer. Never destroy, hide, or restyle Explorer's windows.
struct DesktopLayer { HWND worker{}, iconHost{}; DWORD shellPid{}; };
DesktopLayer discoverDesktopLayer();
bool isDesktopSurface(HWND window);
// Transfers ownership of a GDI bitmap to our own render HWND (used for web pause).
void setPoster(HWND window,HBITMAP bitmap);
class DesktopHost {
    HWND hwnd_{};
    HBITMAP poster_{};
    DesktopLayer layer_{};
    static LRESULT CALLBACK windowProc(HWND,UINT,WPARAM,LPARAM);
public:
    DesktopHost(const DesktopLayer&, Rect physicalBounds);
    ~DesktopHost();
    DesktopHost(const DesktopHost&)=delete;
    DesktopHost& operator=(const DesktopHost&)=delete;
    HWND handle() const { return hwnd_; }
    bool healthy() const;
    void place(Rect physicalBounds);
    void show();
    std::pair<int,int> clientSize() const;
};
}
