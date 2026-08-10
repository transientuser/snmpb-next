#include "communitybindingrepository.h"
#include <QFile>
#include <QSettings>

CommunityBindingRepository::CommunityBindingRepository(const QString &name)
    : fileName(name) {}

QHash<QString, QString> CommunityBindingRepository::load() const
{
    QHash<QString, QString> result;
    if (!QFile::exists(fileName)) return result;
    QSettings settings(fileName, QSettings::IniFormat);
    if (settings.value("schema/version").toInt() != CurrentVersion) return result;
    const int size = settings.beginReadArray("bindings");
    for (int i = 0; i < size; ++i)
    {
        settings.setArrayIndex(i);
        const QString profileId = settings.value("profileId").toString();
        const QString credentialId = settings.value("credentialId").toString();
        if (!profileId.isEmpty() && !credentialId.isEmpty())
            result.insert(profileId, credentialId);
    }
    settings.endArray(); return result;
}

bool CommunityBindingRepository::save(
    const QHash<QString, QString> &bindings) const
{
    QSettings settings(fileName, QSettings::IniFormat);
    settings.clear(); settings.setValue("schema/version", CurrentVersion);
    settings.beginWriteArray("bindings");
    QStringList profiles = bindings.keys(); profiles.sort();
    for (int i = 0; i < profiles.size(); ++i)
    {
        settings.setArrayIndex(i);
        settings.setValue("profileId", profiles[i]);
        settings.setValue("credentialId", bindings.value(profiles[i]));
    }
    settings.endArray(); settings.sync();
    return settings.status() == QSettings::NoError;
}
