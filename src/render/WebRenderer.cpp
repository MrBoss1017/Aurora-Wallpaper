#include "Renderer.h"
#include "platform/win32/DesktopHost.h"
#include <WebView2.h>
#include <wrl.h>
#include <dcomp.h>
#include <d3d11.h>
#include <QStandardPaths>
#include <QDir>
#include <QUrl>
#include <QJsonObject>
#include <QJsonDocument>
#include <QImage>
#include <cstring>

namespace aurora {
namespace {
using namespace win;
using Microsoft::WRL::Callback;
HBITMAP bitmapFromStream(IStream* stream) {
    STATSTG info{};
    if (FAILED(stream->Stat(&info,STATFLAG_NONAME)) || info.cbSize.QuadPart>64*1024*1024 || info.cbSize.QuadPart==0) return nullptr;
    LARGE_INTEGER zero{}; if (FAILED(stream->Seek(zero,STREAM_SEEK_SET,nullptr))) return nullptr;
    QByteArray bytes(static_cast<qsizetype>(info.cbSize.QuadPart),'\0'); ULONG read{};
    if (FAILED(stream->Read(bytes.data(),static_cast<ULONG>(bytes.size()),&read)) || read!=bytes.size()) return nullptr;
    auto image=QImage::fromData(bytes,"PNG").convertToFormat(QImage::Format_ARGB32);
    if (image.isNull()) return nullptr;
    BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth=image.width();
    bmi.bmiHeader.biHeight=-image.height(); bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
    void* pixels{}; HBITMAP bitmap=CreateDIBSection(nullptr,&bmi,DIB_RGB_COLORS,&pixels,nullptr,0);
    if (bitmap) std::memcpy(pixels,image.constBits(),static_cast<std::size_t>(image.sizeInBytes()));
    return bitmap;
}
struct WebState: std::enable_shared_from_this<WebState> {
    HWND hwnd{};
    Wallpaper wallpaper;
    Playback desired;
    RenderError error;
    bool alive=true,ready=false,suspendPending=false,freezePending=false,leftHeld=false;
    int width=1,height=1;
    QString host;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2CompositionController> composition;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> web;
    ComPtr<ICoreWebView2_3> lifecycle;
    ComPtr<ICoreWebView2_8> audio;
    ComPtr<ID3D11Device> graphics;
    ComPtr<IDCompositionDevice> dcomp;
    ComPtr<IDCompositionTarget> target;
    ComPtr<IDCompositionVisual> visual;
    void fail(QString message) { if (alive && error) error(std::move(message)); }
    bool local(const QString& uri) const {
        QUrl url(uri);
        return url.scheme()=="https" && url.host()==host && url.port(-1)==-1 && url.userInfo().isEmpty();
    }
    void size() {
        if (!controller) return;
        RECT r{0,0,width,height}; controller->put_Bounds(r); controller->NotifyParentWindowPositionChanged();
    }
    QString stateJson() const {
        return QString::fromUtf8(QJsonDocument(QJsonObject{{"paused",desired.paused},{"muted",desired.muted},
            {"volume",desired.volume/100.0},{"fps",desired.fps}}).toJson(QJsonDocument::Compact));
    }
    void publish() {
        if (!web || !ready) return;
        QString js="window.__auroraUpdate && window.__auroraUpdate("+stateJson()+");";
        web->ExecuteScript(reinterpret_cast<LPCWSTR>(js.utf16()),nullptr);
    }
    void synchronize() {
        if (!alive || !controller || !ready) return;
        audio->put_IsMuted(desired.muted?TRUE:FALSE);
        if (desired.paused) {
            // Must hide before TrySuspend. WebView2 does not offer a universal FPS cap
            // for arbitrary CSS/WebGL/JS; pause uses the browser lifecycle instead.
            if (suspendPending || freezePending) return;
            BOOL suspended{}; lifecycle->get_IsSuspended(&suspended);
            if (suspended) return;
            publish();
            // Preserve the last rendered frame before hiding the browser. Capture is
            // asynchronous; every completion checks the newest playback intent.
            ComPtr<IStream> stream;
            if (SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream))) {
                freezePending=true;auto weak=weak_from_this();
                auto hr=web->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),
                    Callback<ICoreWebView2CapturePreviewCompletedHandler>([weak,stream](HRESULT hr)->HRESULT {
                        if (auto s=weak.lock();s && s->alive) {
                            s->freezePending=false;
                            if (s->desired.paused) {
                                if (SUCCEEDED(hr)) if (auto bitmap=bitmapFromStream(stream.Get())) setPoster(s->hwnd,bitmap);
                                s->suspend();
                            }
                        }
                        return S_OK;
                    }).Get());
                if (SUCCEEDED(hr)) return;
                freezePending=false;
            }
            suspend();
        } else {
            lifecycle->Resume(); controller->put_IsVisible(TRUE); publish();
        }
    }
    void suspend() {
            if (!alive || !desired.paused || suspendPending) return;
            controller->put_IsVisible(FALSE);
            suspendPending=true;
            auto weak=weak_from_this();
            auto hr=lifecycle->TrySuspend(Callback<ICoreWebView2TrySuspendCompletedHandler>(
                [weak](HRESULT result,BOOL success)->HRESULT {
                    if (auto s=weak.lock();s && s->alive) {
                        s->suspendPending=false;
                        // A fullscreen/battery transition may reverse during the async call.
                        // Reconcile the CURRENT desired state, not a captured old pause value.
                        if (!s->desired.paused) { s->lifecycle->Resume(); s->controller->put_IsVisible(TRUE); s->publish(); }
                        else if (FAILED(result) || !success) s->fail("WebView2 could not suspend this page; this instance was stopped. Reapply or use a different bundle.");
                    }
                    return S_OK;
                }).Get());
            if (FAILED(hr)) { suspendPending=false; fail("WebView2 TrySuspend failed."); }
    }
    void close() {
        alive=false; ready=false;
        if (composition) composition->put_RootVisualTarget(nullptr);
        if (controller) controller->Close();
        if (target) target->SetRoot(nullptr);
        if (dcomp) dcomp->Commit();
        web.Reset(); audio.Reset(); lifecycle.Reset(); composition.Reset(); controller.Reset();
        visual.Reset(); target.Reset(); dcomp.Reset(); graphics.Reset(); environment.Reset();
    }
};
// Installed before document scripts. This is a wallpaper contract, not a privileged
// native bridge. HTML media volume is enforced; WebAudio authors consume the state
// event and set their own master gain. CoreWebView2_8 provides unconditional mute.
const wchar_t bootstrap[]=LR"JS(
(() => {
  let state=Object.freeze({paused:true,muted:true,volume:0.5,fps:60});
  const updateMedia=() => document.querySelectorAll('audio,video').forEach(m => {
    m.muted=state.muted; m.volume=state.volume;
  });
  Object.defineProperty(window,'wallpaperState',{get:()=>state,configurable:false});
  Object.defineProperty(window,'__auroraUpdate',{value:next=>{
    state=Object.freeze(next); updateMedia();
    window.dispatchEvent(new CustomEvent('wallpaperstate',{detail:state}));
  },writable:false,configurable:false});
  document.addEventListener('DOMContentLoaded',()=>{
    updateMedia(); new MutationObserver(updateMedia).observe(document.documentElement,{childList:true,subtree:true});
  });
})();
)JS";
void configure(const std::shared_ptr<WebState>& s) {
    check(s->composition.As(&s->controller),"Query WebView controller");
    check(s->controller->get_CoreWebView2(&s->web),"get_CoreWebView2");
    check(s->web.As(&s->lifecycle),"WebView2 lifecycle API unavailable; update the runtime");
    check(s->web.As(&s->audio),"WebView2 mute API unavailable; update the runtime");
    s->audio->put_IsMuted(TRUE);
    s->controller->put_IsVisible(FALSE);
    ComPtr<ICoreWebView2Controller3> scaled;
    if (SUCCEEDED(s->controller.As(&scaled))) scaled->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS);
    ComPtr<ICoreWebView2Settings> settings; check(s->web->get_Settings(&settings),"get_Settings");
    settings->put_AreDefaultContextMenusEnabled(FALSE); settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreHostObjectsAllowed(FALSE); settings->put_IsWebMessageEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE); settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    ComPtr<ICoreWebView2Settings3> settings3;
    if (SUCCEEDED(settings.As(&settings3))) settings3->put_AreBrowserAcceleratorKeysEnabled(FALSE);
    ComPtr<ICoreWebView2Settings4> settings4;
    if (SUCCEEDED(settings.As(&settings4))) { settings4->put_IsPasswordAutosaveEnabled(FALSE); settings4->put_IsGeneralAutofillEnabled(FALSE); }
    const auto root=QDir::toNativeSeparators(s->wallpaper.root).toStdWString();
    const auto host=s->host.toStdWString();
    check(s->lifecycle->SetVirtualHostNameToFolderMapping(host.c_str(),root.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS),"Map local web bundle");

    auto weak=s->weak_from_this(); EventRegistrationToken token{};
    check(s->web->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
            LPWSTR uri{}; args->get_Uri(&uri);
            auto s=weak.lock(); bool allowed=s && s->alive && s->local(QString::fromWCharArray(uri?uri:L""));
            CoTaskMemFree(uri); if (!allowed) args->put_Cancel(TRUE); return S_OK;
        }).Get(),&token),"Navigation handler");
    s->web->add_FrameNavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>(
        [weak](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* args)->HRESULT {
            LPWSTR uri{}; args->get_Uri(&uri); auto s=weak.lock();
            bool allowed=s && s->alive && s->local(QString::fromWCharArray(uri?uri:L""));
            CoTaskMemFree(uri); if (!allowed) args->put_Cancel(TRUE); return S_OK;
        }).Get(),&token);
    s->web->add_NewWindowRequested(Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* args)->HRESULT { args->put_Handled(TRUE); return S_OK; }).Get(),&token);
    s->web->add_PermissionRequested(Callback<ICoreWebView2PermissionRequestedEventHandler>(
        [](ICoreWebView2*,ICoreWebView2PermissionRequestedEventArgs* args)->HRESULT {
            args->put_State(COREWEBVIEW2_PERMISSION_STATE_DENY); return S_OK;
        }).Get(),&token);
    ComPtr<ICoreWebView2_4> downloads;
    check(s->web.As(&downloads),"Query download API");
    downloads->add_DownloadStarting(Callback<ICoreWebView2DownloadStartingEventHandler>(
        [](ICoreWebView2*,ICoreWebView2DownloadStartingEventArgs* args)->HRESULT { args->put_Cancel(TRUE); args->put_Handled(TRUE); return S_OK; }).Get(),&token);
    check(s->web->AddWebResourceRequestedFilter(L"*",COREWEBVIEW2_WEB_RESOURCE_CONTEXT_ALL),"Web resource filter");
    s->web->add_WebResourceRequested(Callback<ICoreWebView2WebResourceRequestedEventHandler>(
        [weak](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* args)->HRESULT {
            auto s=weak.lock(); if (!s || !s->alive) return S_OK;
            ComPtr<ICoreWebView2WebResourceRequest> request; args->get_Request(&request);
            LPWSTR raw{}; if (request) request->get_Uri(&raw);
            QString uri=QString::fromWCharArray(raw?raw:L""); CoTaskMemFree(raw);
            bool allowed=s->local(uri) || uri.startsWith("data:") || uri.startsWith("blob:https://"+s->host+"/");
            if (!allowed) {
                ComPtr<ICoreWebView2WebResourceResponse> response;
                if (SUCCEEDED(s->environment->CreateWebResourceResponse(nullptr,403,L"Offline wallpaper",L"Content-Type: text/plain",&response))) args->put_Response(response.Get());
            }
            return S_OK;
        }).Get(),&token);
    s->web->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>(
        [weak](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT {
            if (auto s=weak.lock()) s->fail("The web renderer process failed. Reapply the wallpaper to restart it."); return S_OK;
        }).Get(),&token);
    s->web->add_NavigationCompleted(Callback<ICoreWebView2NavigationCompletedEventHandler>(
        [weak](ICoreWebView2*,ICoreWebView2NavigationCompletedEventArgs* args)->HRESULT {
            auto s=weak.lock(); if (!s || !s->alive) return S_OK;
            BOOL success{}; args->get_IsSuccess(&success);
            if (!success) { s->fail("The web bundle's entry page could not load."); return S_OK; }
            s->ready=true; s->synchronize(); return S_OK;
        }).Get(),&token);

    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        nullptr,0,D3D11_SDK_VERSION,&s->graphics,nullptr,nullptr),"Web composition D3D11 device");
    ComPtr<IDXGIDevice> dxgi; check(s->graphics.As(&dxgi),"Web composition DXGI device");
    check(DCompositionCreateDevice(dxgi.Get(),IID_PPV_ARGS(&s->dcomp)),"DCompositionCreateDevice");
    check(s->dcomp->CreateTargetForHwnd(s->hwnd,TRUE,&s->target),"Create web composition target");
    check(s->dcomp->CreateVisual(&s->visual),"Create web visual");
    check(s->target->SetRoot(s->visual.Get()),"Set web visual root");
    check(s->composition->put_RootVisualTarget(s->visual.Get()),"Attach WebView2 visual");
    check(s->dcomp->Commit(),"Commit web composition"); s->size();
    check(s->web->AddScriptToExecuteOnDocumentCreated(bootstrap,Callback<ICoreWebView2AddScriptToExecuteOnDocumentCreatedCompletedHandler>(
        [weak](HRESULT hr,LPCWSTR)->HRESULT {
            auto s=weak.lock(); if (!s || !s->alive) return S_OK;
            if (FAILED(hr)) { s->fail("Cannot install wallpaper lifecycle script."); return S_OK; }
            QUrl url; url.setScheme("https"); url.setHost(s->host); url.setPath("/"+s->wallpaper.entry);
            auto uri=url.toString(QUrl::FullyEncoded).toStdWString();
            if (FAILED(s->web->Navigate(uri.c_str()))) s->fail("Web navigation failed.");
            return S_OK;
        }).Get()),"Install wallpaper script");
}
class WebRenderer final: public Renderer {
    std::shared_ptr<WebState> s_;
public:
    WebRenderer(HWND hwnd,const Wallpaper& w,const Playback& state,RenderError error):s_(std::make_shared<WebState>()) {
        auto s=s_; s->hwnd=hwnd; s->wallpaper=w; s->desired=state; s->error=std::move(error);
        s->host=w.id+".wallpaper.invalid";
        RECT rect{}; GetClientRect(hwnd,&rect); s->width=std::max(1L,rect.right); s->height=std::max(1L,rect.bottom);
        QString data=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/WebView2";
        QDir().mkpath(data); auto path=data.toStdWString(); auto weak=s->weak_from_this();
        auto hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,path.c_str(),nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [weak](HRESULT result,ICoreWebView2Environment* environment)->HRESULT {
                    auto s=weak.lock(); if (!s || !s->alive) return S_OK;
                    try {
                        check(result,"Create WebView2 environment (install Evergreen Runtime)");
                        if (!environment) throw std::runtime_error("WebView2 environment is null.");
                        s->environment=environment;
                        ComPtr<ICoreWebView2Environment3> environment3; check(s->environment.As(&environment3),"Query composition environment");
                        check(environment3->CreateCoreWebView2CompositionController(s->hwnd,
                            Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                                [weak](HRESULT result,ICoreWebView2CompositionController* composition)->HRESULT {
                                    auto s=weak.lock();
                                    if (!s || !s->alive) {
                                        if (composition) { ComPtr<ICoreWebView2Controller> controller; if (SUCCEEDED(composition->QueryInterface(IID_PPV_ARGS(&controller)))) controller->Close(); }
                                        return S_OK;
                                    }
                                    try {
                                        check(result,"Create WebView2 composition controller");
                                        if (!composition) throw std::runtime_error("WebView2 controller is null.");
                                        s->composition=composition; configure(s);
                                    } catch (const std::exception& e) { s->fail(QString::fromUtf8(e.what())); }
                                    return S_OK;
                                }).Get()),"Start WebView2 composition controller");
                    } catch (const std::exception& e) { s->fail(QString::fromUtf8(e.what())); }
                    return S_OK;
                }).Get());
        check(hr,"Start WebView2 environment (install Evergreen Runtime)");
    }
    ~WebRenderer() override { s_->close(); }
    void resize(int width,int height) override { s_->width=width; s_->height=height; s_->size(); }
    void apply(const Playback& state) override {
        if (state.paused && !s_->desired.paused) pointer({0,0,false,false,true});
        s_->desired=state; s_->synchronize();
    }
    void pointer(const Pointer& point) override {
        auto& s=*s_;
        if (!s.ready || !s.composition) return;
        POINT position{point.x,point.y};
        if (point.leave) {
            if (s.leftHeld) s.composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,0,position);
            s.leftHeld=false;
            s.composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,0,position); return;
        }
        if (s.desired.paused) return;
        auto keys=s.leftHeld?COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON:COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE;
        s.composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,keys,0,position);
        if (point.leftDown) { s.leftHeld=true; s.composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_LEFT_BUTTON,0,position); }
        if (point.leftUp) { s.leftHeld=false; s.composition->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,0,position); }
    }
};
}
std::unique_ptr<Renderer> makeWeb(HWND hwnd,const Wallpaper& w,const Playback& state,RenderError error) {
    return std::make_unique<WebRenderer>(hwnd,w,state,std::move(error));
}
}
