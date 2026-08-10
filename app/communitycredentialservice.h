#ifndef COMMUNITYCREDENTIALSERVICE_H
#define COMMUNITYCREDENTIALSERVICE_H

#include "communitybindingrepository.h"
#include "communitycredentialrepository.h"
#include "agentprofilerepository.h"
#include <QObject>

enum class CommunityCredentialHealth
{
    Inline,
    ReusableAvailable,
    ReusableMissing,
    NoReadCommunity,
    NoWriteCommunity
};

struct CommunityCredentialResolution
{
    EffectiveCredentialValues values;
    CommunityCredentialHealth health = CommunityCredentialHealth::Inline;
    QString credentialId;
    QString displayName;
    bool usedInlineFallback = false;
};

enum class CommunityDeleteAssessment { Missing, Unreferenced, Referenced };

class CommunityCredentialService : public QObject
{
    Q_OBJECT
public:
    CommunityCredentialService(const QString &credentialFile,
                               const QString &bindingFile,
                               QObject *parent = nullptr);
    const QList<CommunityCredentialRecord> &records() const;
    const CommunityCredentialRecord *find(const QString &credentialId) const;
    QString create(const CommunityCredentialRecord &draft);
    bool update(const CommunityCredentialRecord &record);
    QString duplicate(const QString &credentialId);
    CommunityDeleteAssessment assessDelete(const QString &credentialId,
                                            int *references = nullptr) const;
    bool remove(const QString &credentialId);
    bool bind(const QString &profileId, const QString &credentialId);
    bool unbind(const QString &profileId);
    QString binding(const QString &profileId) const;
    bool copyBinding(const QString &sourceProfileId, const QString &newProfileId);
    bool removeProfileBinding(const QString &profileId);
    void reconcileProfiles(const QStringList &profileIds);
    CommunityCredentialResolution resolve(const AgentProfileRecord &profile) const;
    QString healthText(const AgentProfileRecord &profile) const;

signals:
    void credentialCreated(const QString &credentialId);
    void credentialUpdated(const QString &credentialId);
    void credentialRenamed(const QString &credentialId);
    void credentialDeleted(const QString &credentialId);
    void bindingsChanged();
    void credentialsChanged();

private:
    static QString newId();
    bool saveCredentials();
    bool saveBindings();
    CommunityCredentialRepository credentialRepository;
    CommunityBindingRepository bindingRepository;
    QList<CommunityCredentialRecord> credentials;
    QHash<QString, QString> bindings;
};

#endif
