#ifndef AGENTPROFILEOPERATIONS_H
#define AGENTPROFILEOPERATIONS_H

#include "agentprofilerepository.h"

class AgentProfileEditTransaction
{
public:
    explicit AgentProfileEditTransaction(
        const QList<AgentProfileRecord> &originalRecords);
    const QList<AgentProfileRecord> &rollbackRecords() const;

private:
    QList<AgentProfileRecord> original;
};

class AgentProfileOperations
{
public:
    static bool Duplicate(const QList<AgentProfileRecord> &profiles,
                          const QString &sourceId,
                          AgentProfileRecord *duplicate);
};

#endif
