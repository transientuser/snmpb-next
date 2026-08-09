#ifndef PREFERENCESETTINGS_H
#define PREFERENCESETTINGS_H

#include <QPoint>
#include <QSettings>
#include <QSize>
#include <QStringList>

struct PreferencesSettings
{
    bool enableIpv4 = true;
    int trapPort4 = 162;
    bool enableIpv6 = true;
    int trapPort6 = 162;
    bool horizontalSplit = false;
    bool expandTrapBinding = true;
    bool showAgentName = false;
    int automaticLoading = 2;
    QString selectedProfile = QStringLiteral("localhost");
    int selectedProtocol = 0;
    QStringList mibPaths;
    QStringList mibPreloads;
    QSize windowSize;
    QPoint windowPosition;

    static PreferencesSettings load(QSettings &settings);
    void save(QSettings &settings) const;
    bool networkRestartRequired(const PreferencesSettings &persisted) const;
};

#endif
