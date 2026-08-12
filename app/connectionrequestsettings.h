#ifndef CONNECTIONREQUESTSETTINGS_H
#define CONNECTIONREQUESTSETTINGS_H

#include "agentprofilerepository.h"
#include "preferencesettings.h"
#include "profilemetadatarepository.h"

enum class RequestSettingsMode { Legacy = 0, Inherit = 1, Override = 2 };

class ConnectionRequestSettings
{
public:
    static RequestSettingsMode mode(const ProfileMetadataRecord &metadata);
    static AgentProfileRecord effectiveProfile(
        const AgentProfileRecord &profile, const ProfileMetadataRecord &metadata,
        const PreferencesSettings &preferences);
    static int activeProtocol(const AgentProfileRecord &profile,
                              const ProfileMetadataRecord &metadata,
                              int legacyProtocol);
};

#endif
