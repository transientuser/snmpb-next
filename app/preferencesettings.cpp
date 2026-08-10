#include "preferencesettings.h"

namespace {
QStringList readArray(QSettings &settings, const char *array, const char *key)
{
    QStringList values;
    const int size = settings.beginReadArray(QLatin1String(array));
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        values << settings.value(QLatin1String(key)).toString();
    }
    settings.endArray();
    return values;
}

void writeArray(QSettings &settings, const char *array, const char *key,
                const QStringList &values)
{
    settings.beginWriteArray(QLatin1String(array));
    for (int i = 0; i < values.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue(QLatin1String(key), values.at(i));
    }
    settings.endArray();
}
}

PreferencesSettings PreferencesSettings::load(QSettings &settings)
{
    PreferencesSettings result;
    result.enableIpv4 = settings.value("network/enableipv4", true).toBool();
    result.trapPort4 = settings.value("network/trapport4", 162).toInt();
    result.enableIpv6 = settings.value("network/enableipv6", true).toBool();
    result.trapPort6 = settings.value("network/trapport6", 162).toInt();
    result.horizontalSplit = settings.value("ui/horizontalsplit", false).toBool();
    result.expandTrapBinding = settings.value("ui/expandtrapbinding", true).toBool();
    result.showAgentName = settings.value("misc/showagentname", false).toBool();
    result.trapHistoryLimit = qBound(1, settings.value("ui/traphistorylimit", 1000).toInt(), 100000);
    result.automaticLoading = settings.value("misc/automaticloading", 2).toInt();
    result.selectedProfile = settings.value("ui/selectedprofile", "localhost").toString();
    result.selectedProfileId = settings.value("ui/selectedprofileid").toString();
    result.selectedProtocol = settings.value("ui/selectedproto", 0).toInt();
    result.mibPaths = readArray(settings, "mibpaths", "dir");
    result.mibPreloads = readArray(settings, "mibpreloads", "mib");
    result.windowSize = settings.value("mainwindow/size").toSize();
    result.windowPosition = settings.value("mainwindow/pos").toPoint();
    return result;
}

void PreferencesSettings::save(QSettings &settings) const
{
    settings.setValue("network/enableipv4", enableIpv4);
    settings.setValue("network/trapport4", trapPort4);
    settings.setValue("network/enableipv6", enableIpv6);
    settings.setValue("network/trapport6", trapPort6);
    settings.setValue("ui/horizontalsplit", horizontalSplit);
    settings.setValue("ui/expandtrapbinding", expandTrapBinding);
    settings.setValue("misc/showagentname", showAgentName);
    settings.setValue("ui/traphistorylimit", qBound(1, trapHistoryLimit, 100000));
    settings.setValue("misc/automaticloading", automaticLoading);
    settings.setValue("ui/selectedprofile", selectedProfile);
    if (!selectedProfileId.isEmpty())
        settings.setValue("ui/selectedprofileid", selectedProfileId);
    else
        settings.remove("ui/selectedprofileid");
    settings.setValue("ui/selectedproto", selectedProtocol);
    writeArray(settings, "mibpaths", "dir", mibPaths);
    writeArray(settings, "mibpreloads", "mib", mibPreloads);
    if (windowSize.isValid())
        settings.setValue("mainwindow/size", windowSize);
    if (!windowPosition.isNull())
        settings.setValue("mainwindow/pos", windowPosition);
}

bool PreferencesSettings::networkRestartRequired(
    const PreferencesSettings &persisted) const
{
    return trapPort4 != persisted.trapPort4 ||
           trapPort6 != persisted.trapPort6 ||
           enableIpv4 != persisted.enableIpv4 ||
           enableIpv6 != persisted.enableIpv6;
}
