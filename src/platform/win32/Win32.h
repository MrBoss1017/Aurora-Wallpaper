#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wrl/client.h>
#include <string>
#include <stdexcept>
#include <system_error>
#include "core/Policy.h"

namespace aurora::win {
using Microsoft::WRL::ComPtr;
inline void check(HRESULT hr, const char* context) {
    if (FAILED(hr)) throw std::runtime_error(std::string(context)+" (HRESULT "+std::to_string(static_cast<unsigned>(hr))+")");
}
[[noreturn]] inline void fail(const char* operation) {
    throw std::system_error(static_cast<int>(GetLastError()),std::system_category(),operation);
}
inline Rect rect(RECT r) { return {r.left,r.top,r.right,r.bottom}; }
inline std::wstring className(HWND hwnd) {
    wchar_t buffer[256]{}; GetClassNameW(hwnd,buffer,256); return buffer;
}
class DpiScope {
    DPI_AWARENESS_CONTEXT old_;
public:
    explicit DpiScope(DPI_AWARENESS_CONTEXT context): old_(SetThreadDpiAwarenessContext(context)) {}
    ~DpiScope() { if (old_) SetThreadDpiAwarenessContext(old_); }
    DpiScope(const DpiScope&)=delete;
};
// Accept physical screen coordinates, including negative monitor origins.
inline POINT physicalToClient(HWND hwnd, POINT point) {
    if (!PhysicalToLogicalPointForPerMonitorDPI(hwnd,&point)) fail("PhysicalToLogicalPointForPerMonitorDPI");
    DpiScope scope(GetWindowDpiAwarenessContext(hwnd));
    if (!ScreenToClient(hwnd,&point)) fail("ScreenToClient");
    return point;
}
} // namespace aurora::win
