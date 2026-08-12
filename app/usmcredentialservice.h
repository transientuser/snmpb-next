#ifndef USMCREDENTIALSERVICE_H
#define USMCREDENTIALSERVICE_H

#include "credentialrecords.h"
#include "usmcredentialrepository.h"
#include "agentprofilerepository.h"
#include "profilemetadatarepository.h"
#include <QObject>

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

enum class UsmDeleteAssessment { NotFound, Unreferenced, Referenced, Ambiguous };

class UsmCredentialService : public QObject
{
    Q_OBJECT
public:
    UsmCredentialService(const QList<UsmCredentialRecord> &legacyRecords,
                         const UsmCredentialRepository &repository,
                         QObject *parent = nullptr);

    const QList<UsmCredentialRecord> &records() const;
    bool saveIdentities() const;
    bool rename(const QString &credentialId, const QString &securityName);
    bool remove(const QString &credentialId);
    UsmReferenceResult validate(const AgentProfileRecord &profile) const;
    UsmReferenceResult validate(const AgentProfileRecord &profile,
                                const ProfileMetadataRecord &metadata) const;
    const UsmCredentialRecord *find(const QString &credentialId) const;
    UsmCredentialRecord createWorkingRecord(const QString &securityName) const;
    bool validateWorkingCopy(const QList<UsmCredentialRecord> &records) const;
    static int securityLevel(const UsmCredentialRecord &record);
    static bool requirementsSatisfied(const UsmCredentialRecord &record);
    void applyCommitted(const QList<UsmCredentialRecord> &records);
    UsmDeleteAssessment assessDelete(
        const QString &credentialId,
        const QList<AgentProfileRecord> &profiles,
        int *referenceCount = nullptr) const;
    UsmDeleteAssessment assessDelete(
        const QString &credentialId,
        const QList<AgentProfileRecord> &profiles,
        const QList<ProfileMetadataRecord> &metadata,
        int *referenceCount = nullptr) const;
    bool isSecurityNameUnambiguous(const QString &securityName) const;

signals:
    void credentialCreated(const QString &credentialId);
    void credentialUpdated(const QString &credentialId);
    void credentialRenamed(const QString &credentialId,
                           const QString &oldName, const QString &newName);
    void credentialDeleted(const QString &credentialId);
    void credentialsReloaded();
    void credentialsChanged();

private:
    static QString createId();
    int indexOfId(const QString &credentialId) const;
    QList<UsmCredentialRecord> credentials;
    UsmCredentialRepository repository;
};

#endif
