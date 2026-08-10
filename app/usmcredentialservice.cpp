#include "usmcredentialservice.h"

#include <QHash>
#include <QUuid>

UsmCredentialService::UsmCredentialService(
    const QList<UsmCredentialRecord> &legacyRecords,
    const UsmCredentialRepository &identityRepository)
    : credentials(legacyRecords), repository(identityRepository)
{
    const QList<UsmCredentialIdentityRecord> stored = repository.load();
    QHash<QString, QList<QString>> idsByName;
    QHash<QString, int> runtimeCounts;
    for (const auto &entry : stored) idsByName[entry.securityName].append(entry.credentialId);
    for (const auto &record : credentials) ++runtimeCounts[record.securityName];
    for (UsmCredentialRecord &record : credentials)
    {
        record.identity.kind = CredentialKind::Usm;
        const QList<QString> ids = idsByName.value(record.securityName);
        if (runtimeCounts.value(record.securityName) == 1 && ids.size() == 1)
            record.identity.credentialId = ids.first();
        else
            record.identity.credentialId = createId();
        if (record.displayName.isEmpty()) record.displayName = record.securityName;
    }
}

const QList<UsmCredentialRecord> &UsmCredentialService::records() const
{
    return credentials;
}

bool UsmCredentialService::saveIdentities() const
{
    QList<UsmCredentialIdentityRecord> identities;
    for (const auto &record : credentials)
        identities.append({record.identity.credentialId, record.securityName});
    return repository.save(identities);
}

bool UsmCredentialService::rename(const QString &id, const QString &name)
{
    const int index = indexOfId(id);
    if (index < 0 || name.trimmed().isEmpty()) return false;
    credentials[index].securityName = name.trimmed();
    credentials[index].displayName = name.trimmed();
    return true;
}

bool UsmCredentialService::remove(const QString &id)
{
    const int index = indexOfId(id);
    if (index < 0) return false;
    credentials.removeAt(index);
    return true;
}

UsmReferenceResult UsmCredentialService::validate(
    const AgentProfileRecord &profile) const
{
    if (!profile.v3) return {UsmReferenceStatus::NotApplicable, {}};
    if (profile.secname.isEmpty()) return {UsmReferenceStatus::Empty, {}};
    QList<const UsmCredentialRecord *> matches;
    for (const auto &record : credentials)
        if (record.securityName == profile.secname) matches.append(&record);
    if (matches.isEmpty()) return {UsmReferenceStatus::Missing, {}};
    if (matches.size() != 1) return {UsmReferenceStatus::Ambiguous, {}};
    const UsmCredentialRecord &record = *matches.first();
    const bool authRequired = profile.seclevel >= 1;
    const bool privacyRequired = profile.seclevel >= 2;
    if ((authRequired && record.authProtocol == 0) ||
        (privacyRequired && record.privacyProtocol == 0))
        return {UsmReferenceStatus::IncompatibleSecurityLevel,
                record.identity.credentialId};
    return {UsmReferenceStatus::Valid, record.identity.credentialId};
}

QString UsmCredentialService::createId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

int UsmCredentialService::indexOfId(const QString &id) const
{
    for (int i = 0; i < credentials.size(); ++i)
        if (credentials[i].identity.credentialId == id) return i;
    return -1;
}
