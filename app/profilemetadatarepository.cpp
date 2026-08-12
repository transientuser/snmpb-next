#include "profilemetadatarepository.h"

#include <QFileInfo>
#include <QSettings>
#include <QSet>

ProfileMetadataRepository::ProfileMetadataRepository(const QString &fileName)
    : path(fileName)
{
}

QList<ProfileMetadataRecord> ProfileMetadataRepository::load() const
{
    if (!QFileInfo::exists(path))
        return {};
    QSettings settings(path, QSettings::IniFormat);
    const int version = settings.value("schema/version").toInt();
    if (version < 1 || version > CurrentVersion)
        return {};
    QList<ProfileMetadataRecord> result;
    QSet<QString> seen;
    const int count = settings.beginReadArray("profiles");
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        ProfileMetadataRecord record;
        record.profileId = settings.value("profileId").toString().trimmed();
        record.notes = settings.value("notes").toString();
        record.tags = normalizeTags(settings.value("tags").toStringList());
        if (version >= 2)
            record.preferredMibs = normalizeMibs(
                settings.value("preferredMibs").toStringList());
        if (version >= 3)
        {
            record.hasActiveProtocol = settings.contains("activeProtocol");
            record.activeProtocol = settings.value("activeProtocol").toInt();
            record.usmCredentialId = settings.value("usmCredentialId").toString();
            record.hasRequestSettingsMode = settings.contains("requestSettingsMode");
            record.requestSettingsMode = settings.value("requestSettingsMode").toInt();
            record.overrideTimeout = settings.value("overrideTimeout", 3).toInt();
            record.overrideRetries = settings.value("overrideRetries", 1).toInt();
            record.overrideBulkNonRepeaters =
                settings.value("overrideBulkNonRepeaters", 0).toInt();
            record.overrideBulkMaxRepetitions =
                settings.value("overrideBulkMaxRepetitions", 10).toInt();
        }
        if (record.profileId.isEmpty() || seen.contains(record.profileId))
            continue;
        seen.insert(record.profileId);
        result.append(record);
    }
    settings.endArray();
    return result;
}

bool ProfileMetadataRepository::save(
    const QList<ProfileMetadataRecord> &records) const
{
    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    settings.setValue("schema/version", CurrentVersion);
    settings.beginWriteArray("profiles");
    int index = 0;
    for (const ProfileMetadataRecord &source : records)
    {
        if (source.profileId.trimmed().isEmpty())
            continue;
        settings.setArrayIndex(index++);
        settings.setValue("profileId", source.profileId.trimmed());
        settings.setValue("notes", source.notes);
        settings.setValue("tags", normalizeTags(source.tags));
        settings.setValue("preferredMibs", normalizeMibs(source.preferredMibs));
        if (source.hasActiveProtocol)
            settings.setValue("activeProtocol", source.activeProtocol);
        if (!source.usmCredentialId.isEmpty())
            settings.setValue("usmCredentialId", source.usmCredentialId);
        if (source.hasRequestSettingsMode)
        {
            settings.setValue("requestSettingsMode", source.requestSettingsMode);
            if (source.requestSettingsMode == 2)
            {
                settings.setValue("overrideTimeout", source.overrideTimeout);
                settings.setValue("overrideRetries", source.overrideRetries);
                settings.setValue("overrideBulkNonRepeaters",
                                  source.overrideBulkNonRepeaters);
                settings.setValue("overrideBulkMaxRepetitions",
                                  source.overrideBulkMaxRepetitions);
            }
        }
    }
    settings.endArray();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QStringList ProfileMetadataRepository::normalizeMibs(const QStringList &mibs)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &raw : mibs)
    {
        const QString name = raw.trimmed();
        if (!name.isEmpty() && !seen.contains(name))
        {
            seen.insert(name);
            result.append(name);
        }
    }
    return result;
}

QString ProfileMetadataRepository::fileName() const
{
    return path;
}

QStringList ProfileMetadataRepository::normalizeTags(const QStringList &tags)
{
    QStringList result;
    QSet<QString> seen;
    for (const QString &raw : tags)
    {
        const QString tag = raw.trimmed();
        const QString key = tag.toCaseFolded();
        if (!tag.isEmpty() && !seen.contains(key))
        {
            seen.insert(key);
            result.append(tag);
        }
    }
    return result;
}
