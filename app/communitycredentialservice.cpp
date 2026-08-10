#include "communitycredentialservice.h"
#include <QSet>
#include <QUuid>

CommunityCredentialService::CommunityCredentialService(
    const QString &credentialFile, const QString &bindingFile, QObject *parent)
    : QObject(parent), credentialRepository(credentialFile),
      bindingRepository(bindingFile), credentials(credentialRepository.load()),
      bindings(bindingRepository.load()) {}

const QList<CommunityCredentialRecord> &CommunityCredentialService::records() const { return credentials; }
const CommunityCredentialRecord *CommunityCredentialService::find(const QString &id) const
{ for (const auto &r : credentials) if (r.identity.credentialId == id) return &r; return nullptr; }

QString CommunityCredentialService::create(const CommunityCredentialRecord &draft)
{
    CommunityCredentialRecord record = draft;
    record.identity.kind = CredentialKind::Community;
    if (record.identity.credentialId.isEmpty() || find(record.identity.credentialId))
        record.identity.credentialId = newId();
    credentials.append(record); if (!saveCredentials()) { credentials.removeLast(); return {}; }
    emit credentialCreated(record.identity.credentialId); emit credentialsChanged();
    return record.identity.credentialId;
}

bool CommunityCredentialService::update(const CommunityCredentialRecord &record)
{
    for (auto &stored : credentials) if (stored.identity.credentialId == record.identity.credentialId)
    {
        const QString oldName = stored.displayName; const auto old = stored; stored = record;
        if (!saveCredentials()) { stored = old; return false; }
        if (oldName != stored.displayName) emit credentialRenamed(stored.identity.credentialId);
        emit credentialUpdated(stored.identity.credentialId); emit credentialsChanged(); return true;
    }
    return false;
}

QString CommunityCredentialService::duplicate(const QString &id)
{ const auto *source = find(id); if (!source) return {}; CommunityCredentialRecord copy = *source;
  copy.displayName += tr(" copy"); return create(copy); }

CommunityDeleteAssessment CommunityCredentialService::assessDelete(const QString &id, int *references) const
{
    int count = 0; for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) if (it.value() == id) ++count;
    if (references) *references = count;
    if (!find(id)) return CommunityDeleteAssessment::Missing;
    return count ? CommunityDeleteAssessment::Referenced : CommunityDeleteAssessment::Unreferenced;
}

bool CommunityCredentialService::remove(const QString &id)
{
    for (int i = 0; i < credentials.size(); ++i) if (credentials[i].identity.credentialId == id)
    { const auto old = credentials.takeAt(i); if (!saveCredentials()) { credentials.insert(i, old); return false; }
      emit credentialDeleted(id); emit credentialsChanged(); return true; }
    return false;
}

bool CommunityCredentialService::bind(const QString &profileId, const QString &id)
{ if (profileId.isEmpty() || id.isEmpty()) return false; const auto old = bindings; bindings.insert(profileId, id);
  if (!saveBindings()) { bindings = old; return false; } emit bindingsChanged(); return true; }
bool CommunityCredentialService::unbind(const QString &profileId)
{ if (!bindings.contains(profileId)) return true; const auto old = bindings; bindings.remove(profileId);
  if (!saveBindings()) { bindings = old; return false; } emit bindingsChanged(); return true; }
QString CommunityCredentialService::binding(const QString &profileId) const { return bindings.value(profileId); }
bool CommunityCredentialService::copyBinding(const QString &source, const QString &target)
{ const QString id = binding(source); return id.isEmpty() ? true : bind(target, id); }
bool CommunityCredentialService::removeProfileBinding(const QString &id) { return unbind(id); }
void CommunityCredentialService::reconcileProfiles(const QStringList &ids)
{
    const QSet<QString> valid(ids.begin(), ids.end());
    const auto old = bindings;
    for (auto it = bindings.begin(); it != bindings.end();)
    {
        if (valid.contains(it.key()))
            ++it;
        else
            it = bindings.erase(it);
    }
    if (bindings != old)
    {
        if (!saveBindings())
        {
            bindings = old;
            return;
        }
        emit bindingsChanged();
    }
}

CommunityCredentialResolution CommunityCredentialService::resolve(const AgentProfileRecord &profile) const
{
    CommunityCredentialResolution result;
    result.values = CredentialResolver::inlineValues(profile.readcomm, profile.writecomm,
                                                     profile.secname, profile.seclevel);
    const QString id = binding(profile.profileId); result.credentialId = id;
    if (id.isEmpty())
    { result.health = profile.readcomm.isEmpty() ? CommunityCredentialHealth::NoReadCommunity :
                      (profile.writecomm.isEmpty() ? CommunityCredentialHealth::NoWriteCommunity :
                                                    CommunityCredentialHealth::Inline); return result; }
    const auto *record = find(id);
    if (!record) { result.health = CommunityCredentialHealth::ReusableMissing;
                   result.usedInlineFallback = true; return result; }
    result.values = CredentialResolver::communityValues(*record);
    result.displayName = record->displayName;
    result.health = record->readCommunity.isEmpty() ? CommunityCredentialHealth::NoReadCommunity :
                    (record->writeCommunity.isEmpty() ? CommunityCredentialHealth::NoWriteCommunity :
                                                        CommunityCredentialHealth::ReusableAvailable);
    return result;
}

QString CommunityCredentialService::healthText(const AgentProfileRecord &profile) const
{
    const auto result = resolve(profile);
    switch (result.health) {
      case CommunityCredentialHealth::Inline: return tr("Inline communities");
      case CommunityCredentialHealth::ReusableAvailable: return tr("Reusable - %1").arg(result.displayName);
      case CommunityCredentialHealth::ReusableMissing: return tr("Reusable credential missing");
      case CommunityCredentialHealth::NoReadCommunity: return tr("No usable read community");
      case CommunityCredentialHealth::NoWriteCommunity: return tr("No usable write community"); }
    return {};
}

QString CommunityCredentialService::newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
bool CommunityCredentialService::saveCredentials() { return credentialRepository.save(credentials); }
bool CommunityCredentialService::saveBindings() { return bindingRepository.save(bindings); }
