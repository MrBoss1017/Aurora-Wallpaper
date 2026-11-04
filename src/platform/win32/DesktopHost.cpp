#include "DesktopHost.h"
#include <mutex>

namespace aurora::win {
namespace {
constexpr UINT PosterMessage=WM_APP+141;
bool containsIcons(HWND root) {
    bool found=false;
    EnumChildWindows(root,[](HWND child, LPARAM data)->BOOL {
        if (className(child)==L"SHELLDLL_DefView") { *reinterpret_cast<bool*>(data)=true; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&found));
    return found;
}
bool ownedBy(HWND hwnd, DWORD pid) {
    DWORD actual{}; GetWindowThreadProcessId(hwnd,&actual); return actual==pid;
}
bool candidate(HWND hwnd, DWORD pid) {
    return hwnd && IsWindow(hwnd) && className(hwnd)==L"WorkerW" && ownedBy(hwnd,pid) && !containsIcons(hwnd);
}
}
void setPoster(HWND window,HBITMAP bitmap) {
    if (IsWindow(window) && className(window)==L"Aurora.RenderSurface") SendMessageW(window,PosterMessage,0,reinterpret_cast<LPARAM>(bitmap));
    else if (bitmap) DeleteObject(bitmap);
}
DesktopLayer discoverDesktopLayer() {
    HWND progman=FindWindowW(L"Progman",nullptr);
    if (!progman) throw std::runtime_error("Explorer desktop is unavailable. Aurora will retry.");
    DWORD pid{}; GetWindowThreadProcessId(progman,&pid);

    // 0x052C is an UNDOCUMENTED Explorer message. A timeout prevents a hung shell
    // from blocking the UI indefinitely. This is window parenting, not DLL injection.
    DWORD_PTR result{};
    SendMessageTimeoutW(progman,0x052C,0,0,
        SMTO_ABORTIFHUNG|SMTO_ERRORONEXIT,1000,&result);

    DesktopLayer layer{nullptr,nullptr,pid};
    // Classic layout (front -> back): icon-host WorkerW, empty WorkerW.
    // FindWindowEx(nullptr, iconHost, ...) searches *subsequent* top-level siblings,
    // not arbitrary WorkerW windows (many unrelated WorkerWs exist).
    EnumWindows([](HWND top, LPARAM data)->BOOL {
        auto& out=*reinterpret_cast<DesktopLayer*>(data);
        if (!ownedBy(top,out.shellPid) || !FindWindowExW(top,nullptr,L"SHELLDLL_DefView",nullptr)) return TRUE;
        HWND behind=FindWindowExW(nullptr,top,L"WorkerW",nullptr);
        if (candidate(behind,out.shellPid)) { out.worker=behind; out.iconHost=top; return FALSE; }
        return TRUE;
    },reinterpret_cast<LPARAM>(&layer));

    // Some newer Explorer layouts place both layers under Progman. Only select
    // an empty WorkerW *after* the icon child in sibling z-order. Never parent to
    // SHELLDLL_DefView, and never use an unvalidated Progman fallback over icons.
    if (!layer.worker) {
        HWND icons=FindWindowExW(progman,nullptr,L"SHELLDLL_DefView",nullptr);
        for (HWND child=icons?GetWindow(icons,GW_HWNDNEXT):nullptr; child; child=GetWindow(child,GW_HWNDNEXT)) {
            if (candidate(child,pid)) { layer.worker=child; layer.iconHost=progman; break; }
        }
    }
    if (!layer.worker || !IsWindowVisible(layer.worker))
        throw std::runtime_error("No validated background WorkerW found. This Explorer layout is unsupported or not ready.");
    return layer;
}
bool isDesktopSurface(HWND hwnd) {
    if (!hwnd) return false;
    HWND progman=FindWindowW(L"Progman",nullptr);
    DWORD pid{}; GetWindowThreadProcessId(progman,&pid);
    if (!pid || !ownedBy(hwnd,pid)) return false;
    auto cls=className(hwnd);
    if (cls!=L"WorkerW" && cls!=L"Progman" && cls!=L"SHELLDLL_DefView" && cls!=L"SysListView32") return false;
    auto root=className(GetAncestor(hwnd,GA_ROOT));
    return root==L"WorkerW" || root==L"Progman";
}
LRESULT CALLBACK DesktopHost::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto self=reinterpret_cast<DesktopHost*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) {
        self=static_cast<DesktopHost*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_=hwnd;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if (msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg==PosterMessage && self) {
        if (self->poster_) DeleteObject(self->poster_);
        self->poster_=reinterpret_cast<HBITMAP>(lp); InvalidateRect(hwnd,nullptr,FALSE); return 0;
    }
    if (msg==WM_PAINT && self) {
        PAINTSTRUCT paint{}; HDC dc=BeginPaint(hwnd,&paint); RECT client{};GetClientRect(hwnd,&client);
        if (self->poster_) {
            BITMAP bitmap{};GetObjectW(self->poster_,sizeof(bitmap),&bitmap);
            HDC memory=CreateCompatibleDC(dc);auto old=SelectObject(memory,self->poster_);
            StretchBlt(dc,0,0,client.right,client.bottom,memory,0,0,bitmap.bmWidth,bitmap.bmHeight,SRCCOPY);
            SelectObject(memory,old);DeleteDC(memory);
        } else FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        EndPaint(hwnd,&paint);return 0;
    }
    if (msg==WM_ERASEBKGND) return 1;
    if (msg==WM_NCDESTROY && self) {
        if (self->poster_) {DeleteObject(self->poster_);self->poster_=nullptr;}
        self->hwnd_=nullptr; SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
    }
    return DefWindowProcW(hwnd,msg,wp,lp);
}
DesktopHost::DesktopHost(const DesktopLayer& layer, Rect bounds):layer_(layer) {
    static std::once_flag once;
    std::call_once(once,[] {
        WNDCLASSEXW wc{sizeof(wc)}; wc.hInstance=GetModuleHandleW(nullptr);
        wc.lpfnWndProc=windowProc; wc.lpszClassName=L"Aurora.RenderSurface";
        wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        wc.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        if (!RegisterClassExW(&wc)) fail("RegisterClassExW");
    });
    // Match the parent DPI context *before* creating the child. Cross-process
    // SetParent with different awareness modes can reset the child's DPI mode.
    DpiScope scope(GetWindowDpiAwarenessContext(layer.worker));
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"Aurora.RenderSurface",L"",
        WS_POPUP|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),this);
    if (!hwnd) fail("CreateWindowExW");
    try {
        // SetParent does NOT change WS_POPUP/WS_CHILD for us.
        auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);
        SetLastError(ERROR_SUCCESS);
        if (!SetWindowLongPtrW(hwnd,GWL_STYLE,(style & ~static_cast<LONG_PTR>(WS_POPUP))|WS_CHILD)
            && GetLastError()!=ERROR_SUCCESS) fail("SetWindowLongPtrW");
        // NULL can mean successful parenting of a previously unparented window.
        SetLastError(ERROR_SUCCESS);
        HWND previous=SetParent(hwnd,layer.worker);
        if (!previous && GetLastError()!=ERROR_SUCCESS) fail("SetParent");
        if (GetParent(hwnd)!=layer.worker) throw std::runtime_error("WorkerW parenting did not persist.");
        SendMessageW(hwnd,WM_CHANGEUISTATE,MAKEWPARAM(UIS_INITIALIZE,0),0);
        place(bounds);
    } catch (...) { DestroyWindow(hwnd); throw; }
}
DesktopHost::~DesktopHost() { if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_); }
bool DesktopHost::healthy() const {
    return hwnd_ && IsWindow(hwnd_) && candidate(layer_.worker,layer_.shellPid) && GetParent(hwnd_)==layer_.worker;
}
void DesktopHost::place(Rect bounds) {
    POINT tl=physicalToClient(layer_.worker,{bounds.left,bounds.top});
    POINT br=physicalToClient(layer_.worker,{bounds.right,bounds.bottom});
    DpiScope scope(GetWindowDpiAwarenessContext(layer_.worker));
    if (!SetWindowPos(hwnd_,HWND_BOTTOM,tl.x,tl.y,std::max(1L,br.x-tl.x),std::max(1L,br.y-tl.y),
        SWP_NOACTIVATE|SWP_FRAMECHANGED)) fail("SetWindowPos");
}
void DesktopHost::show() { ShowWindow(hwnd_,SW_SHOWNOACTIVATE); }
std::pair<int,int> DesktopHost::clientSize() const {
    DpiScope scope(GetWindowDpiAwarenessContext(hwnd_));
    RECT r{}; GetClientRect(hwnd_,&r); return {std::max(1L,r.right),std::max(1L,r.bottom)};
}
}
