#ifndef DIAGNOSTICLOGGER_H
#define DIAGNOSTICLOGGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class QTextEdit;
class QWidget;

class DiagnosticLogger : public QObject
{
public:
    static bool initialize(const QString &version, const QStringList &arguments,
                           const QString &directoryOverride = {});
    static void shutdown();
    static void log(const QString &category, const QString &message,
                    bool mirrorToUi = true);
    static QString redact(const QString &message);
    static QString logFilePath();
    static QString logDirectory();
    static void attachLogWidget(QTextEdit *widget);
    static void installMainWindowLifecycle(QWidget *window);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
};

#endif
