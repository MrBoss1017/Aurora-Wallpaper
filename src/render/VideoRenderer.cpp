#include "Renderer.h"
#include <QCoreApplication>
#include <QDir>
#include <QTimer>
#include <mutex>
#include <iterator>

namespace aurora {
namespace {
// Public libVLC 3.x C ABI. Opaque objects keep video optional at startup and avoid
// binding to a compiler-specific import library. No private VLC structures are used.
struct libvlc_instance_t;
struct libvlc_media_t;
struct libvlc_media_player_t;
struct libvlc_media_list_t;
struct libvlc_media_list_player_t;
class Vlc {
    HMODULE module_{};
    DLL_DIRECTORY_COOKIE dllDirectory_{};
    template<class T> void load(T& field,const char* name) {
        field=reinterpret_cast<T>(GetProcAddress(module_,name));
        if (!field) throw std::runtime_error(std::string("Missing libVLC 3.x symbol: ")+name);
    }
public:
    libvlc_instance_t* instance{};
    libvlc_instance_t* (__cdecl *create)(int,const char* const*){};
    void (__cdecl *release)(libvlc_instance_t*){};
    libvlc_media_t* (__cdecl *mediaNew)(libvlc_instance_t*,const char*){};
    void (__cdecl *mediaOption)(libvlc_media_t*,const char*){};
    void (__cdecl *mediaRelease)(libvlc_media_t*){};
    libvlc_media_player_t* (__cdecl *playerNew)(libvlc_instance_t*){};
    void (__cdecl *playerRelease)(libvlc_media_player_t*){};
    void (__cdecl *setHwnd)(libvlc_media_player_t*,void*){};
    void (__cdecl *setMouse)(libvlc_media_player_t*,unsigned){};
    void (__cdecl *setKeys)(libvlc_media_player_t*,unsigned){};
    void (__cdecl *setMute)(libvlc_media_player_t*,int){};
    int (__cdecl *setVolume)(libvlc_media_player_t*,int){};
    int (__cdecl *getState)(libvlc_media_player_t*){};
    libvlc_media_list_t* (__cdecl *listNew)(libvlc_instance_t*){};
    int (__cdecl *listAdd)(libvlc_media_list_t*,libvlc_media_t*){};
    void (__cdecl *listLock)(libvlc_media_list_t*){};
    void (__cdecl *listUnlock)(libvlc_media_list_t*){};
    void (__cdecl *listRelease)(libvlc_media_list_t*){};
    libvlc_media_list_player_t* (__cdecl *listPlayerNew)(libvlc_instance_t*){};
    void (__cdecl *listPlayerRelease)(libvlc_media_list_player_t*){};
    void (__cdecl *listSetPlayer)(libvlc_media_list_player_t*,libvlc_media_player_t*){};
    void (__cdecl *listSetMedia)(libvlc_media_list_player_t*,libvlc_media_list_t*){};
    void (__cdecl *listSetMode)(libvlc_media_list_player_t*,int){};
    void (__cdecl *play)(libvlc_media_list_player_t*){};
    void (__cdecl *setPause)(libvlc_media_list_player_t*,int){};
    void (__cdecl *stop)(libvlc_media_list_player_t*){};
    Vlc() {
        QString root=QCoreApplication::applicationDirPath()+"/vlc";
        auto directory=root.toStdWString();
        dllDirectory_=AddDllDirectory(directory.c_str());
        auto path=QDir::toNativeSeparators(root+"/libvlc.dll").toStdWString();
        module_=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module_) { if (dllDirectory_) RemoveDllDirectory(dllDirectory_); throw std::runtime_error("libVLC runtime missing. Run Setup-Dependencies.ps1 and rebuild/install."); }
        try {
#define VLC_BIND(field,name) load(field,"libvlc_" name)
            VLC_BIND(create,"new"); VLC_BIND(release,"release"); VLC_BIND(mediaNew,"media_new_path");
            VLC_BIND(mediaOption,"media_add_option"); VLC_BIND(mediaRelease,"media_release");
            VLC_BIND(playerNew,"media_player_new"); VLC_BIND(playerRelease,"media_player_release");
            VLC_BIND(setHwnd,"media_player_set_hwnd"); VLC_BIND(setMouse,"video_set_mouse_input"); VLC_BIND(setKeys,"video_set_key_input");
            VLC_BIND(setMute,"audio_set_mute"); VLC_BIND(setVolume,"audio_set_volume"); VLC_BIND(getState,"media_player_get_state");
            VLC_BIND(listNew,"media_list_new"); VLC_BIND(listAdd,"media_list_add_media"); VLC_BIND(listRelease,"media_list_release");
            VLC_BIND(listLock,"media_list_lock"); VLC_BIND(listUnlock,"media_list_unlock");
            VLC_BIND(listPlayerNew,"media_list_player_new"); VLC_BIND(listPlayerRelease,"media_list_player_release");
            VLC_BIND(listSetPlayer,"media_list_player_set_media_player"); VLC_BIND(listSetMedia,"media_list_player_set_media_list");
            VLC_BIND(listSetMode,"media_list_player_set_playback_mode"); VLC_BIND(play,"media_list_player_play");
            VLC_BIND(setPause,"media_list_player_set_pause"); VLC_BIND(stop,"media_list_player_stop");
#undef VLC_BIND
            qputenv("VLC_PLUGIN_PATH",QDir::toNativeSeparators(root+"/plugins").toUtf8());
            const char* args[]={"--intf=dummy","--no-video-title-show","--no-osd","--no-media-library",
                "--avcodec-hw=any","--vout=direct3d11","--no-sub-autodetect-file","--quiet"};
            instance=create(static_cast<int>(std::size(args)),args);
            if (!instance) throw std::runtime_error("libVLC could not initialize its plugins.");
        } catch (...) { FreeLibrary(module_); if (dllDirectory_) RemoveDllDirectory(dllDirectory_); throw; }
    }
    ~Vlc() { if (instance) release(instance); if (module_) FreeLibrary(module_); if (dllDirectory_) RemoveDllDirectory(dllDirectory_); }
};
std::shared_ptr<Vlc> sharedVlc() {
    static std::weak_ptr<Vlc> weak;
    auto vlc=weak.lock(); if (!vlc) { vlc=std::make_shared<Vlc>(); weak=vlc; } return vlc;
}
class VideoRenderer final: public Renderer {
    std::shared_ptr<Vlc> vlc_;
    libvlc_media_player_t* player_{};
    libvlc_media_list_player_t* listPlayer_{};
    QTimer timer_;
    Playback desired_;
    bool started_{},errorSent_{};
    RenderError error_;
    void cleanup() {
        timer_.stop();
        // Stop joins VLC's playback work before HWND destruction. Never call this
        // inside a VLC callback: it can deadlock. We use a Qt timer instead.
        if (listPlayer_) { vlc_->stop(listPlayer_); vlc_->listPlayerRelease(listPlayer_); listPlayer_=nullptr; }
        if (player_) { vlc_->playerRelease(player_); player_=nullptr; }
    }
    void synchronize() {
        vlc_->setMute(player_,desired_.muted?1:0);
        vlc_->setVolume(player_,desired_.volume);
        int state=vlc_->getState(player_);
        if (state==7 && !errorSent_) { errorSent_=true; if (error_) error_("Video decoding failed. Check the codec, file, and graphics driver."); }
        if (!desired_.paused && !started_) { vlc_->play(listPlayer_); started_=true; }
        // Explicit set_pause (not toggle) is idempotent. Retry after Opening/Buffering
        // so a policy change during asynchronous startup is not lost.
        if (started_ && (state==3 || state==4)) vlc_->setPause(listPlayer_,desired_.paused?1:0);
    }
public:
    VideoRenderer(HWND hwnd,const Wallpaper& w,const Playback& state,RenderError error):vlc_(sharedVlc()),desired_(state),error_(std::move(error)) {
        auto path=w.path().toUtf8(); auto media=vlc_->mediaNew(vlc_->instance,path.constData());
        if (!media) throw std::runtime_error("Cannot create video media.");
        auto list=vlc_->listNew(vlc_->instance);
        try {
            if (!list) throw std::runtime_error("Cannot create media list.");
            vlc_->mediaOption(media,":no-video-title-show");
            vlc_->listLock(list); int added=vlc_->listAdd(list,media); vlc_->listUnlock(list);
            if (added!=0) throw std::runtime_error("Cannot populate media list.");
            player_=vlc_->playerNew(vlc_->instance); listPlayer_=vlc_->listPlayerNew(vlc_->instance);
            if (!player_ || !listPlayer_) throw std::runtime_error("Cannot create video player.");
            vlc_->setHwnd(player_,hwnd); vlc_->setMouse(player_,0); vlc_->setKeys(player_,0);
            vlc_->setMute(player_,1); vlc_->setVolume(player_,0); // No unmuted startup burst.
            vlc_->listSetPlayer(listPlayer_,player_); vlc_->listSetMedia(listPlayer_,list);
            vlc_->listSetMode(listPlayer_,1); // libvlc_playback_mode_loop (one-item playlist).
            QObject::connect(&timer_,&QTimer::timeout,[this]{synchronize();}); timer_.start(250);
            synchronize();
        } catch (...) { cleanup(); if (list) vlc_->listRelease(list); vlc_->mediaRelease(media); throw; }
        vlc_->listRelease(list); vlc_->mediaRelease(media);
    }
    ~VideoRenderer() override { cleanup(); }
    void resize(int,int) override {} // Direct3D output follows its host HWND.
    void apply(const Playback& state) override { desired_=state; synchronize(); }
};
}
std::unique_ptr<Renderer> makeVideo(HWND hwnd,const Wallpaper& w,const Playback& state,RenderError error) {
    return std::make_unique<VideoRenderer>(hwnd,w,state,std::move(error));
}
}
