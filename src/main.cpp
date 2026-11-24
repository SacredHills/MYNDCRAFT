#include <QApplication>
#include <QCommandLineParser>
#include <QStyleFactory>
#include <QTimer>

#include "Core/Settings.h"
#include "UI/MainWindow.h"

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
// If the app dies with an unhandled exception, leave a small text report next to the exe so the
// cause can be diagnosed (exception code, faulting address and a raw call stack as module+offset).
static LONG WINAPI crashReport(EXCEPTION_POINTERS* ep) {
    char path[MAX_PATH];
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string p(path);
    p = p.substr(0, p.find_last_of("\\/") + 1) + "myndcraft-crash.txt";
    if (FILE* f = std::fopen(p.c_str(), "w")) {
        std::fprintf(f, "MyndCraft crash\ncode=0x%08lX address=%p\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
        void* frames[48];
        const USHORT n = CaptureStackBackTrace(0, 48, frames, nullptr);
        for (USHORT i = 0; i < n; ++i) {
            HMODULE m = nullptr;
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)frames[i], &m);
            char name[MAX_PATH] = "?";
            if (m) GetModuleFileNameA(m, name, MAX_PATH);
            std::fprintf(f, "#%u %s +0x%llX\n", i, name, (unsigned long long)((char*)frames[i] - (char*)m));
        }
        std::fclose(f);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

static void applyDarkTheme(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    const QColor base(0x2b, 0x2b, 0x2e), alt(0x33, 0x33, 0x37), text(0xdd, 0xdd, 0xdd);
    p.setColor(QPalette::Window, QColor(0x38, 0x38, 0x3c));
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alt);
    p.setColor(QPalette::ToolTipBase, QColor(0x20, 0x20, 0x22));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x44, 0x44, 0x49));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Highlight, QColor(0x2a, 0x82, 0xda));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x80, 0x80, 0x80));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x80, 0x80, 0x80));
    app.setPalette(p);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    SetUnhandledExceptionFilter(crashReport);
#endif
    QApplication app(argc, argv);
    app.setApplicationName("MyndCraft");
    app.setOrganizationName("MyndCraft");
    app.setApplicationVersion("0.1.0");
    applyDarkTheme(app);

    QCommandLineParser parser;
    parser.setApplicationDescription("MyndCraft — 2D animation, motion graphics and 3D compositing");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument("file", "Project (.studio) to open.");
    QCommandLineOption shot("screenshot", "Render the main window to a PNG and exit (for automated checks).", "png");
    parser.addOption(shot);
    parser.process(app);

    mynd::MainWindow w;
    const QStringList args = parser.positionalArguments();
    if (!args.isEmpty()) {
        QString err;
        if (!w.openFile(args.first(), &err)) qWarning().noquote() << "Cannot open" << args.first() << ":" << err;
    }
    w.show();
    if (parser.isSet(shot)) {
        const QString out = parser.value(shot);
        QTimer::singleShot(300, [&w, out, &app] {
            w.grab().save(out);
            app.quit();
        });
        return app.exec();
    }
    if (args.isEmpty()) QTimer::singleShot(200, &w, [&w] { w.offerRecovery(); });
    return app.exec();
}
