#include "diagnosticlogger.h"
#include "udpportowner.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    bool ok = check(temporary.isValid(), "temporary log directory");
    ok &= check(DiagnosticLogger::initialize("test", app.arguments(), temporary.path()),
                "logger initialization");
    DiagnosticLogger::log("Test",
        "community=public password=hunter2 authSecret=alpha privacySecret=beta");
    const QString path = DiagnosticLogger::logFilePath();
    DiagnosticLogger::shutdown();
    QFile log(path);
    ok &= check(log.open(QIODevice::ReadOnly), "diagnostic log creation");
    const QByteArray contents = log.readAll();
    ok &= check(contents.contains("PROCESS START") && contents.contains("[REDACTED]"),
                "startup logging and redaction marker");
    ok &= check(!contents.contains("public") && !contents.contains("hunter2") &&
                !contents.contains("alpha") && !contents.contains("beta"),
                "secret leaked to diagnostic log");

    UdpPortOwner owner;
    owner.found = true; owner.processId = 78472;
    owner.processName = "mib-navigator.exe";
    owner.executablePath = "C:/MIB Navigator/mib-navigator.exe";
    const QString description = UdpPortOwnerLookup::conflictDescription(162, owner);
    ok &= check(description.contains("Another instance") &&
                description.contains("78472") && description.contains(owner.executablePath),
                "Windows owner diagnostic formatting");
    const QString unavailable = UdpPortOwnerLookup::conflictDescription(162, {});
    ok &= check(unavailable.contains("could not be determined"),
                "unavailable owner diagnostic");
    // This read-only lookup opens no socket and is safe regardless of whether
    // the chosen high port currently has an owner.
    const UdpPortOwner probe = UdpPortOwnerLookup::lookup(65534, false);
    Q_UNUSED(probe);
    return ok ? 0 : 1;
}
