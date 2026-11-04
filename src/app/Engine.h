#pragma once
#include "Library.h"
#include "platform/win32/DesktopHost.h"
#include "platform/win32/DesktopEvents.h"
#include "render/Renderer.h"
#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <memory>
#include <vector>
namespace aurora {
class Engine:public QObject {
    struct Instance {
        QString key;
        Wallpaper wallpaper;
        Rect bounds;
        std::vector<win::Monitor> displays;
        std::unique_ptr<win::DesktopHost> host;
        std::unique_ptr<Renderer> renderer; // Declared after host: destroyed first.
        Playback state;
        qint64 clearSince=-1;
        bool pointerInside{},failed{};
    };
    Library& library_;
    win::DesktopEvents events_;
    std::vector<win::Monitor> monitors_;
    std::vector<std::unique_ptr<Instance>> instances_;
    QTimer timer_,activity_,topology_,inputTimer_;
    QElapsedTimer clock_;
    bool manualPause_{},rebuilding_{};
    std::uint64_t generation_{};
    qint64 lastRetry_=-5000;
    std::optional<win::MouseEvent> pendingMove_;
    void evaluatePolicies();
    void dispatchMouse(win::MouseEvent);
    Playback desired(const Instance&,const win::WindowSnapshot&) const;
    void add(const win::DesktopLayer&,QString,Wallpaper,std::vector<win::Monitor>,Rect);
public:
    std::function<void(QString)> status;
    std::function<void(QString)> error;
    std::function<void()> displaysChanged;
    explicit Engine(Library&,QObject* parent=nullptr);
    ~Engine() override;
    const std::vector<win::Monitor>& monitors() const { return monitors_; }
    void start();
    void rebuild();
    void applySettings();
    void setPaused(bool);
    bool paused() const { return manualPause_; }
    void skip();
    void stop();
};
}
