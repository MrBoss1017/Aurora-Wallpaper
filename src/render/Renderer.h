#pragma once
#include "app/Library.h"
#include "platform/win32/Win32.h"
#include <functional>
#include <memory>
namespace aurora {
struct Pointer { int x{},y{}; bool leftDown{},leftUp{},leave{}; };
using RenderError=std::function<void(QString)>;
// All calls except a renderer's private worker execute on the UI's COM STA.
// Destruction is synchronous: worker resources are gone before the host HWND.
class Renderer {
public:
    virtual ~Renderer()=default;
    virtual void resize(int width,int height)=0;
    virtual void apply(const Playback&)=0;
    virtual void pointer(const Pointer&) {}
};
std::unique_ptr<Renderer> makeShader(HWND,const Wallpaper&,const Playback&,RenderError);
std::unique_ptr<Renderer> makeVideo(HWND,const Wallpaper&,const Playback&,RenderError);
std::unique_ptr<Renderer> makeWeb(HWND,const Wallpaper&,const Playback&,RenderError);
}
