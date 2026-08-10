#include "communitycredentialservice.h"
#include "snmprequestconfig.h"
#include "snmprequestcontext.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
AgentProfileRecord profile(const QString &id)
{
    AgentProfileRecord p{}; p.profileId = id; p.name = id; p.v2 = true;
    p.address = "192.0.2.1"; p.port = "161"; p.retries = 2; p.timeout = 4;
    p.readcomm = "legacy-read"; p.writecomm = "legacy-write";
    return p;
}
CommunityCredentialRecord credential(const QString &name)
{
    CommunityCredentialRecord r; r.displayName = name;
    r.readCommunity = CredentialSecret("reusable-read");
    r.writeCommunity = CredentialSecret("reusable-write"); return r;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv); QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");
    const QString credentialsFile = dir.filePath("community-credentials.conf");
    const QString bindingsFile = dir.filePath("credential-bindings.conf");
    CommunityCredentialService service(credentialsFile, bindingsFile);
    ok &= check(service.records().isEmpty() && !QFile::exists(credentialsFile) &&
                !QFile::exists(bindingsFile), "missing stores/read-only load");
    const QString first = service.create(credential("shared"));
    const QString second = service.create(credential("shared"));
    ok &= check(!first.isEmpty() && !second.isEmpty() && first != second &&
                service.records().size() == 2, "stable IDs/duplicate display names");
    CommunityCredentialRecord edited = *service.find(first);
    edited.displayName = "renamed";
    edited.readCommunity = CredentialSecret("updated-read");
    ok &= check(service.update(edited) && service.find(first)->displayName == "renamed",
                "update/rename preserves ID");
    const QString copy = service.duplicate(first);
    ok &= check(!copy.isEmpty() && copy != first, "duplicate gets new ID");

    CommunityCredentialService reloaded(credentialsFile, bindingsFile);
    ok &= check(reloaded.records().size() == 3 &&
                reloaded.find(first) && reloaded.find(first)->displayName == "renamed",
                "credential repository round-trip");

    AgentProfileRecord source = profile("profile-a");
    AgentProfileRecord duplicate = profile("profile-b");
    ok &= check(service.resolve(source).health == CommunityCredentialHealth::Inline &&
                service.bind(source.profileId, first) &&
                service.copyBinding(source.profileId, duplicate.profileId) &&
                service.binding(duplicate.profileId) == first,
                "bind and duplicate shares reusable credential");
    const auto resolved = service.resolve(source);
    ok &= check(resolved.health == CommunityCredentialHealth::ReusableAvailable &&
                resolved.values.readCommunity == "updated-read" &&
                source.readcomm == "legacy-read", "reusable resolution leaves inline intact");
    CommunityCredentialService bindingsReloaded(credentialsFile, bindingsFile);
    ok &= check(bindingsReloaded.binding(source.profileId) == first &&
                bindingsReloaded.binding(duplicate.profileId) == first,
                "binding repository round-trip");

    AgentProfileRecord legacyV1 = profile("legacy-v1");
    legacyV1.v1 = true; legacyV1.v2 = false;
    AgentProfileRecord legacyV2 = profile("legacy-v2");
    const auto v1 = service.resolve(legacyV1);
    const auto v2 = service.resolve(legacyV2);
    ok &= check(v1.health == CommunityCredentialHealth::Inline &&
                v2.health == CommunityCredentialHealth::Inline &&
                v1.values.readCommunity == "legacy-read" &&
                v2.values.writeCommunity == "legacy-write",
                "legacy v1/v2 inline equivalence");
    AgentProfileRecord v3Profile = profile("v3");
    v3Profile.v2 = false; v3Profile.v3 = true;
    v3Profile.secname = "v3-user"; v3Profile.seclevel = 2;
    const auto v3 = service.resolve(v3Profile);
    ok &= check(v3.values.securityName == "v3-user" &&
                v3.values.securityLevel == 2,
                "v3 credential references remain unchanged");
    SnmpRequestConfig config;
    SnmpRequestConfig::FromProfile(source, 1, resolved.values, &config);
    SnmpRequestContext captured(config, SnmpRequestOperation::Get);
    edited = *service.find(first); edited.readCommunity = CredentialSecret("next-read");
    ok &= check(service.update(edited) &&
                captured.config().readCommunity == "updated-read" &&
                service.resolve(source).values.readCommunity == "next-read",
                "credential updates affect next request, not captured context");

    int references = 0;
    ok &= check(service.assessDelete(first, &references) ==
                    CommunityDeleteAssessment::Referenced && references == 2,
                "referenced delete assessment");
    ok &= check(service.remove(first) && service.resolve(source).health ==
                    CommunityCredentialHealth::ReusableMissing &&
                service.resolve(source).usedInlineFallback &&
                service.resolve(source).values.readCommunity == "legacy-read",
                "confirmed delete preserves broken binding with inline fallback");
    QFile bindingFile(bindingsFile);
    const bool bindingOpened = bindingFile.open(QIODevice::ReadOnly);
    const QByteArray bindingBytes = bindingFile.readAll(); bindingFile.close();
    ok &= check(bindingOpened && !bindingBytes.contains("legacy-read") &&
                !bindingBytes.contains("reusable-read") &&
                !bindingBytes.contains("updated-read") &&
                !bindingBytes.contains("next-read"), "binding store contains no secrets");
    ok &= check(service.unbind(source.profileId) &&
                service.resolve(source).health == CommunityCredentialHealth::Inline &&
                service.removeProfileBinding(duplicate.profileId), "unbind/delete profile cleanup");
    service.reconcileProfiles({});
    return ok ? 0 : 1;
}
