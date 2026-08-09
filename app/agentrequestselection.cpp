#include "agentrequestselection.h"

bool AgentRequestSelection::requestConfig(SnmpRequestConfig *config) const
{
    return SnmpRequestConfig::FromProfile(profile, selectedProtocol, config);
}

AgentSelectionError AgentSelectionResolver::Resolve(
    const QList<AgentProfileRecord> &profiles, const QString &profileName,
    int selectedProtocol, AgentRequestSelection *selection)
{
    if (selectedProtocol < 0 || selectedProtocol > 2)
        return AgentSelectionError::InvalidProtocol;

    const AgentProfileRecord *profile = nullptr;
    for (const AgentProfileRecord &candidate : profiles)
    {
        if (candidate.name == profileName)
        {
            profile = &candidate;
            break;
        }
    }
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
