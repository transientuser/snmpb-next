#include "connectionrequestsettings.h"

#include <iostream>

namespace {
bool check(bool condition, const char *message)
{
    if (!condition) std::cerr << message << std::endl;
    return condition;
}
}

int main()
{
    AgentProfileRecord profile = AgentProfileRepository::DefaultProfile("legacy", "192.0.2.1");
    profile.v1 = true; profile.v2 = true;
    profile.timeout = 7; profile.retries = 4;
    profile.nonrepeaters = 2; profile.maxrepetitions = 25;
    PreferencesSettings defaults;
    defaults.requestTimeout = 12; defaults.requestRetries = 6;
    defaults.bulkNonRepeaters = 3; defaults.bulkMaxRepetitions = 40;
    ProfileMetadataRecord metadata; metadata.profileId = profile.profileId;

    AgentProfileRecord effective = ConnectionRequestSettings::effectiveProfile(
        profile, metadata, defaults);
    if (!check(ConnectionRequestSettings::mode(metadata) == RequestSettingsMode::Legacy &&
                   effective.timeout == 7 && effective.retries == 4 &&
                   effective.nonrepeaters == 2 && effective.maxrepetitions == 25,
               "missing mode did not preserve legacy request settings") ||
        !check(ConnectionRequestSettings::activeProtocol(profile, metadata, 1) == 1,
               "missing active protocol did not preserve legacy selection"))
        return 1;

    metadata.hasRequestSettingsMode = true; metadata.requestSettingsMode = 1;
    effective = ConnectionRequestSettings::effectiveProfile(profile, metadata, defaults);
    if (!check(effective.timeout == 12 && effective.retries == 6 &&
                   effective.nonrepeaters == 3 && effective.maxrepetitions == 40,
               "inherit mode did not use Preferences defaults"))
        return 1;
    defaults.requestTimeout = 20;
    if (!check(ConnectionRequestSettings::effectiveProfile(profile, metadata, defaults).timeout == 20,
               "changed Preferences did not affect a future inherited request"))
        return 1;

    metadata.requestSettingsMode = 2; metadata.overrideTimeout = 9;
    metadata.overrideRetries = 8; metadata.overrideBulkNonRepeaters = 5;
    metadata.overrideBulkMaxRepetitions = 60;
    effective = ConnectionRequestSettings::effectiveProfile(profile, metadata, defaults);
    metadata.hasActiveProtocol = true; metadata.activeProtocol = 0;
    return check(effective.timeout == 9 && effective.retries == 8 &&
                     effective.nonrepeaters == 5 && effective.maxrepetitions == 60,
                 "override mode did not use metadata values") &&
           check(ConnectionRequestSettings::activeProtocol(profile, metadata, 1) == 0,
                 "explicit active protocol was not retained") ? 0 : 1;
}
