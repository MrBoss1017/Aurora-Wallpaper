#include "Library.h"
#include "platform/win32/Win32.h"
#include <shobjidl.h>
#include <propkey.h>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QFile>
#include <QSaveFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QDebug>
#include <stdexcept>
namespace aurora {
namespace {
void error(const QString& message) { throw std::runtime_error(message.toStdString()); }
QJsonDocument readJson(const QString& path) {
    QFile f(path);
    if (!f.exists()) return {};
    if (!f.open(QIODevice::ReadOnly)) error("Cannot read "+path);
    if (f.size()>4*1024*1024) error("JSON metadata too large: "+path);
    QJsonParseError parse;
    auto doc=QJsonDocument::fromJson(f.readAll(),&parse);
    if (parse.error!=QJsonParseError::NoError) error("Invalid JSON in "+path+": "+parse.errorString());
    return doc;
}
void atomicJson(const QString& path, const QJsonDocument& doc) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) error("Cannot write "+path);
    const auto bytes=doc.toJson();
    if (file.write(bytes)!=bytes.size() || !file.commit()) error("Cannot commit "+path);
}
bool safeRelative(const QString& value) {
    if (value.isEmpty() || QDir::isAbsolutePath(value) || value.contains(':') || value.contains('\\')) return false;
    for (const auto& segment:value.split('/')) if (segment==".." || segment=="." || segment.isEmpty()) return false;
    return true;
}
QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
void copyTree(const QString& source,const QString& dest) {
    QDirIterator it(source,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDirIterator::Subdirectories);
    qint64 bytes=0; int count=0;
    while (it.hasNext()) {
        it.next(); auto info=it.fileInfo();
        if (info.isSymLink() || info.isJunction()) error("Web bundles cannot contain symlinks or junctions.");
        if (++count>20000 || (info.isFile() && (bytes+=info.size())>2LL*1024*1024*1024)) error("Web bundle exceeds the 20,000-file / 2 GiB limit.");
        QString relative=QDir(source).relativeFilePath(info.absoluteFilePath());
        if (!safeRelative(relative)) error("Invalid bundle path.");
        QString target=QDir(dest).filePath(relative);
        if (info.isDir()) { if (!QDir().mkpath(target)) error("Cannot create bundle directory."); }
        else if (!info.isFile() || !QFile::copy(info.absoluteFilePath(),target)) error("Cannot copy "+relative);
    }
}
ResourceMode mode(int value) { return static_cast<ResourceMode>(std::clamp(value,0,2)); }
}
QString Wallpaper::path() const { return QDir(root).filePath(entry); }
QString kindName(Kind kind) { return kind==Kind::Video?"Video":kind==Kind::Web?"Web":"Shader"; }
Library::Library() {
    directory_=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(directory_+"/wallpapers")) error("Cannot create local wallpaper library.");
    auto library=readJson(directory_+"/library.json").array();
    for (auto value:library) {
        auto o=value.toObject(); QString id=o["id"].toString(),entry=o["entry"].toString();
        if (QUuid(id).isNull() || !safeRelative(entry)) continue;
        Wallpaper w{id,o["title"].toString(),directory_+"/wallpapers/"+id,entry,{},
            static_cast<Kind>(std::clamp(o["kind"].toInt(),0,2)),o["width"].toInt(),o["height"].toInt()};
        auto thumbnail=o["thumbnail"].toString();
        if (safeRelative(thumbnail)) w.thumbnail=thumbnail;
        if (QFileInfo::exists(w.path())) entries_.push_back(w);
    }
    auto s=readJson(directory_+"/settings.json").object();
    settings.muted=s["muted"].toBool(true); settings.interactive=s["interactive"].toBool(false);
    settings.span=s["span"].toBool(false); settings.spanWallpaper=s["spanWallpaper"].toString();
    auto& r=settings.rules;
    r.fps=std::clamp(s["fps"].toInt(60),1,240); r.volume=std::clamp(s["volume"].toInt(50),0,100);
    r.ecoFps=std::clamp(s["ecoFps"].toInt(15),1,60);
    r.pauseFullscreen=s["pauseFullscreen"].toBool(true);
    r.battery=mode(s["battery"].toInt(2)); r.overlap=mode(s["overlap"].toInt(2));
    r.overlapThreshold=std::clamp(s["overlapThreshold"].toDouble(.90),.01,1.0);
    auto assignments=s["assignments"].toObject();
    for (auto it=assignments.begin();it!=assignments.end();++it) settings.assignments[it.key()]=it.value().toString();
    auto mutes=s["displayMuted"].toObject();
    for (auto it=mutes.begin();it!=mutes.end();++it) settings.displayMuted[it.key()]=it.value().toBool();
}
const Wallpaper* Library::find(const QString& id) const {
    for (const auto& entry:entries_) if (entry.id==id) return &entry;
    return nullptr;
}
void Library::saveLibrary() const {
    QJsonArray array;
    for (const auto& w:entries_) array.append(QJsonObject{{"id",w.id},{"title",w.title},{"entry",w.entry},{"kind",static_cast<int>(w.kind)},
        {"thumbnail",w.thumbnail},{"width",w.width},{"height",w.height}});
    atomicJson(directory_+"/library.json",QJsonDocument(array));
}
void Library::saveSettings() const {
    QJsonObject assignments,mutes;
    for (auto it=settings.assignments.begin();it!=settings.assignments.end();++it) assignments[it.key()]=it.value();
    for (auto it=settings.displayMuted.begin();it!=settings.displayMuted.end();++it) mutes[it.key()]=it.value();
    const auto& r=settings.rules;
    atomicJson(directory_+"/settings.json",QJsonDocument(QJsonObject{
        {"schema",1},{"muted",settings.muted},{"interactive",settings.interactive},{"span",settings.span},
        {"spanWallpaper",settings.spanWallpaper},{"assignments",assignments},{"displayMuted",mutes},
        {"fps",r.fps},{"ecoFps",r.ecoFps},{"volume",r.volume},{"pauseFullscreen",r.pauseFullscreen},
        {"battery",static_cast<int>(r.battery)},{"overlap",static_cast<int>(r.overlap)},{"overlapThreshold",r.overlapThreshold}}));
}
Wallpaper Library::importFile(const QString& path,Kind kind,const QString& title) {
    QFileInfo file(path);
    QString ext=file.suffix().toLower();
    if (!file.isFile() || file.isSymLink() || (kind==Kind::Shader && ext!="hlsl") ||
        (kind==Kind::Video && ext!="mp4" && ext!="webm") || kind==Kind::Web) error("Choose an MP4, WebM, or HLSL file of the selected type.");
    if (file.size()>(kind==Kind::Shader?1024LL*1024:8LL*1024*1024*1024)) error("File is too large (HLSL: 1 MiB; video: 8 GiB).");
    Wallpaper w; w.id=newId(); w.title=title.isEmpty()?file.completeBaseName():title; w.kind=kind;
    w.root=directory_+"/wallpapers/"+w.id; w.entry=file.fileName();
    if (!QDir().mkpath(w.root)) error("Cannot create wallpaper folder.");
    try {
        if (!QFile::copy(path,w.path())) error("Cannot copy wallpaper file.");
        for (auto thumb:{QString("preview.png"),QString("preview.jpg")}) if (QFileInfo::exists(file.dir().filePath(thumb))) {
            if (QFile::copy(file.dir().filePath(thumb),QDir(w.root).filePath(thumb))) w.thumbnail=thumb;
            break;
        }
        if (kind==Kind::Video) {
            win::ComPtr<IShellItem2> item;
            if (SUCCEEDED(SHCreateItemFromParsingName(reinterpret_cast<LPCWSTR>(path.utf16()),nullptr,IID_PPV_ARGS(&item)))) {
                ULONG width{},height{}; item->GetUInt32(PKEY_Video_FrameWidth,&width); item->GetUInt32(PKEY_Video_FrameHeight,&height);
                w.width=static_cast<int>(width); w.height=static_cast<int>(height);
            }
        }
        entries_.push_back(w);
        try { saveLibrary(); } catch (...) { entries_.pop_back(); throw; }
    } catch (...) { QDir(w.root).removeRecursively(); throw; }
    return w;
}
Wallpaper Library::importWebFolder(const QString& directory) {
    QFileInfo source(directory);
    if (!source.isDir() || source.isSymLink() || source.isJunction()) error("Choose a regular web bundle folder.");
    auto manifest=readJson(QDir(directory).filePath("wallpaper.json")).object();
    QString entry=manifest["entry"].toString("index.html"),thumbnail=manifest["thumbnail"].toString("preview.png");
    if (!safeRelative(entry) || !QFileInfo::exists(QDir(directory).filePath(entry))) error("Web bundle needs an index.html or a valid wallpaper.json entry.");
    Wallpaper w; w.id=newId(); w.kind=Kind::Web; w.title=manifest["title"].toString(source.fileName());
    w.root=directory_+"/wallpapers/"+w.id; w.entry=entry;
    w.width=std::clamp(manifest["width"].toInt(),0,32768); w.height=std::clamp(manifest["height"].toInt(),0,32768);
    if (safeRelative(thumbnail) && QFileInfo::exists(QDir(directory).filePath(thumbnail))) w.thumbnail=thumbnail;
    if (!QDir().mkpath(w.root)) error("Cannot create wallpaper folder.");
    try {
        copyTree(directory,w.root); entries_.push_back(w);
        try { saveLibrary(); } catch (...) { entries_.pop_back(); throw; }
    } catch (...) { QDir(w.root).removeRecursively(); throw; }
    return w;
}
void Library::importSamples() {
    // A marker prevents re-importing samples every time the app starts.
    QString marker=directory_+"/samples-v1.installed";
    if (QFileInfo::exists(marker)) return;
    QString samples=QCoreApplication::applicationDirPath()+"/samples";
    if (!QFileInfo::exists(samples+"/aurora/aurora.hlsl")) return;
    bool hasShader=false,hasWeb=false;
    for (const auto& entry:entries_) {
        hasShader |= entry.kind==Kind::Shader && entry.title=="Aurora Ribbon";
        hasWeb |= entry.kind==Kind::Web && entry.title=="Quiet Orbit";
    }
    if (!hasShader) importFile(samples+"/aurora/aurora.hlsl",Kind::Shader,"Aurora Ribbon");
    if (!hasWeb) importWebFolder(samples+"/web-orbit");
    QFile file(marker); if (file.open(QIODevice::WriteOnly)) file.write("1");
}
}
