#include "connectionrequestsettings.h"

RequestSettingsMode ConnectionRequestSettings::mode(
    const ProfileMetadataRecord &metadata)
{
    if (!metadata.hasRequestSettingsMode)
        return RequestSettingsMode::Legacy;
    return metadata.requestSettingsMode == 2 ? RequestSettingsMode::Override
        : RequestSettingsMode::Inherit;
}

AgentProfileRecord ConnectionRequestSettings::effectiveProfile(
    const AgentProfileRecord &source, const ProfileMetadataRecord &metadata,
    const PreferencesSettings &preferences)
{
    AgentProfileRecord result = source;
    switch (mode(metadata))
    {
    case RequestSettingsMode::Legacy:
        break;
    case RequestSettingsMode::Inherit:
        result.timeout = preferences.requestTimeout;
        result.retries = preferences.requestRetries;
        result.nonrepeaters = preferences.bulkNonRepeaters;
        result.maxrepetitions = preferences.bulkMaxRepetitions;
        break;
    case RequestSettingsMode::Override:
        result.timeout = metadata.overrideTimeout;
        result.retries = metadata.overrideRetries;
        result.nonrepeaters = metadata.overrideBulkNonRepeaters;
        result.maxrepetitions = metadata.overrideBulkMaxRepetitions;
        break;
    }
    return result;
}

int ConnectionRequestSettings::activeProtocol(
    const AgentProfileRecord &profile, const ProfileMetadataRecord &metadata,
    int legacyProtocol)
{
    const int requested = metadata.hasActiveProtocol ? metadata.activeProtocol
                                                      : legacyProtocol;
    if (requested == 0 && profile.v1) return 0;
    if (requested == 1 && profile.v2) return 1;
    if (requested == 2 && profile.v3) return 2;
    if (profile.v1) return 0;
    if (profile.v2) return 1;
    if (profile.v3) return 2;
    return -1;
}
