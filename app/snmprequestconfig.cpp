#include "snmprequestconfig.h"

QString SnmpRequestConfig::endpoint() const
{
    return address + QLatin1Char('/') + port;
}

bool SnmpRequestConfig::FromProfile(const AgentProfileRecord &profile,
                                    int selectedProtocol,
                                    SnmpRequestConfig *config)
{
    if (!config)
        return false;

    SnmpRequestVersion version;
    if (selectedProtocol == 0)
        version = SnmpRequestVersion::V1;
    else if (selectedProtocol == 1)
        version = SnmpRequestVersion::V2c;
    else if (selectedProtocol == 2)
        version = SnmpRequestVersion::V3;
    else
        return false;

    config->version = version;
    config->address = profile.address;
    config->port = profile.port;
    config->retries = profile.retries;
    config->timeout = profile.timeout;
    config->readCommunity = profile.readcomm;
    config->writeCommunity = profile.writecomm;
    config->securityName = profile.secname;
    config->securityLevel = profile.seclevel;
    config->contextName = profile.contextname;
    config->contextEngineId = profile.contextengineid;
    config->maxRepetitions = profile.maxrepetitions;
    config->nonRepeaters = profile.nonrepeaters;
    return true;
}
