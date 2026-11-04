#include "MainWindow.h"
#include <QApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDateTime>
#include <QLockFile>
#include <QMessageBox>
#include <QDebug>
#include <QTimer>
#include <WebView2.h>
#include <d3dcompiler.h>
#include <mutex>
namespace {
QString logPath;
std::mutex logMutex;
void logMessage(QtMsgType,const QMessageLogContext&,const QString& message) {
    std::lock_guard lock(logMutex);
    // Bounded local logs; no analytics, uploads, or mouse-input logging.
    QFile file(logPath);
    if (file.size()>2*1024*1024) { QFile::remove(logPath+".old"); file.rename(logPath+".old"); file.setFileName(logPath); }
    if (file.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text)) {
        QTextStream out(&file);out<<QDateTime::currentDateTimeUtc().toString(Qt::ISODate)<<" "<<message<<'\n';
    }
    OutputDebugStringW(reinterpret_cast<LPCWSTR>((message+'\n').utf16()));
}
struct ComApartment {
    HRESULT hr=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~ComApartment(){if(SUCCEEDED(hr)) CoUninitialize();}
};
struct InstallerMutex {
    HANDLE handle=CreateMutexW(nullptr,FALSE,L"Local\\MrBoss.AuroraWallpaper");
    ~InstallerMutex(){if(handle) CloseHandle(handle);}
};
bool verifyInstalledPayload() {
    const auto root=QCoreApplication::applicationDirPath();
    LPWSTR version{};
    HRESULT browser=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);
    CoTaskMemFree(version);
    if (FAILED(browser)) { qCritical()<<"WebView2 Runtime is unavailable";return false; }
    const auto vlcPath=QDir::toNativeSeparators(root+"/vlc/libvlc.dll").toStdWString();
    HMODULE vlc=LoadLibraryExW(vlcPath.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!vlc) {qCritical()<<"Bundled libVLC failed to load";return false;}
    bool vlcApi=GetProcAddress(vlc,"libvlc_new") && GetProcAddress(vlc,"libvlc_media_player_set_hwnd");
    FreeLibrary(vlc);
    if (!vlcApi) {qCritical()<<"Bundled libVLC API is missing";return false;}
    QFile file(root+"/samples/aurora/aurora.hlsl");
    if (!file.open(QIODevice::ReadOnly)) return false;
    auto source=file.readAll(); aurora::win::ComPtr<ID3DBlob> compiled,errors;
    auto result=D3DCompile(source.constData(),static_cast<SIZE_T>(source.size()),nullptr,nullptr,nullptr,
        "PSMain","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&compiled,&errors);
    if (FAILED(result)) {qCritical()<<"Bundled shader failed compilation";return false;}
    return QFileInfo::exists(root+"/samples/web-orbit/index.html");
}
}
int main(int argc,char** argv) {
    // Match Explorer privilege level. Run as a regular user; never request elevation.
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ComApartment com;
    QApplication app(argc,argv);
    app.setOrganizationName("MrBoss");app.setApplicationName("AuroraWallpaper");app.setApplicationVersion("0.1.1");
    app.setQuitOnLastWindowClosed(false);
    const bool smoke=app.arguments().contains("--smoke-test");
    const bool tray=app.arguments().contains("--tray");
    if(smoke) QStandardPaths::setTestModeEnabled(true);
    if (FAILED(com.hr)) { QMessageBox::critical(nullptr,"Aurora Wallpaper","The UI thread must run in a COM STA.");return 1; }
    auto directory=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);QDir().mkpath(directory);
    logPath=directory+"/aurora.log";qInstallMessageHandler(logMessage);
    InstallerMutex installerMutex;
    if(smoke && !verifyInstalledPayload()) {qInstallMessageHandler(nullptr);return 2;}
    QLockFile lock(directory+"/instance.lock");lock.setStaleLockTime(0);
    if (!lock.tryLock(0)) { QMessageBox::information(nullptr,"Aurora Wallpaper","Aurora is already running. Open it from the system tray.");return 0; }
    int result=1;
    try {
        aurora::Library library;
        try { library.importSamples(); } catch (const std::exception& e) { qWarning()<<e.what(); }
        aurora::Engine engine(library);
        aurora::MainWindow window(library,engine);
        QObject::connect(&app,&QCoreApplication::aboutToQuit,&engine,[&]{engine.stop();});
        if (!smoke && (!tray || !QSystemTrayIcon::isSystemTrayAvailable())) window.show();
        engine.start();
        if(smoke) QTimer::singleShot(1000,&app,&QCoreApplication::quit);
        result=app.exec();
    } catch (const std::exception& e) { QMessageBox::critical(nullptr,"Aurora Wallpaper",QString::fromUtf8(e.what())); }
    qInstallMessageHandler(nullptr);return result;
}
