#pragma once
#include "core/Policy.h"
#include <QString>
#include <QMap>
#include <QVector>
#include <optional>
namespace aurora {
struct Wallpaper {
    QString id,title,root,entry,thumbnail;
    Kind kind=Kind::Shader;
    int width{},height{};
    QString path() const;
};
struct Settings {
    Rules rules;
    bool muted=true, interactive=false, span=false;
    QString spanWallpaper;
    QMap<QString,QString> assignments;
    QMap<QString,bool> displayMuted;
};
class Library {
    QString directory_;
    QVector<Wallpaper> entries_;
    void saveLibrary() const;
public:
    Settings settings;
    Library();
    const QVector<Wallpaper>& entries() const { return entries_; }
    QString directory() const { return directory_; }
    const Wallpaper* find(const QString& id) const;
    Wallpaper importFile(const QString& path, Kind kind, const QString& title = {});
    Wallpaper importWebFolder(const QString& directory);
    void saveSettings() const;
    void importSamples();
    void acceptImportedEntries(const Library& imported) { entries_=imported.entries_; }
};
QString kindName(Kind kind);
}
