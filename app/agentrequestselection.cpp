#include "agentrequestselection.h"

bool AgentRequestSelection::requestConfig(SnmpRequestConfig *config) const
{
    return hasResolvedCredentials
        ? SnmpRequestConfig::FromProfile(profile, selectedProtocol, credentials, config)
        : SnmpRequestConfig::FromProfile(profile, selectedProtocol, config);
}

namespace
{
AgentSelectionError ResolveRecord(const AgentProfileRecord *profile,
    int selectedProtocol, AgentRequestSelection *selection)
{
    if (selectedProtocol < 0 || selectedProtocol > 2)
        return AgentSelectionError::InvalidProtocol;

    if (!profile)
        return AgentSelectionError::ProfileNotFound;

    const bool supported = selectedProtocol == 0 ? profile->v1
        : (selectedProtocol == 1 ? profile->v2 : profile->v3);
    if (!supported)
        return AgentSelectionError::UnsupportedProtocol;
    if (!selection)
        return AgentSelectionError::InvalidProtocol;

    selection->profile = *profile;
    selection->selectedProtocol = selectedProtocol;
    return AgentSelectionError::None;
}
}

AgentSelectionError AgentSelectionResolver::ResolveById(
    const QList<AgentProfileRecord> &profiles, const QString &profileId,
    int selectedProtocol, AgentRequestSelection *selection)
{
    for (const AgentProfileRecord &candidate : profiles)
        if (candidate.profileId == profileId)
            return ResolveRecord(&candidate, selectedProtocol, selection);
    return ResolveRecord(nullptr, selectedProtocol, selection);
}

QString AgentSelectionResolver::UniqueProfileIdForName(
    const QList<AgentProfileRecord> &profiles, const QString &profileName)
{
    QString result;
    for (const AgentProfileRecord &candidate : profiles)
        if (candidate.name == profileName)
        {
            if (!result.isEmpty())
                return {};
            result = candidate.profileId;
        }
    return result;
}

AgentSelectionError AgentSelectionResolver::Resolve(
    const QList<AgentProfileRecord> &profiles, const QString &profileName,
    int selectedProtocol, AgentRequestSelection *selection)
{
    for (const AgentProfileRecord &candidate : profiles)
        if (candidate.name == profileName)
            return ResolveRecord(&candidate, selectedProtocol, selection);
    return ResolveRecord(nullptr, selectedProtocol, selection);
}
