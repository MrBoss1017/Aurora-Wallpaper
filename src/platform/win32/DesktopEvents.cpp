#include "DesktopEvents.h"
#include "DesktopHost.h"
#include <dwmapi.h>
#include <wtsapi32.h>
#include <powrprof.h>
#include <map>
#include <cstring>

namespace aurora::win {
namespace {
struct Identity { std::wstring id,name; double hz{}; };
std::map<std::wstring,Identity> displayIdentities() {
    std::map<std::wstring,Identity> out;
    // The topology may change between size query and enumeration. Retry boundedly.
    for (int attempt=0;attempt<3;++attempt) {
        UINT32 pc{},mc{};
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS,&pc,&mc)!=ERROR_SUCCESS) break;
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pc);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(mc);
        LONG result=QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS,&pc,paths.data(),&mc,modes.data(),nullptr);
        if (result==ERROR_INSUFFICIENT_BUFFER) continue;
        if (result!=ERROR_SUCCESS) break;
        paths.resize(pc);
        for (auto& path:paths) {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
            source.header={DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME,sizeof(source),path.sourceInfo.adapterId,path.sourceInfo.id};
            DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
            target.header={DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME,sizeof(target),path.targetInfo.adapterId,path.targetInfo.id};
            if (DisplayConfigGetDeviceInfo(&source.header)!=ERROR_SUCCESS || DisplayConfigGetDeviceInfo(&target.header)!=ERROR_SUCCESS) continue;
            auto r=path.targetInfo.refreshRate;
            out.try_emplace(source.viewGdiDeviceName,Identity{target.monitorDevicePath,target.monitorFriendlyDeviceName,
                r.Denominator?static_cast<double>(r.Numerator)/r.Denominator:60.0});
        }
        break;
    }
    return out;
}
bool eligible(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    DWORD pid{}; GetWindowThreadProcessId(hwnd,&pid);
    if (pid==GetCurrentProcessId()) return false; // Keep wallpaper visible in its control window preview.
    auto name=className(hwnd);
    if (name==L"Progman" || name==L"WorkerW" || name==L"Shell_TrayWnd" || name==L"Shell_SecondaryTrayWnd") return false;
    auto ex=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return false;
    DWORD cloaked{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked) return false;
    if (ex & WS_EX_LAYERED) {
        COLORREF color{}; BYTE alpha{}; DWORD flags{};
        if (GetLayeredWindowAttributes(hwnd,&color,&alpha,&flags) && (flags & LWA_ALPHA) && alpha<255) return false;
    }
    return true;
}
Rect visibleBounds(HWND hwnd) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd,DWMWA_EXTENDED_FRAME_BOUNDS,&r,sizeof(r)))) GetWindowRect(hwnd,&r);
    return rect(r);
}
}
std::vector<Monitor> enumerateMonitors() {
    DpiScope physical(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto identities=displayIdentities();
    std::vector<Monitor> out;
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR monitor,HDC,LPRECT,LPARAM data)->BOOL {
        auto& result=*reinterpret_cast<std::vector<Monitor>*>(data);
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        if (!GetMonitorInfoW(monitor,&info)) return TRUE;
        DEVMODEW mode{}; mode.dmSize=sizeof(mode);
        double hz=60;
        if (EnumDisplaySettingsW(info.szDevice,ENUM_CURRENT_SETTINGS,&mode) && mode.dmDisplayFrequency>1) hz=mode.dmDisplayFrequency;
        result.push_back({info.szDevice,info.szDevice,info.szDevice,rect(info.rcMonitor),hz,(info.dwFlags&MONITORINFOF_PRIMARY)!=0});
        return TRUE;
    },reinterpret_cast<LPARAM>(&out));
    for (auto& monitor:out) if (auto it=identities.find(monitor.gdiName);it!=identities.end()) {
        if (!it->second.id.empty()) monitor.id=it->second.id;
        if (!it->second.name.empty()) monitor.name=it->second.name;
        if (it->second.hz>1) monitor.refreshHz=it->second.hz;
    }
    return out;
}
WindowSnapshot sampleWindows() {
    DpiScope physical(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WindowSnapshot out;
    EnumWindows([](HWND hwnd,LPARAM data)->BOOL {
        auto& snapshot=*reinterpret_cast<WindowSnapshot*>(data);
        if (eligible(hwnd)) {
            auto r=visibleBounds(hwnd);
            if (r.width()>0 && r.height()>0) snapshot.windows.push_back(r);
        }
        return TRUE;
    },reinterpret_cast<LPARAM>(&out));
    HWND foreground=GetForegroundWindow();
    if (eligible(foreground)) { out.foreground=visibleBounds(foreground); out.hasForeground=true; }
    return out;
}
bool onBattery() {
    SYSTEM_POWER_STATUS status{};
    return GetSystemPowerStatus(&status) && status.ACLineStatus==0 && status.BatteryFlag!=128 && status.BatteryFlag!=255;
}
namespace {
bool sessionLocked() {
    LPWSTR buffer{}; DWORD size{}; bool locked=false;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,WTS_CURRENT_SESSION,WTSInfoEx,&buffer,&size)) {
        if (size>=sizeof(WTSINFOEXW)) {
            auto info=reinterpret_cast<WTSINFOEXW*>(buffer);
            if (info->Level==1) locked=info->Data.WTSInfoExLevel1.SessionFlags==WTS_SESSIONSTATE_LOCK;
        }
        WTSFreeMemory(buffer);
    }
    return locked;
}
}
HWND DesktopEvents::callbackWindow_{};
namespace { constexpr UINT ActivityMessage=WM_APP+73; }
void CALLBACK DesktopEvents::eventProc(HWINEVENTHOOK,DWORD event,HWND,LONG object,LONG,DWORD,DWORD) {
    // No COM, enumeration, or blocking work in this callback. Qt coalesces the wakeup.
    if (event>=EVENT_OBJECT_CREATE && object!=OBJID_WINDOW) return;
    if (callbackWindow_) PostMessageW(callbackWindow_,ActivityMessage,0,0);
}
DesktopEvents::DesktopEvents() {
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr); wc.lpfnWndProc=windowProc; wc.lpszClassName=L"Aurora.Events";
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) fail("RegisterClassW");
    hwnd_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,wc.hInstance,this);
    if (!hwnd_) fail("Create event window");
    callbackWindow_=hwnd_;
    locked=sessionLocked();
    taskbarCreated_=RegisterWindowMessageW(L"TaskbarCreated");
    registeredSession_=WTSRegisterSessionNotification(hwnd_,NOTIFY_FOR_THIS_SESSION)!=FALSE;
    powerNotify_=RegisterPowerSettingNotification(hwnd_,&GUID_CONSOLE_DISPLAY_STATE,DEVICE_NOTIFY_WINDOW_HANDLE);
    const std::pair<DWORD,DWORD> ranges[]={{EVENT_SYSTEM_FOREGROUND,EVENT_SYSTEM_FOREGROUND},
        {EVENT_SYSTEM_MINIMIZESTART,EVENT_SYSTEM_MINIMIZEEND},{EVENT_OBJECT_SHOW,EVENT_OBJECT_HIDE},
        {EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_LOCATIONCHANGE}};
    for (auto [start,end]:ranges) {
        auto hook=SetWinEventHook(start,end,nullptr,eventProc,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        if (hook) hooks_.push_back(hook); // A 1-second sampler remains active if hooks are unavailable.
    }
}
DesktopEvents::~DesktopEvents() {
    changed={}; mouse={};
    enableInput(false);
    callbackWindow_=nullptr;
    for (auto hook:hooks_) UnhookWinEvent(hook);
    if (registeredSession_) WTSUnRegisterSessionNotification(hwnd_);
    if (powerNotify_) UnregisterPowerSettingNotification(powerNotify_);
    if (hwnd_) DestroyWindow(hwnd_);
}
void DesktopEvents::enableInput(bool enabled) {
    if (enabled==inputEnabled_) return;
    RAWINPUTDEVICE device{0x01,0x02,static_cast<DWORD>(enabled?RIDEV_INPUTSINK:RIDEV_REMOVE),enabled?hwnd_:nullptr};
    if (!RegisterRawInputDevices(&device,1,sizeof(device))) {
        if (enabled) fail("RegisterRawInputDevices");
    } else inputEnabled_=enabled;
}
void DesktopEvents::rawInput(HRAWINPUT handle) {
    // Read only mouse packets; no keyboard registration, key logging, or event suppression.
    // Only mirror when both foreground and pointer hit-test belong to the desktop.
    if (!inputEnabled_ || !mouse || !isDesktopSurface(GetForegroundWindow())) return;
    POINT point{};
    if (!GetPhysicalCursorPos(&point)) return;
    DpiScope physical(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if (!isDesktopSurface(WindowFromPoint(point))) return;
    RAWINPUT packet{}; UINT size=sizeof(packet);
    if (GetRawInputData(handle,RID_INPUT,&packet,&size,sizeof(RAWINPUTHEADER))==static_cast<UINT>(-1) || packet.header.dwType!=RIM_TYPEMOUSE) return;
    auto flags=packet.data.mouse.usButtonFlags;
    mouse({point,(flags&RI_MOUSE_LEFT_BUTTON_DOWN)!=0,(flags&RI_MOUSE_LEFT_BUTTON_UP)!=0});
}
LRESULT CALLBACK DesktopEvents::windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto self=reinterpret_cast<DesktopEvents*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) {
        self=static_cast<DesktopEvents*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd,msg,wp,lp);
    auto notify=[&](DesktopEvent event) { if (self->changed) self->changed(event); };
    if (msg==ActivityMessage) notify(DesktopEvent::Activity);
    else if (msg==WM_DISPLAYCHANGE || msg==WM_DPICHANGED || msg==WM_SETTINGCHANGE) notify(DesktopEvent::Topology);
    else if (self->taskbarCreated_ && msg==self->taskbarCreated_) notify(DesktopEvent::ShellRestart);
    else if (msg==WM_WTSSESSION_CHANGE) {
        if (wp==WTS_SESSION_LOCK || wp==WTS_REMOTE_DISCONNECT || wp==WTS_CONSOLE_DISCONNECT) self->locked=true;
        if (wp==WTS_SESSION_UNLOCK || wp==WTS_REMOTE_CONNECT || wp==WTS_CONSOLE_CONNECT) self->locked=false;
        notify(DesktopEvent::Session);
    } else if (msg==WM_POWERBROADCAST) {
        if (wp==PBT_APMSUSPEND) self->suspended=true;
        if (wp==PBT_APMRESUMEAUTOMATIC || wp==PBT_APMRESUMESUSPEND) { self->suspended=false; self->locked=sessionLocked(); notify(DesktopEvent::Topology); }
        if (wp==PBT_POWERSETTINGCHANGE) {
            auto setting=reinterpret_cast<POWERBROADCAST_SETTING*>(lp);
            if (IsEqualGUID(setting->PowerSetting,GUID_CONSOLE_DISPLAY_STATE) && setting->DataLength==sizeof(DWORD)) {
                DWORD state{}; std::memcpy(&state,setting->Data,sizeof(state)); self->displayOff=state==0;
            }
        }
        notify(DesktopEvent::Power);
    } else if (msg==WM_INPUT) {
        try { self->rawInput(reinterpret_cast<HRAWINPUT>(lp)); } catch (...) { /* Never unwind a native callback. */ }
    }
    return DefWindowProcW(hwnd,msg,wp,lp); // Required cleanup for foreground WM_INPUT.
}
}
