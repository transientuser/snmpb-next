#include "profilemetadataservice.h"

#include <QSet>

ProfileMetadataService::ProfileMetadataService(const QString &fileName,
                                               QObject *parent)
    : QObject(parent), repository(fileName), records(repository.load())
{
}

ProfileMetadataRecord ProfileMetadataService::metadataForProfile(
    const QString &id) const
{
    const int index = indexOf(id);
    if (index >= 0)
        return records[index];
    ProfileMetadataRecord empty;
    empty.profileId = id;
    return empty;
}

const QList<ProfileMetadataRecord> &ProfileMetadataService::allMetadata() const
{
    return records;
}

bool ProfileMetadataService::update(const ProfileMetadataRecord &source)
{
    ProfileMetadataRecord record = source;
    record.profileId = record.profileId.trimmed();
    record.tags = ProfileMetadataRepository::normalizeTags(record.tags);
    record.preferredMibs = ProfileMetadataRepository::normalizeMibs(
        record.preferredMibs);
    if (record.profileId.isEmpty())
        return false;
    const int index = indexOf(record.profileId);
    if (record.notes.isEmpty() && record.tags.isEmpty() &&
        record.preferredMibs.isEmpty())
    {
        if (index < 0) return true;
        records.removeAt(index);
    }
    else if (index < 0)
        records.append(record);
    else
        records[index] = record;
    if (!save()) return false;
    emit metadataChanged(record.profileId);
    return true;
}

bool ProfileMetadataService::setNotes(const QString &id, const QString &notes)
{
    ProfileMetadataRecord record = metadataForProfile(id);
    record.notes = notes;
    return update(record);
}

bool ProfileMetadataService::setTags(const QString &id, const QStringList &tags)
{
    ProfileMetadataRecord record = metadataForProfile(id);
    record.tags = tags;
    return update(record);
}

bool ProfileMetadataService::remove(const QString &id)
{
    const int index = indexOf(id);
    if (index < 0) return true;
    records.removeAt(index);
    if (!save()) return false;
    emit metadataChanged(id);
    return true;
}

bool ProfileMetadataService::copy(const QString &sourceId,
                                  const QString &destinationId)
{
    ProfileMetadataRecord record = metadataForProfile(sourceId);
    record.profileId = destinationId;
    return update(record);
}

bool ProfileMetadataService::reconcile(const QStringList &ids, bool persist)
{
    const QSet<QString> existing(ids.begin(), ids.end());
    const qsizetype before = records.size();
    records.removeIf([&](const ProfileMetadataRecord &record) {
        return !existing.contains(record.profileId);
    });
    return before == records.size() || !persist || save();
}

QStringList ProfileMetadataService::allTags() const
{
    QStringList tags;
    for (const ProfileMetadataRecord &record : records)
        tags.append(record.tags);
    return ProfileMetadataRepository::normalizeTags(tags);
}

void ProfileMetadataService::reload()
{
    records = repository.load();
    emit metadataChanged(QString());
}

int ProfileMetadataService::indexOf(const QString &id) const
{
    for (int i = 0; i < records.size(); ++i)
        if (records[i].profileId == id) return i;
    return -1;
}

bool ProfileMetadataService::save()
{
    return repository.save(records);
}
