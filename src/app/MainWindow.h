#pragma once
#include "Engine.h"
#include <QMainWindow>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QAction>
namespace aurora {
class MainWindow:public QMainWindow {
    Library& library_;
    Engine& engine_;
    QSystemTrayIcon tray_;
    QListWidget* grid_{};
    QComboBox* displays_{};
    QCheckBox* span_{};
    QCheckBox* displayMute_{};
    QCheckBox* globalMute_{};
    QLabel* status_{};
    QLabel* error_{};
    QWidget* settingsPanel_{};
    QAction* pauseAction_{};
    QAction* muteAction_{};
    bool exiting_{},importing_{};
    void refreshGrid();
    void refreshDisplays();
    void applySelected();
    void import(bool web,Kind kind);
    void guarded(std::function<void()>);
    QString selectedDisplay() const;
protected:
    void closeEvent(QCloseEvent*) override;
    void changeEvent(QEvent*) override;
public:
    MainWindow(Library&,Engine&);
    ~MainWindow() override;
};
}
