#include "Engine.h"
#include <QPointer>
#include <QDebug>
#include <limits>
namespace aurora {
Engine::Engine(Library& library,QObject* parent):QObject(parent),library_(library) {
    clock_.start(); activity_.setSingleShot(true); activity_.setInterval(75);
    topology_.setSingleShot(true); topology_.setInterval(350);
    inputTimer_.setSingleShot(true); inputTimer_.setInterval(16);
    connect(&activity_,&QTimer::timeout,this,[this]{evaluatePolicies();});
    connect(&topology_,&QTimer::timeout,this,[this]{rebuild();});
    connect(&inputTimer_,&QTimer::timeout,this,[this]{if (pendingMove_) { dispatchMouse(*pendingMove_); pendingMove_.reset(); }});
    connect(&timer_,&QTimer::timeout,this,[this] {
        bool invalid=false;
        for (const auto& instance:instances_) invalid |= !instance->host->healthy();
        bool wanted=library_.settings.span?!library_.settings.spanWallpaper.isEmpty():!library_.settings.assignments.isEmpty();
        if ((invalid || (wanted && instances_.empty())) && clock_.elapsed()-lastRetry_>=5000) rebuild();
        else evaluatePolicies();
        // Deliver LEAVE even if the desktop stops receiving raw packets (another
        // application takes focus, a menu opens, or a button is released elsewhere).
        POINT p{};
        if (GetPhysicalCursorPos(&p) && (!win::isDesktopSurface(GetForegroundWindow()) || !win::isDesktopSurface(WindowFromPoint(p)))) {
            for (auto& instance:instances_) if (instance->pointerInside && instance->renderer) {
                instance->renderer->pointer({0,0,false,false,true}); instance->pointerInside=false;
            }
        }
    });
    events_.changed=[this](win::DesktopEvent event) {
        if (event==win::DesktopEvent::ShellRestart || event==win::DesktopEvent::Topology) topology_.start();
        if (!activity_.isActive()) activity_.start();
    };
    events_.mouse=[this](win::MouseEvent event) {
        if (event.leftDown || event.leftUp) { pendingMove_.reset(); dispatchMouse(event); }
        else { pendingMove_=event; if (!inputTimer_.isActive()) inputTimer_.start(); }
    };
}
Engine::~Engine() { stop(); }
void Engine::start() { timer_.start(1000); rebuild(); }
void Engine::stop() {
    timer_.stop(); activity_.stop(); topology_.stop(); inputTimer_.stop(); events_.changed={}; events_.mouse={};
    events_.enableInput(false); ++generation_; instances_.clear();
}
Playback Engine::desired(const Instance& instance,const win::WindowSnapshot& snapshot) const {
    Environment env;
    env.manualPause=manualPause_; env.battery=win::onBattery(); env.locked=events_.locked || events_.suspended;
    env.displayOff=events_.displayOff; env.globalMute=library_.settings.muted;
    env.refreshHz=240;
    // A span is one rendering surface: any constituent monitor's blocking policy
    // pauses it; the slowest refresh rate caps it. Independent mode evaluates each.
    for (const auto& monitor:instance.displays) {
        env.fullscreen |= snapshot.hasForeground && covers(snapshot.foreground,monitor.bounds);
        env.covered=std::max(env.covered,coverage(monitor.bounds,snapshot.windows));
        env.refreshHz=std::min(env.refreshHz,monitor.refreshHz);
        env.displayMute |= library_.settings.displayMuted.value(QString::fromStdWString(monitor.id),false);
    }
    return evaluate(instance.wallpaper.kind,library_.settings.rules,env);
}
void Engine::add(const win::DesktopLayer& layer,QString key,Wallpaper wallpaper,std::vector<win::Monitor> monitors,Rect bounds) {
    auto instance=std::make_unique<Instance>();
    instance->key=key; instance->wallpaper=std::move(wallpaper); instance->bounds=bounds; instance->displays=std::move(monitors);
    instance->host=std::make_unique<win::DesktopHost>(layer,bounds);
    instance->state=desired(*instance,win::sampleWindows());
    auto guard=QPointer<Engine>(this); auto generation=generation_;
    RenderError onError=[guard,generation,key](QString message) {
        if (!guard) return;
        // Shader errors originate on a worker; web/VLC errors are also deferred so
        // disposal never occurs inside an active renderer/native callback.
        QMetaObject::invokeMethod(guard.data(),[guard,generation,key,message] {
            if (!guard || guard->generation_!=generation) return;
            for (auto& instance:guard->instances_) if (instance->key==key) {
                instance->failed=true; instance->renderer.reset(); break;
            }
            qWarning().noquote()<<message;
            if (guard->error) guard->error(message);
        },Qt::QueuedConnection);
    };
    switch (instance->wallpaper.kind) {
        case Kind::Video: instance->renderer=makeVideo(instance->host->handle(),instance->wallpaper,instance->state,onError); break;
        case Kind::Web: instance->renderer=makeWeb(instance->host->handle(),instance->wallpaper,instance->state,onError); break;
        case Kind::Shader: instance->renderer=makeShader(instance->host->handle(),instance->wallpaper,instance->state,onError); break;
    }
    auto [width,height]=instance->host->clientSize(); instance->renderer->resize(width,height);
    instance->host->show(); instances_.push_back(std::move(instance));
}
void Engine::rebuild() {
    if (rebuilding_) return;
    rebuilding_=true; ++generation_; lastRetry_=clock_.elapsed(); instances_.clear();
    try {
        monitors_=win::enumerateMonitors();
        if (displaysChanged) displaysChanged();
        if (monitors_.empty()) throw std::runtime_error("No active displays; waiting for a display to connect.");
        bool wanted=library_.settings.span?!library_.settings.spanWallpaper.isEmpty():!library_.settings.assignments.isEmpty();
        if (wanted) {
            auto layer=win::discoverDesktopLayer();
            if (library_.settings.span) {
                if (const auto* w=library_.find(library_.settings.spanWallpaper)) {
                    Rect bounds=monitors_.front().bounds;
                    for (const auto& m:monitors_) {
                        bounds.left=std::min(bounds.left,m.bounds.left); bounds.top=std::min(bounds.top,m.bounds.top);
                        bounds.right=std::max(bounds.right,m.bounds.right); bounds.bottom=std::max(bounds.bottom,m.bounds.bottom);
                    }
                    add(layer,"span",*w,monitors_,bounds);
                }
            } else for (const auto& monitor:monitors_) {
                QString key=QString::fromStdWString(monitor.id);
                if (const auto* w=library_.find(library_.settings.assignments.value(key))) {
                    try { add(layer,key,*w,{monitor},monitor.bounds); }
                    catch (const std::exception& e) { if (error) error(QString::fromUtf8(e.what())); }
                }
            }
        }
        events_.enableInput(library_.settings.interactive);
    } catch (const std::exception& e) { qWarning()<<e.what(); if (error) error(QString::fromUtf8(e.what())); }
    rebuilding_=false; evaluatePolicies();
}
void Engine::evaluatePolicies() {
    if (rebuilding_) return;
    auto snapshot=win::sampleWindows(); int playing=0,paused=0,failed=0;
    for (auto& instance:instances_) {
        if (!instance->renderer || instance->failed) { ++failed; continue; }
        auto next=desired(*instance,snapshot);
        if (next.paused) instance->clearSince=-1;
        else if (instance->state.paused) {
            // Pause immediately; require 500 ms of continuously clear desktop to
            // resume. This prevents rapid pause/play oscillation during Alt-Tab.
            if (instance->clearSince<0) instance->clearSince=clock_.elapsed();
            if (clock_.elapsed()-instance->clearSince<500) { next.paused=true; next.muted=true; }
        }
        if (!(next==instance->state)) { instance->state=next; instance->renderer->apply(next); }
        if (next.paused) ++paused; else ++playing;
    }
    if (status) status(QString("%1 playing · %2 paused · %3 failed").arg(playing).arg(paused).arg(failed));
}
void Engine::dispatchMouse(win::MouseEvent event) {
    if (!library_.settings.interactive) return;
    // Recheck at dispatch time, since coalesced movement can outlive focus changes.
    if (!win::isDesktopSurface(GetForegroundWindow()) || !win::isDesktopSurface(WindowFromPoint(event.physical))) return;
    for (auto& instance:instances_) {
        if (!instance->renderer) continue;
        bool inside=instance->bounds.contains(event.physical.x,event.physical.y);
        if (inside) {
            try {
                auto point=win::physicalToClient(instance->host->handle(),event.physical);
                instance->renderer->pointer({point.x,point.y,event.leftDown,event.leftUp,false});
            } catch (const std::exception& e) { qWarning()<<e.what(); }
        } else if (instance->pointerInside) instance->renderer->pointer({0,0,false,false,true});
        instance->pointerInside=inside;
    }
}
void Engine::applySettings() {
    library_.saveSettings(); events_.enableInput(library_.settings.interactive);
    if (!library_.settings.interactive) for (auto& instance:instances_) if (instance->renderer) {
        instance->renderer->pointer({0,0,false,false,true}); instance->pointerInside=false;
    }
    evaluatePolicies();
}
void Engine::setPaused(bool value) { manualPause_=value; evaluatePolicies(); }
void Engine::skip() {
    if (library_.entries().empty()) return;
    auto next=[&](const QString& id) {
        for (qsizetype i=0;i<library_.entries().size();++i)
            if (library_.entries()[i].id==id) return library_.entries()[(i+1)%library_.entries().size()].id;
        return library_.entries().front().id;
    };
    if (library_.settings.span) library_.settings.spanWallpaper=next(library_.settings.spanWallpaper);
    else for (const auto& monitor:monitors_) {
        auto key=QString::fromStdWString(monitor.id); library_.settings.assignments[key]=next(library_.settings.assignments.value(key));
    }
    library_.saveSettings(); rebuild();
}
}
