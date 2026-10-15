#include "MainWindow.h"

#include <QApplication>
#include <QDebug>
#include <QIcon>

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>

// GECICI: carpan noktayi gormek icin yigin izini yazdirir.
static LONG WINAPI printStackTrace(EXCEPTION_POINTERS* info)
{
    if (info && info->ExceptionRecord) {
        fprintf(stderr, "EXCEPTION 0x%08X at %p\n",
                info->ExceptionRecord->ExceptionCode,
                info->ExceptionRecord->ExceptionAddress);
    }

    void* stack[62];
    const USHORT frames = CaptureStackBackTrace(0, 62, stack, nullptr);

    HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);

    for (USHORT i = 0; i < frames; ++i) {
        char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME];
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;

        DWORD64 displacement = 0;
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(stack[i]), &displacement, symbol)) {
            fprintf(stderr, "  #%02u %s+0x%llx [%p]\n", i, symbol->Name,
                    static_cast<unsigned long long>(displacement), stack[i]);
        } else {
            fprintf(stderr, "  #%02u <bilinmeyen> [%p]\n", i, stack[i]);
        }
    }
    fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

int main(int argc, char* argv[])
{
    SetUnhandledExceptionFilter(printStackTrace);

    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("xVNC"));
    QCoreApplication::setApplicationName(QStringLiteral("xVNCManager"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.99"));

    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    qDebug() << "step: QApplication hazir";
    MainWindow window;
    qDebug() << "step: MainWindow olusturuldu";
    window.show();
    qDebug() << "step: show() tamam";

    return app.exec();
}
