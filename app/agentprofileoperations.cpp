#include "agentprofileoperations.h"

AgentProfileEditTransaction::AgentProfileEditTransaction(
    const QList<AgentProfileRecord> &originalRecords)
    : original(originalRecords)
{
}

const QList<AgentProfileRecord> &AgentProfileEditTransaction::rollbackRecords() const
{
    return original;
}

bool AgentProfileOperations::Duplicate(
    const QList<AgentProfileRecord> &profiles, const QString &sourceId,
    AgentProfileRecord *duplicate)
{
    if (!duplicate)
        return false;
    const AgentProfileRecord *source = nullptr;
    for (const AgentProfileRecord &profile : profiles)
        if (profile.profileId == sourceId)
        {
            source = &profile;
            break;
        }
    if (!source)
        return false;

    const QString sourceName = source->name;
    QString candidate = sourceName + QStringLiteral(" copy");
    int suffix = 2;
    auto nameExists = [&profiles](const QString &name) {
        for (const AgentProfileRecord &profile : profiles)
            if (profile.name == name)
                return true;
        return false;
    };
    while (nameExists(candidate))
        candidate = sourceName + QStringLiteral(" copy %1").arg(suffix++);
    *duplicate = *source;
    duplicate->profileId = AgentProfileRepository::CreateProfileId();
    duplicate->name = candidate;
    return true;
}
