#include "credentialrecords.h"
#include "snmprequestconfig.h"
#include "usmcredentialservice.h"

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
    const QString identityFile = dir.filePath("credential-identities.conf");
    UsmCredentialRepository repository(identityFile);

    UsmCredentialService first({usm("operator")}, repository);
    ok &= check(!QFile::exists(identityFile), "read-only construction persisted data");
    const QString stableId = first.records().first().identity.credentialId;
    ok &= check(!stableId.isEmpty() && first.saveIdentities(), "identity save");
    QFile identityContents(identityFile);
    ok &= check(identityContents.open(QIODevice::ReadOnly) &&
                !identityContents.readAll().contains("auth-value"),
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

    UsmCredentialService ambiguous({usm("duplicate"), usm("duplicate")}, repository);
    ok &= check(ambiguous.validate(profile("duplicate")).status ==
                    UsmReferenceStatus::Ambiguous &&
                ambiguous.records()[0].identity.credentialId !=
                    ambiguous.records()[1].identity.credentialId,
                "ambiguous names are not merged");
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
