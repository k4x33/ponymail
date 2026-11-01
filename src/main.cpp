#include "ui/MainWindow.h"
#include "core/Paths.h"
#include <QApplication>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QDir>

namespace {
QFile* g_log = nullptr;
void logHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (!g_log) return;
    static const char* names[] = {"DBG", "WRN", "CRT", "FTL", "INF"};
    QTextStream ts(g_log);
    ts << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << ' ' << names[int(type)] << ' ' << msg << '\n';
    ts.flush();
}
}

int main(int argc, char* argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("PonyMail"));
    QCoreApplication::setApplicationName(QStringLiteral("PonyMail"));
    QCoreApplication::setApplicationVersion(QStringLiteral(PONYMAIL_VERSION));
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication app(argc, argv);
    Q_INIT_RESOURCE(ponymail);   // resources live in the static core library
    ponymail::Paths::ensureDirs();
    QFile log(ponymail::Paths::logFile());
    if (log.size() > 2 * 1024 * 1024) log.remove();
    if (log.open(QIODevice::Append | QIODevice::Text)) { g_log = &log; qInstallMessageHandler(logHandler); }

    ponymail::MainWindow w;
    w.show();
    const int rc = app.exec();
    g_log = nullptr;
    return rc;
}
