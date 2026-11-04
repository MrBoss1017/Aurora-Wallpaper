#include "Renderer.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d3dcompiler.h>
#include <QFile>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <cstring>

namespace aurora {
namespace {
using namespace win;
using Clock=std::chrono::steady_clock;
constexpr char vertexSource[]=R"(
struct V { float4 position:SV_POSITION; float2 uv:TEXCOORD0; };
V VSMain(uint id:SV_VertexID) {
    V o; float2 p=float2((id<<1)&2,id&2);
    o.uv=p; o.position=float4(p.x*2-1,1-p.y*2,0,1); return o;
})";
ComPtr<ID3DBlob> compile(const char* source,std::size_t size,const char* entry,const char* target) {
    ComPtr<ID3DBlob> shader,errors;
    // No file include handler: an imported shader cannot read arbitrary local files.
    auto hr=D3DCompile(source,size,nullptr,nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);
    if (FAILED(hr)) throw std::runtime_error(errors?std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()):"D3DCompile failed");
    return shader;
}
struct Device {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swap;
    ComPtr<ID3D11RenderTargetView> target;
    ComPtr<ID3D11VertexShader> vertex;
    ComPtr<ID3D11PixelShader> pixel;
    ComPtr<ID3D11Buffer> constants;
    ComPtr<ID3D11RasterizerState> raster;
    int width{},height{};
    Device(HWND hwnd,ID3DBlob* vs,ID3DBlob* ps,int w,int h) {
        // Select the adapter owning this monitor; this avoids unnecessary copies on
        // multi-GPU systems. A span uses the adapter of the window's largest monitor.
        ComPtr<IDXGIFactory1> finder; check(CreateDXGIFactory1(IID_PPV_ARGS(&finder)),"CreateDXGIFactory1");
        ComPtr<IDXGIAdapter1> selected;
        HMONITOR wanted=MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST);
        for (UINT i=0;;++i) {
            ComPtr<IDXGIAdapter1> adapter;
            auto adapterResult=finder->EnumAdapters1(i,&adapter);
            if (adapterResult==DXGI_ERROR_NOT_FOUND) break;
            check(adapterResult,"EnumAdapters1");
            for (UINT j=0;;++j) {
                ComPtr<IDXGIOutput> output;
                auto outputResult=adapter->EnumOutputs(j,&output);
                if (outputResult==DXGI_ERROR_NOT_FOUND) break;
                check(outputResult,"EnumOutputs");
                DXGI_OUTPUT_DESC desc{};
                if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor==wanted) { selected=adapter; break; }
            }
            if (selected) break;
        }
        const D3D_FEATURE_LEVEL levels[]={D3D_FEATURE_LEVEL_11_0};
        check(D3D11CreateDevice(selected.Get(),selected?D3D_DRIVER_TYPE_UNKNOWN:D3D_DRIVER_TYPE_HARDWARE,nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT,levels,1,D3D11_SDK_VERSION,&device,nullptr,&context),"D3D11CreateDevice");
        ComPtr<IDXGIDevice> dxgi; check(device.As(&dxgi),"Query IDXGIDevice");
        ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter),"GetAdapter");
        ComPtr<IDXGIFactory2> factory; check(adapter->GetParent(IID_PPV_ARGS(&factory)),"Query IDXGIFactory2");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width=std::max(1,w); desc.Height=std::max(1,h); desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count=1; desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount=2; desc.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; desc.Scaling=DXGI_SCALING_STRETCH;
        check(factory->CreateSwapChainForHwnd(device.Get(),hwnd,&desc,nullptr,nullptr,&swap),"CreateSwapChainForHwnd");
        factory->MakeWindowAssociation(hwnd,DXGI_MWA_NO_ALT_ENTER);
        check(device->CreateVertexShader(vs->GetBufferPointer(),vs->GetBufferSize(),nullptr,&vertex),"CreateVertexShader");
        check(device->CreatePixelShader(ps->GetBufferPointer(),ps->GetBufferSize(),nullptr,&pixel),"CreatePixelShader");
        D3D11_BUFFER_DESC cb{}; cb.ByteWidth=32; cb.Usage=D3D11_USAGE_DYNAMIC;
        cb.BindFlags=D3D11_BIND_CONSTANT_BUFFER; cb.CPUAccessFlags=D3D11_CPU_ACCESS_WRITE;
        check(device->CreateBuffer(&cb,nullptr,&constants),"CreateBuffer");
        D3D11_RASTERIZER_DESC rd{}; rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
        check(device->CreateRasterizerState(&rd,&raster),"CreateRasterizerState");
        resize(w,h);
    }
    void resize(int w,int h) {
        w=std::max(1,w); h=std::max(1,h);
        context->OMSetRenderTargets(0,nullptr,nullptr); target.Reset();
        if (width && height) check(swap->ResizeBuffers(0,w,h,DXGI_FORMAT_UNKNOWN,0),"ResizeBuffers");
        ComPtr<ID3D11Texture2D> back;
        check(swap->GetBuffer(0,IID_PPV_ARGS(&back)),"GetBuffer");
        check(device->CreateRenderTargetView(back.Get(),nullptr,&target),"CreateRenderTargetView");
        width=w; height=h;
    }
    HRESULT draw(float time,float delta,float x,float y) {
        struct alignas(16) Constants { float resolution[2],time,delta,mouse[2],padding[2]; };
        static_assert(sizeof(Constants)==32);
        Constants data{{static_cast<float>(width),static_cast<float>(height)},time,delta,{x,y},{0,0}};
        D3D11_MAPPED_SUBRESOURCE mapping{};
        auto hr=context->Map(constants.Get(),0,D3D11_MAP_WRITE_DISCARD,0,&mapping);
        if (FAILED(hr)) return hr;
        std::memcpy(mapping.pData,&data,sizeof(data)); context->Unmap(constants.Get(),0);
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        context->RSSetViewports(1,&viewport); context->RSSetState(raster.Get());
        auto rtv=target.Get(); context->OMSetRenderTargets(1,&rtv,nullptr);
        context->IASetInputLayout(nullptr); context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex.Get(),nullptr,0); context->PSSetShader(pixel.Get(),nullptr,0);
        auto buffer=constants.Get(); context->PSSetConstantBuffers(0,1,&buffer);
        context->Draw(3,0);
        // DWM handles composition; the CPU deadline enforces the selected frame cap.
        return swap->Present(0,0);
    }
};
class ShaderRenderer final: public Renderer {
    HWND hwnd_;
    QByteArray source_;
    RenderError error_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Playback playback_;
    int width_=1,height_=1;
    float mouseX_{},mouseY_{};
    bool stop_{};
    std::thread thread_;
    void run() noexcept {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        try {
            auto vs=compile(vertexSource,sizeof(vertexSource)-1,"VSMain","vs_5_0");
            auto ps=compile(source_.constData(),static_cast<std::size_t>(source_.size()),"PSMain","ps_5_0");
            std::unique_ptr<Device> gpu;
            auto previous=Clock::now(); float time=0;
            int recoveries=0;
            while (true) {
                Playback state; int w,h; float mx,my;
                {
                    std::unique_lock lock(mutex_);
                    if (playback_.paused) {
                        cv_.wait(lock,[&]{return stop_ || !playback_.paused;});
                        previous=Clock::now(); // Paused wall time never advances shader time.
                    }
                    if (stop_) break;
                    state=playback_; w=width_; h=height_; mx=mouseX_; my=mouseY_;
                }
                auto start=Clock::now();
                if (!gpu) gpu=std::make_unique<Device>(hwnd_,vs.Get(),ps.Get(),w,h);
                if (w!=gpu->width || h!=gpu->height) gpu->resize(w,h);
                float delta=std::clamp(std::chrono::duration<float>(start-previous).count(),0.f,.1f);
                previous=start; time+=delta;
                auto hr=gpu->draw(time,delta,mx,my);
                if (hr==DXGI_ERROR_DEVICE_REMOVED || hr==DXGI_ERROR_DEVICE_RESET) {
                    gpu.reset();
                    if (++recoveries>3) throw std::runtime_error("Direct3D device repeatedly lost; reapply the wallpaper after checking the driver.");
                    std::unique_lock lock(mutex_); cv_.wait_for(lock,std::chrono::seconds(1),[&]{return stop_;}); continue;
                }
                check(hr,"Present");
                // No busy loop. Paused instances sleep indefinitely; occluded surfaces
                // back off even when the higher-level policy has not caught up yet.
                auto period=std::chrono::microseconds(hr==DXGI_STATUS_OCCLUDED?250000:1000000/std::max(1,state.fps));
                std::unique_lock lock(mutex_);
                cv_.wait_until(lock,start+period,[&]{return stop_ || playback_.paused || playback_.fps!=state.fps;});
                if (stop_) break;
            }
        } catch (const std::exception& e) { if (error_) error_(QString::fromUtf8(e.what())); }
    }
public:
    ShaderRenderer(HWND hwnd,const Wallpaper& w,const Playback& state,RenderError error):hwnd_(hwnd),error_(std::move(error)),playback_(state) {
        QFile file(w.path());
        if (!file.open(QIODevice::ReadOnly) || file.size()>1024*1024) throw std::runtime_error("Cannot read HLSL source (1 MiB maximum).");
        source_=file.readAll(); RECT r{}; GetClientRect(hwnd,&r); width_=std::max(1L,r.right); height_=std::max(1L,r.bottom);
        thread_=std::thread([this]{run();});
    }
    ~ShaderRenderer() override {
        { std::lock_guard lock(mutex_); stop_=true; } cv_.notify_all();
        if (thread_.joinable()) thread_.join();
    }
    void resize(int w,int h) override { std::lock_guard lock(mutex_); width_=w; height_=h; cv_.notify_all(); }
    void apply(const Playback& state) override { std::lock_guard lock(mutex_); playback_=state; cv_.notify_all(); }
    void pointer(const Pointer& p) override { std::lock_guard lock(mutex_); mouseX_=static_cast<float>(p.x); mouseY_=static_cast<float>(p.y); }
};
}
std::unique_ptr<Renderer> makeShader(HWND hwnd,const Wallpaper& w,const Playback& state,RenderError error) {
    return std::make_unique<ShaderRenderer>(hwnd,w,state,std::move(error));
}
}
