#include "MainWindow.h"
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSlider>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QCloseEvent>
#include <QPainter>
#include <QLinearGradient>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QDir>

namespace aurora {
namespace {
QIcon appIcon() {
    QPixmap icon(64,64); icon.fill(Qt::transparent); QPainter p(&icon); p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0,0,64,64); gradient.setColorAt(0,QColor("#69dfcf")); gradient.setColorAt(1,QColor("#7463ff"));
    p.setBrush(gradient); p.setPen(Qt::NoPen); p.drawRoundedRect(1,1,62,62,18,18);
    p.setPen(QColor("#101322")); QFont font; font.setPixelSize(42); font.setBold(true); p.setFont(font); p.drawText(icon.rect(),Qt::AlignCenter,"A");
    return QIcon(icon);
}
QPixmap thumbnail(const Wallpaper& w) {
    QPixmap image;
    if (!w.thumbnail.isEmpty()) image.load(QDir(w.root).filePath(w.thumbnail));
    if (!image.isNull()) return image.scaled(224,126,Qt::KeepAspectRatioByExpanding,Qt::SmoothTransformation);
    image=QPixmap(224,126); image.fill(QColor("#17223a"));
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0,0,224,126);
    gradient.setColorAt(0,w.kind==Kind::Web?QColor("#124b65"):QColor("#372655"));
    gradient.setColorAt(1,w.kind==Kind::Video?QColor("#96634c"):QColor("#2a927c"));
    p.fillRect(image.rect(),gradient); p.setPen(QPen(QColor(255,255,255,60),1));
    for (int i=0;i<8;++i) p.drawEllipse(QPoint(160,80),20+i*17,20+i*12);
    p.setPen(QColor("#ffffff")); QFont font; font.setPixelSize(16); font.setBold(true); p.setFont(font);
    p.drawText(QRect(15,14,190,50),Qt::AlignLeft,kindName(w.kind).toUpper());
    font.setPixelSize(11); font.setBold(false); p.setFont(font);
    p.drawText(QRect(15,95,190,20),Qt::AlignLeft,"LOCAL WALLPAPER");
    return image;
}
struct ImportResult { std::optional<Library> library; QString error; };
}
void MainWindow::guarded(std::function<void()> action) {
    try { action(); } catch (const std::exception& e) { error_->setText(QString::fromUtf8(e.what())); error_->show(); }
}
MainWindow::MainWindow(Library& library,Engine& engine):library_(library),engine_(engine),tray_(this) {
    setWindowTitle("Aurora Wallpaper"); setWindowIcon(appIcon()); resize(1180,790); setMinimumSize(920,640);
    setStyleSheet(R"(
        QMainWindow,QWidget { background:#101522; color:#e6ecf7; font-family:'Segoe UI'; font-size:13px; }
        QGroupBox { border:1px solid #30384d; border-radius:10px; margin-top:16px; padding-top:18px; }
        QGroupBox::title { subcontrol-origin:margin; left:14px; color:#94e5d6; }
        QPushButton,QComboBox,QSpinBox { background:#20293d; border:1px solid #39465f; padding:8px; border-radius:6px; }
        QPushButton:hover { background:#30415a; }
        QPushButton#apply { background:#69dfcf; color:#101522; font-weight:bold; border:0; }
        QListWidget { border:0; background:#151c2c; border-radius:10px; padding:8px; }
        QListWidget::item { padding:10px; border:1px solid transparent; border-radius:8px; }
        QListWidget::item:selected { background:#283952; border:1px solid #69dfcf; }
        QMenu { border:1px solid #39465f; padding:5px; } QMenu::item { padding:8px 22px; }
        QMenu::item:selected { background:#30415a; }
    )");
    auto central=new QWidget; setCentralWidget(central); auto outer=new QVBoxLayout(central); outer->setContentsMargins(24,22,24,16); outer->setSpacing(16);
    auto header=new QHBoxLayout;
    auto title=new QLabel("Aurora <span style='color:#87a0bd;font-weight:normal'>/ Wallpaper Library</span>");
    title->setStyleSheet("font-size:25px;font-weight:600;"); header->addWidget(title); header->addStretch();
    auto importButton=new QPushButton("+ Import wallpaper"); auto importMenu=new QMenu(importButton);
    importMenu->addAction("Video · MP4 / WebM",this,[this]{import(false,Kind::Video);});
    importMenu->addAction("Web · local folder",this,[this]{import(true,Kind::Web);});
    importMenu->addAction("Shader · HLSL",this,[this]{import(false,Kind::Shader);});
    importButton->setMenu(importMenu); header->addWidget(importButton); outer->addLayout(header);
    auto hint=new QLabel("Living backgrounds. Quiet when you need your computer."); hint->setStyleSheet("color:#99abc5;"); outer->addWidget(hint);
    auto displayRow=new QHBoxLayout;
    displays_=new QComboBox; displays_->setMinimumWidth(360); displayRow->addWidget(displays_,1);
    displayMute_=new QCheckBox("Mute this display"); displayRow->addWidget(displayMute_);
    span_=new QCheckBox("Span all displays"); span_->setChecked(library_.settings.span); displayRow->addWidget(span_); outer->addLayout(displayRow);
    auto body=new QHBoxLayout; body->setSpacing(18);
    grid_=new QListWidget; grid_->setViewMode(QListView::IconMode); grid_->setResizeMode(QListView::Adjust);
    grid_->setMovement(QListView::Static); grid_->setIconSize({224,126}); grid_->setGridSize({252,204}); grid_->setSpacing(4); grid_->setWordWrap(true);
    body->addWidget(grid_,1);
    settingsPanel_=new QGroupBox("Playback & power"); settingsPanel_->setFixedWidth(290);
    auto form=new QFormLayout(settingsPanel_); form->setContentsMargins(16,28,16,16); form->setSpacing(15);
    auto fps=new QSpinBox; fps->setRange(1,240); fps->setValue(library_.settings.rules.fps); fps->setSuffix(" fps"); form->addRow("Frame limit",fps);
    auto volume=new QSlider(Qt::Horizontal); volume->setRange(0,100); volume->setValue(library_.settings.rules.volume); form->addRow("Volume",volume);
    globalMute_=new QCheckBox("Mute all wallpaper audio"); globalMute_->setChecked(library_.settings.muted); form->addRow(globalMute_);
    auto fullscreen=new QCheckBox("Pause for full-screen apps"); fullscreen->setChecked(library_.settings.rules.pauseFullscreen); form->addRow(fullscreen);
    auto resourceCombo=[](ResourceMode mode) { auto c=new QComboBox; c->addItems({"Keep playing","Eco","Pause"}); c->setCurrentIndex(static_cast<int>(mode)); return c; };
    auto battery=resourceCombo(library_.settings.rules.battery); form->addRow("On battery",battery);
    auto overlap=resourceCombo(library_.settings.rules.overlap); form->addRow("Covered display",overlap);
    auto threshold=new QSpinBox; threshold->setRange(1,100); threshold->setSuffix(" %"); threshold->setValue(static_cast<int>(library_.settings.rules.overlapThreshold*100)); form->addRow("Coverage",threshold);
    auto eco=new QSpinBox; eco->setRange(1,60); eco->setSuffix(" fps"); eco->setValue(library_.settings.rules.ecoFps); form->addRow("Eco limit",eco);
    auto input=new QCheckBox("Mirror desktop mouse input"); input->setChecked(library_.settings.interactive); form->addRow(input);
    auto note=new QLabel("Eco slows shaders and pauses video/web.\nWeb FPS and WebAudio volume depend on the bundle.\n\nMouse input is opt-in and never consumes desktop clicks. Keyboard input is not forwarded.");
    note->setWordWrap(true); note->setStyleSheet("color:#9aabc3;font-size:12px;"); form->addRow(note);
    body->addWidget(settingsPanel_); outer->addLayout(body,1);
    error_=new QLabel; error_->setWordWrap(true); error_->setStyleSheet("color:#ffc491;"); error_->hide(); outer->addWidget(error_);
    auto footer=new QHBoxLayout;
    status_=new QLabel("Ready"); status_->setStyleSheet("color:#99abc5;"); footer->addWidget(status_,1);
    auto clear=new QPushButton("Clear display"); footer->addWidget(clear);
    auto next=new QPushButton("Next wallpaper"); footer->addWidget(next);
    auto apply=new QPushButton("Apply wallpaper"); apply->setObjectName("apply"); footer->addWidget(apply); outer->addLayout(footer);

    auto save=[this]{guarded([this]{engine_.applySettings();});};
    connect(fps,&QSpinBox::valueChanged,this,[this,save](int value){library_.settings.rules.fps=value;save();});
    connect(volume,&QSlider::valueChanged,this,[this,save](int value){library_.settings.rules.volume=value;save();});
    connect(fullscreen,&QCheckBox::toggled,this,[this,save](bool value){library_.settings.rules.pauseFullscreen=value;save();});
    connect(battery,&QComboBox::currentIndexChanged,this,[this,save](int value){library_.settings.rules.battery=static_cast<ResourceMode>(value);save();});
    connect(overlap,&QComboBox::currentIndexChanged,this,[this,save](int value){library_.settings.rules.overlap=static_cast<ResourceMode>(value);save();});
    connect(threshold,&QSpinBox::valueChanged,this,[this,save](int value){library_.settings.rules.overlapThreshold=value/100.0;save();});
    connect(eco,&QSpinBox::valueChanged,this,[this,save](int value){library_.settings.rules.ecoFps=value;save();});
    connect(input,&QCheckBox::toggled,this,[this,save](bool value){library_.settings.interactive=value;save();});
    connect(span_,&QCheckBox::toggled,this,[this](bool value){ guarded([&] {library_.settings.span=value;library_.saveSettings();engine_.rebuild();}); });
    connect(displays_,&QComboBox::currentIndexChanged,this,[this] {
        QSignalBlocker blocker(displayMute_); displayMute_->setChecked(library_.settings.displayMuted.value(selectedDisplay(),false));
    });
    connect(displayMute_,&QCheckBox::toggled,this,[this,save](bool value){if (!selectedDisplay().isEmpty()) library_.settings.displayMuted[selectedDisplay()]=value;save();});
    connect(apply,&QPushButton::clicked,this,[this]{applySelected();});
    connect(grid_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){applySelected();});
    connect(next,&QPushButton::clicked,this,[this]{guarded([this]{engine_.skip();});});
    connect(clear,&QPushButton::clicked,this,[this]{guarded([this]{
        if (library_.settings.span) library_.settings.spanWallpaper.clear(); else library_.settings.assignments.remove(selectedDisplay());
        library_.saveSettings(); engine_.rebuild();
    });});

    tray_.setIcon(appIcon()); tray_.setToolTip("Aurora Wallpaper"); auto menu=new QMenu(this);
    menu->addAction("Open library",this,[this]{showNormal();raise();activateWindow();});
    pauseAction_=menu->addAction("Pause wallpapers"); pauseAction_->setCheckable(true);
    connect(pauseAction_,&QAction::toggled,this,[this](bool value){engine_.setPaused(value);});
    menu->addAction("Next wallpaper",this,[this]{guarded([this]{engine_.skip();});});
    muteAction_=menu->addAction("Mute audio"); muteAction_->setCheckable(true); muteAction_->setChecked(library_.settings.muted);
    connect(muteAction_,&QAction::toggled,this,[this](bool value){globalMute_->setChecked(value);});
    connect(globalMute_,&QCheckBox::toggled,this,[this,save](bool value){
        library_.settings.muted=value; QSignalBlocker blocker(muteAction_); muteAction_->setChecked(value);save();
    });
    menu->addAction("Settings",this,[this]{showNormal();raise();activateWindow();settingsPanel_->setFocus();});
    menu->addSeparator();
    menu->addAction("Exit",this,[this]{
        if (importing_) { error_->setText("Wait for the current import to finish, then exit."); error_->show(); showNormal(); return; }
        exiting_=true; engine_.stop(); qApp->quit();
    });
    tray_.setContextMenu(menu); tray_.show();
    connect(&tray_,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){
        if (reason==QSystemTrayIcon::DoubleClick || reason==QSystemTrayIcon::Trigger) {showNormal();raise();activateWindow();}
    });
    engine_.status=[this](QString text){if (!importing_) status_->setText(text);};
    engine_.error=[this](QString text){error_->setText(text);error_->show();};
    engine_.displaysChanged=[this]{refreshDisplays();};
    refreshGrid(); refreshDisplays();
}
MainWindow::~MainWindow() { engine_.status={};engine_.error={};engine_.displaysChanged={};tray_.hide(); }
QString MainWindow::selectedDisplay() const { return displays_->currentData().toString(); }
void MainWindow::refreshDisplays() {
    QString current=selectedDisplay(); QSignalBlocker blocker(displays_); displays_->clear();
    for (const auto& monitor:engine_.monitors()) {
        displays_->addItem(QString("%1 · %2 × %3 · %4 Hz%5").arg(QString::fromStdWString(monitor.name))
            .arg(monitor.bounds.width()).arg(monitor.bounds.height()).arg(monitor.refreshHz,0,'f',2).arg(monitor.primary?" · Primary":""),QString::fromStdWString(monitor.id));
    }
    int index=displays_->findData(current); if (index>=0) displays_->setCurrentIndex(index);
    QSignalBlocker muteBlocker(displayMute_); displayMute_->setChecked(library_.settings.displayMuted.value(selectedDisplay(),false));
}
void MainWindow::refreshGrid() {
    grid_->clear();
    for (const auto& w:library_.entries()) {
        QString resolution=w.width>0 && w.height>0?QString("%1 × %2").arg(w.width).arg(w.height):"Display-sized / auto";
        auto item=new QListWidgetItem(QIcon(thumbnail(w)),w.title+"\n"+kindName(w.kind)+" · "+resolution,grid_);
        item->setData(Qt::UserRole,w.id); item->setToolTip(w.title+"\n"+w.path()); item->setSizeHint({246,196});
    }
}
void MainWindow::applySelected() {
    if (!grid_->currentItem()) { status_->setText("Choose a wallpaper in the library first."); return; }
    QString id=grid_->currentItem()->data(Qt::UserRole).toString();
    guarded([&] {
        if (library_.settings.span) library_.settings.spanWallpaper=id;
        else if (!selectedDisplay().isEmpty()) library_.settings.assignments[selectedDisplay()]=id;
        else throw std::runtime_error("No display selected.");
        library_.saveSettings(); error_->hide(); engine_.rebuild();
    });
}
void MainWindow::import(bool web,Kind kind) {
    if (importing_) { status_->setText("An import is already in progress."); return; }
    QString path=web?QFileDialog::getExistingDirectory(this,"Import a web wallpaper folder"):
        QFileDialog::getOpenFileName(this,"Import wallpaper",{},kind==Kind::Shader?"HLSL shaders (*.hlsl)":"Videos (*.mp4 *.webm)");
    if (path.isEmpty()) return;
    importing_=true; status_->setText("Importing wallpaper…");
    // Copying large videos never blocks the UI / shell event pump. Work on an
    // independent implicitly-shared snapshot; only commit the entry list on the UI.
    auto watcher=new QFutureWatcher<ImportResult>(this);
    connect(watcher,&QFutureWatcher<ImportResult>::finished,this,[this,watcher] {
        auto result=watcher->result(); importing_=false;
        if (result.library) { library_.acceptImportedEntries(*result.library); refreshGrid(); status_->setText("Imported. Choose a display and apply the wallpaper."); }
        else { error_->setText(result.error); error_->show(); }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([snapshot=library_,path,web,kind]() mutable -> ImportResult {
        HRESULT com=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        ImportResult result;
        try { if (web) snapshot.importWebFolder(path); else snapshot.importFile(path,kind); result.library=std::move(snapshot); }
        catch (const std::exception& e) { result.error=QString::fromUtf8(e.what()); }
        if (SUCCEEDED(com)) CoUninitialize(); return result;
    }));
}
void MainWindow::closeEvent(QCloseEvent* event) {
    if (!exiting_ && QSystemTrayIcon::isSystemTrayAvailable()) { event->ignore();hide(); }
    else if (importing_) event->ignore();
    else { engine_.stop();event->accept();qApp->quit(); }
}
void MainWindow::changeEvent(QEvent* event) {
    if (event->type()==QEvent::WindowStateChange && isMinimized() && QSystemTrayIcon::isSystemTrayAvailable()) QTimer::singleShot(0,this,[this]{hide();});
    QMainWindow::changeEvent(event);
}
}
