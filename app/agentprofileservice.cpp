#include "agentprofileservice.h"

#include "agentprofileoperations.h"

AgentProfileService::AgentProfileService(const QString &fileName, QObject *parent)
    : QObject(parent), repository(fileName), records(repository.LoadOrCreateDefaults())
{
}

const QList<AgentProfileRecord> &AgentProfileService::profiles() const
{
    return records;
}

const AgentProfileRecord *AgentProfileService::findById(const QString &id) const
{
    for (const AgentProfileRecord &record : records)
        if (record.profileId == id)
            return &record;
    return nullptr;
}

const AgentProfileRecord *AgentProfileService::findFirstByName(const QString &name) const
{
    for (const AgentProfileRecord &record : records)
        if (record.name == name)
            return &record;
    return nullptr;
}

QString AgentProfileService::uniqueIdForName(const QString &name) const
{
    QString id;
    for (const AgentProfileRecord &record : records)
        if (record.name == name)
        {
            if (!id.isEmpty())
                return {};
            id = record.profileId;
        }
    return id;
}

QString AgentProfileService::create(const AgentProfileRecord &draft)
{
    AgentProfileRecord record = draft;
    if (record.profileId.isEmpty() || findById(record.profileId))
        record.profileId = AgentProfileRepository::CreateProfileId();
    records.append(record);
    save();
    emit profileCreated(record.profileId);
    emit profilesChanged();
    return record.profileId;
}

bool AgentProfileService::update(const AgentProfileRecord &record)
{
    for (AgentProfileRecord &stored : records)
        if (stored.profileId == record.profileId)
        {
            const QString oldName = stored.name;
            stored = record;
            save();
            emit profileUpdated(record.profileId);
            if (oldName != record.name)
                emit profileRenamed(record.profileId, oldName, record.name);
            emit profilesChanged();
            return true;
        }
    return false;
}

bool AgentProfileService::remove(const QString &id)
{
    for (int i = 0; i < records.size(); ++i)
        if (records[i].profileId == id)
        {
            records.removeAt(i);
            save();
            emit profileDeleted(id);
            emit profilesChanged();
            return true;
        }
    return false;
}

QString AgentProfileService::duplicate(const QString &id)
{
    AgentProfileRecord copy;
    if (!AgentProfileOperations::Duplicate(records, id, &copy))
        return {};
    records.append(copy);
    save();
    emit profileCreated(copy.profileId);
    emit profileDuplicated(id, copy.profileId);
    emit profilesChanged();
    return copy.profileId;
}

QString AgentProfileService::createFromTemplate(
    const QString &templateId, const QString &name, const QString &address,
    const QString &port, bool v1, bool v2c, bool v3)
{
    const AgentProfileRecord *source = findById(templateId);
    if (!source) return {};
    AgentProfileRecord record = *source;
    record.profileId.clear(); record.name = name; record.address = address;
    record.port = port; record.v1 = v1; record.v2 = v2c; record.v3 = v3;
    return create(record);
}

void AgentProfileService::reload()
{
    records = repository.LoadOrCreateDefaults();
    emit profilesReloaded();
    emit profilesChanged();
}

void AgentProfileService::save()
{
    repository.Save(records);
}
