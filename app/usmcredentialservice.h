#ifndef USMCREDENTIALSERVICE_H
#define USMCREDENTIALSERVICE_H

#include "credentialrecords.h"
#include "usmcredentialrepository.h"
#include "agentprofilerepository.h"

enum class UsmReferenceStatus
{
    NotApplicable,
    Empty,
    Valid,
    Missing,
    Ambiguous,
    IncompatibleSecurityLevel
};

struct UsmReferenceResult
{
    UsmReferenceStatus status = UsmReferenceStatus::NotApplicable;
    QString credentialId;
};

class UsmCredentialService
{
public:
    UsmCredentialService(const QList<UsmCredentialRecord> &legacyRecords,
                         const UsmCredentialRepository &repository);

    const QList<UsmCredentialRecord> &records() const;
    bool saveIdentities() const;
    bool rename(const QString &credentialId, const QString &securityName);
    bool remove(const QString &credentialId);
    UsmReferenceResult validate(const AgentProfileRecord &profile) const;

private:
    static QString createId();
    int indexOfId(const QString &credentialId) const;
    QList<UsmCredentialRecord> credentials;
    UsmCredentialRepository repository;
};

#endif
