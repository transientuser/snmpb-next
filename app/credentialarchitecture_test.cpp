#include "credentialrecords.h"
#include "snmprequestconfig.h"
#include "usmcredentialservice.h"
#include "usmcredentialcoordinator.h"
#include "agentprofileservice.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}

UsmCredentialRecord usm(const QString &name, int auth = 1, int privacy = 1)
{
    UsmCredentialRecord record;
    record.securityName = name;
    record.authProtocol = auth;
    record.privacyProtocol = privacy;
    record.authSecret = CredentialSecret("auth-value");
    record.privacySecret = CredentialSecret("privacy-value");
    return record;
}

AgentProfileRecord profile(const QString &name, int level = 1)
{
    AgentProfileRecord record{};
    record.v3 = true;
    record.secname = name;
    record.seclevel = level;
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");
    UsmCredentialRecord noAuth = usm("no-auth", 0, 0);
    noAuth.authSecret = {}; noAuth.privacySecret = {};
    UsmCredentialRecord authNoPriv = usm("auth-only", 4, 0);
    authNoPriv.authSecret = {}; authNoPriv.privacySecret = {};
    UsmCredentialRecord authPriv = usm("auth-priv", 4, 3);
    authPriv.authSecret = {}; authPriv.privacySecret = {};
    ok &= check(UsmCredentialService::requirementsSatisfied(noAuth),
                "noAuth/noPriv rejected a valid security name");
    ok &= check(!UsmCredentialService::requirementsSatisfied(authNoPriv),
                "AuthNoPriv accepted a missing authentication secret");
    ok &= check(!UsmCredentialService::requirementsSatisfied(authPriv),
                "AuthPriv accepted missing authentication/privacy secrets");
    authNoPriv.authSecret = CredentialSecret("auth-secret");
    authPriv.authSecret = CredentialSecret("auth-secret");
    authPriv.privacySecret = CredentialSecret("privacy-secret");
    ok &= check(UsmCredentialService::requirementsSatisfied(authNoPriv),
                "AuthNoPriv rejected complete requirements");
    ok &= check(UsmCredentialService::requirementsSatisfied(authPriv),
                "AuthPriv rejected complete requirements");
    const QString identityFile = dir.filePath("credential-identities.conf");
    UsmCredentialRepository repository(identityFile);

    UsmCredentialService first({usm("operator")}, repository);
    ok &= check(!QFile::exists(identityFile), "read-only construction persisted data");
    const QString stableId = first.records().first().identity.credentialId;
    ok &= check(!stableId.isEmpty() && first.saveIdentities(), "identity save");
    QFile identityContents(identityFile);
    const bool identityOpened = identityContents.open(QIODevice::ReadOnly);
    const QByteArray identityBytes = identityOpened ? identityContents.readAll()
                                                    : QByteArray();
    ok &= check(identityOpened && !identityBytes.contains("auth-value") &&
                !identityBytes.contains("privacy-value"),
                "identity sidecar contains no secrets");
    identityContents.close();

    UsmCredentialService reloaded({usm("operator")}, repository);
    ok &= check(reloaded.records().first().identity.credentialId == stableId,
                "stable identity round-trip");
    ok &= check(reloaded.validate(profile("operator")).status ==
                    UsmReferenceStatus::Valid,
                "valid legacy security-name reference");
    ok &= check(reloaded.validate(profile("missing")).status ==
                    UsmReferenceStatus::Missing,
                "missing reference remains diagnosable");
    ok &= check(reloaded.validate(profile("operator", 2)).status ==
                    UsmReferenceStatus::Valid,
                "compatible auth/privacy level");

    const QByteArray authBefore = reloaded.records().first().authSecret.bytes();
    ok &= check(reloaded.rename(stableId, "renamed") &&
                reloaded.records().first().identity.credentialId == stableId &&
                reloaded.records().first().authSecret.bytes() == authBefore,
                "rename preserves identity and secret value");
    ok &= check(reloaded.validate(profile("operator")).status ==
                    UsmReferenceStatus::Missing &&
                reloaded.validate(profile("renamed")).status ==
                    UsmReferenceStatus::Valid,
                "rename reference behavior");
    ok &= check(reloaded.saveIdentities(), "renamed identity save");
    UsmCredentialService renamedReload({usm("renamed")}, repository);
    ok &= check(renamedReload.records().first().identity.credentialId == stableId,
                "rename remains stable across save/load");
    ProfileMetadataRecord explicitBinding;
    explicitBinding.profileId = "explicit-profile";
    explicitBinding.usmCredentialId = stableId;
    ok &= check(renamedReload.validate(profile("stale-shadow"), explicitBinding).status ==
                    UsmReferenceStatus::Valid,
                "explicit stable ID remains valid across credential rename");
    explicitBinding.usmCredentialId = "broken-id";
    ok &= check(renamedReload.validate(profile("renamed"), explicitBinding).status ==
                    UsmReferenceStatus::Missing,
                "broken explicit ID did not silently rebind by security name");

    QList<UsmCredentialRecord> working = renamedReload.records();
    working.first().authProtocol = 2;
    working.first().authSecret = CredentialSecret("changed-working-secret");
    const UsmCredentialRecord added = renamedReload.createWorkingRecord("new-user");
    working.append(added);
    ok &= check(renamedReload.records().size() == 1 &&
                renamedReload.records().first().authSecret.bytes() !=
                    working.first().authSecret.bytes() &&
                !added.identity.credentialId.isEmpty() &&
                added.identity.credentialId != stableId,
                "cancelled existing/new working records do not mutate service");

    QList<UsmCredentialRecord> runtimeState = renamedReload.records();
    QList<UsmCredentialRecord> identityState = renamedReload.records();
    auto runtimeWriter = [&runtimeState](const QList<UsmCredentialRecord> &records) {
        runtimeState = records; return true;
    };
    auto identityWriter = [&identityState](const QList<UsmCredentialRecord> &records) {
        identityState = records; return true;
    };
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), working, runtimeWriter,
                    identityWriter).status == UsmCommitStatus::Success &&
                runtimeState.size() == 2 && identityState.size() == 2,
                "coordinated save");
    runtimeState = renamedReload.records(); identityState = renamedReload.records();
    bool failRuntimeOnce = true;
    auto failingRuntime = [&runtimeState, &failRuntimeOnce](
                              const QList<UsmCredentialRecord> &records) {
        if (failRuntimeOnce) { failRuntimeOnce = false; return false; }
        runtimeState = records; return true;
    };
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), working, failingRuntime,
                    identityWriter).status ==
                    UsmCommitStatus::RuntimePersistenceFailed &&
                runtimeState.size() == 1,
                "runtime failure rollback");
    runtimeState = renamedReload.records(); identityState = renamedReload.records();
    bool failIdentityOnce = true;
    auto failingIdentity = [&identityState, &failIdentityOnce](
                               const QList<UsmCredentialRecord> &records) {
        if (failIdentityOnce) { failIdentityOnce = false; return false; }
        identityState = records; return true;
    };
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), working, runtimeWriter,
                    failingIdentity).status ==
                    UsmCommitStatus::IdentityPersistenceFailed &&
                runtimeState.size() == 1 && identityState.size() == 1,
                "identity failure rolls back both boundaries");
    QList<UsmCredentialRecord> failedRename = renamedReload.records();
    failedRename.first().securityName = "must-not-commit";
    runtimeState = renamedReload.records(); identityState = renamedReload.records();
    failIdentityOnce = true;
    ok &= check(UsmCredentialCoordinator::apply(
                    renamedReload.records(), failedRename, runtimeWriter,
                    failingIdentity).status ==
                    UsmCommitStatus::IdentityPersistenceFailed &&
                runtimeState.first().securityName == "renamed" &&
                identityState.first().securityName == "renamed" &&
                renamedReload.records().first().securityName == "renamed",
                "rename failure rollback");

    int createdSignals = 0, updatedSignals = 0, renamedSignals = 0,
        deletedSignals = 0;
    QObject::connect(&renamedReload, &UsmCredentialService::credentialCreated,
                     [&createdSignals](const QString &) { ++createdSignals; });
    QObject::connect(&renamedReload, &UsmCredentialService::credentialUpdated,
                     [&updatedSignals](const QString &) { ++updatedSignals; });
    QObject::connect(&renamedReload, &UsmCredentialService::credentialRenamed,
                     [&renamedSignals](const QString &, const QString &,
                                       const QString &) { ++renamedSignals; });
    QObject::connect(&renamedReload, &UsmCredentialService::credentialDeleted,
                     [&deletedSignals](const QString &) { ++deletedSignals; });
    working.first().securityName = "renamed-again";
    renamedReload.applyCommitted(working);
    ok &= check(createdSignals == 1 && updatedSignals == 1 &&
                renamedSignals == 1 &&
                renamedReload.records().first().identity.credentialId == stableId,
                "identity-based create/update/rename notifications");

    const QString agentsFile = dir.filePath("agents.conf");
    AgentProfileRepository(agentsFile).Save(
        {profile("renamed-again"), profile("renamed-again")});
    AgentProfileService profiles(agentsFile);
    int references = 0;
    ok &= check(renamedReload.assessDelete(
                    stableId, profiles.profiles(), &references) ==
                    UsmDeleteAssessment::Referenced && references == 2,
                "referenced delete assessment");
    AgentProfileRecord explicitProfile = profile("unrelated-shadow");
    explicitProfile.profileId = "explicit-profile";
    ProfileMetadataRecord deleteBinding;
    deleteBinding.profileId = explicitProfile.profileId;
    deleteBinding.usmCredentialId = stableId;
    ok &= check(renamedReload.assessDelete(stableId, {explicitProfile}, {deleteBinding},
                                           &references) ==
                    UsmDeleteAssessment::Referenced && references == 1,
                "stable metadata reference participates in delete safety");
    ok &= check(profiles.renameSecurityNameReferences(
                    "renamed-again", "propagated") &&
                profiles.securityNameReferenceIds("propagated").size() == 2,
                "rename propagates to multiple Agent Profiles");
    QList<UsmCredentialRecord> afterDelete = working;
    afterDelete.removeFirst();
    renamedReload.applyCommitted(afterDelete);
    ok &= check(deletedSignals == 1 && profiles.profiles().size() == 2 &&
                profiles.securityNameReferenceIds("propagated").size() == 2,
                "confirmed delete notification preserves Agent Profiles");

    UsmCredentialService ambiguous({usm("duplicate"), usm("duplicate")}, repository);
    ok &= check(ambiguous.validate(profile("duplicate")).status ==
                    UsmReferenceStatus::Ambiguous &&
                ambiguous.records()[0].identity.credentialId !=
                    ambiguous.records()[1].identity.credentialId,
                "ambiguous names are not merged");
    ok &= check(!ambiguous.isSecurityNameUnambiguous("duplicate"),
                "ambiguous rename references are not eligible for propagation");
    UsmCredentialService incompatible({usm("noauth", 0, 0)}, repository);
    ok &= check(incompatible.validate(profile("noauth", 1)).status ==
                    UsmReferenceStatus::IncompatibleSecurityLevel,
                "security-level compatibility");
    ok &= check(reloaded.remove(stableId) &&
                reloaded.validate(profile("renamed")).status ==
                    UsmReferenceStatus::Missing,
                "delete leaves reference missing rather than rewriting profile");

    CommunityCredentialRecord community;
    community.identity = {"community-id", CredentialKind::Community};
    community.displayName = "read/write";
    community.readCommunity = CredentialSecret("inline-read");
    community.writeCommunity = CredentialSecret("inline-write");
    AgentProfileRecord legacy{};
    legacy.address = "192.0.2.1"; legacy.port = "161";
    legacy.retries = 2; legacy.timeout = 5; legacy.maxrepetitions = 20;
    legacy.nonrepeaters = 1; legacy.readcomm = "inline-read";
    legacy.writecomm = "inline-write"; legacy.secname = "operator";
    legacy.seclevel = 1; legacy.contextname = "ctx";
    legacy.contextengineid = "engine";
    SnmpRequestConfig inlineConfig, reusableConfig;
    ok &= check(SnmpRequestConfig::FromProfile(legacy, 1, &inlineConfig) &&
                SnmpRequestConfig::FromProfile(
                    legacy, 1, CredentialResolver::communityValues(community),
                    &reusableConfig) &&
                inlineConfig.readCommunity == reusableConfig.readCommunity &&
                inlineConfig.writeCommunity == reusableConfig.writeCommunity &&
                inlineConfig.address == reusableConfig.address &&
                inlineConfig.retries == reusableConfig.retries &&
                inlineConfig.timeout == reusableConfig.timeout &&
                inlineConfig.maxRepetitions == reusableConfig.maxRepetitions,
                "community abstraction preserves effective request configuration");
    const EffectiveCredentialValues legacyValues = CredentialResolver::inlineValues(
        legacy.readcomm, legacy.writecomm, legacy.secname, legacy.seclevel);
    for (int protocol = 0; protocol < 3; ++protocol)
    {
        SnmpRequestConfig original, resolved;
        ok &= check(SnmpRequestConfig::FromProfile(legacy, protocol, &original) &&
                    SnmpRequestConfig::FromProfile(legacy, protocol, legacyValues,
                                                   &resolved) &&
                    original.version == resolved.version &&
                    original.readCommunity == resolved.readCommunity &&
                    original.writeCommunity == resolved.writeCommunity &&
                    original.securityName == resolved.securityName &&
                    original.securityLevel == resolved.securityLevel &&
                    original.contextName == resolved.contextName &&
                    original.contextEngineId == resolved.contextEngineId &&
                    original.retries == resolved.retries &&
                    original.timeout == resolved.timeout &&
                    original.maxRepetitions == resolved.maxRepetitions &&
                    original.nonRepeaters == resolved.nonRepeaters,
                    "v1/v2c/v3 legacy credential resolution equivalence");
    }

    return ok ? 0 : 1;
}
