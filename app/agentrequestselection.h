#ifndef AGENTREQUESTSELECTION_H
#define AGENTREQUESTSELECTION_H

#include <QList>
#include <QString>

#include "agentprofilerepository.h"
#include "snmprequestconfig.h"

enum class AgentSelectionError
{
    None,
    ProfileNotFound,
    InvalidProtocol,
    UnsupportedProtocol
};

struct AgentRequestSelection
{
    AgentProfileRecord profile;
    int selectedProtocol = 0;
    EffectiveCredentialValues credentials;
    bool hasResolvedCredentials = false;

    bool requestConfig(SnmpRequestConfig *config) const;
};

class AgentSelectionResolver
{
public:
    static QString UniqueProfileIdForName(
        const QList<AgentProfileRecord> &profiles, const QString &profileName);
    static AgentSelectionError ResolveById(
        const QList<AgentProfileRecord> &profiles, const QString &profileId,
        int selectedProtocol, AgentRequestSelection *selection);
    static AgentSelectionError Resolve(const QList<AgentProfileRecord> &profiles,
                                       const QString &profileName,
                                       int selectedProtocol,
                                       AgentRequestSelection *selection);
};

#endif
