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
    if (settings.value("schema/version").toInt() != CurrentVersion)
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
    }
    settings.endArray();
    settings.sync();
    return settings.status() == QSettings::NoError;
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
