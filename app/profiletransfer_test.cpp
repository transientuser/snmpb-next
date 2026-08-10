#include "profiletransfer.h"
#include "devicetreerepository.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
AgentProfileRecord profile(const QString &id, const QString &name)
{
    AgentProfileRecord p = AgentProfileRepository::DefaultProfile(name, "192.0.2.8");
    p.profileId = id; p.v2 = true; p.secname = "unavailable-usm-reference";
    p.readcomm = "read-secret"; p.writecomm = "write-secret";
    return p;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    bool ok = true;
    ProfileTransferDocument source;
    source.profiles = {profile("profile-a", "same"), profile("profile-b", "same")};
    source.metadata = {{"profile-a", "core note", {"Core", "Lab"},
                        {"IF-MIB", "SNMPv2-MIB"}},
                       {"profile-b", "edge note", {"Edge"}, {}}};
    source.tree.folders = {{"folder-root", "", "Datacenter", 0},
                           {"folder-child", "folder-root", "Core", 1}};
    source.tree.placements = {{"place-a", "folder-child", "profile-a", "", 2},
                              {"place-b", "folder-root", "profile-b", "", 3}};
    const ProfileTransferDocument before = source;
    const QByteArray json = ProfileTransfer::exportJson(source);
    ok &= check(json.contains("snmpb-next-profile-transfer") && json.contains("\"version\": 2"),
                "schema and version missing");
    ok &= check(!json.contains("read-secret") && !json.contains("write-secret") &&
                !json.contains("readcomm") && !json.contains("writecomm") &&
                !json.contains("communityCredentialId") &&
                !json.contains("credential-bindings") &&
                json.contains("credentialsOmitted"), "default export exposed credentials");
    ok &= check(source.profiles.first().readcomm == before.profiles.first().readcomm,
                "export mutated source state");

    const ProfileTransferDocument selected = ProfileTransfer::selectProfiles(source, {"profile-a"});
    ok &= check(selected.profiles.size() == 1 && selected.metadata.size() == 1 &&
                selected.tree.placements.first().parentId.isEmpty(), "selected-profile export");
    const ProfileTransferDocument subtree = ProfileTransfer::selectFolder(source, "folder-root");
    ok &= check(subtree.profiles.size() == 2 && subtree.tree.folders.size() == 2 &&
                subtree.tree.placements.size() == 2, "folder/subtree export");

    ProfileImportPlan plan;
    ok &= check(ProfileTransfer::planImport(json, {}, {}, &plan) == ProfileTransferError::None &&
                plan.profiles.size() == 2 && plan.metadata.size() == 2 &&
                plan.tree.folders.size() == 2 && plan.tree.placements.size() == 2,
                "valid import round-trip plan");
    ok &= check(plan.profiles.first().readcomm.isEmpty() &&
                plan.profiles.first().writecomm.isEmpty() &&
                plan.profiles.first().secname == "unavailable-usm-reference",
                "credential-free/missing USM reference import policy");
    ok &= check(plan.metadata.first().preferredMibs ==
                    QStringList({"IF-MIB", "SNMPv2-MIB"}) &&
                !json.contains("BEGIN") && !json.contains("C:\\\\"),
                "preferred MIB transfer exported content/path or lost names");
    QTemporaryDir directory;
    const QString agentsFile = directory.filePath("agents.conf");
    const QString metadataFile = directory.filePath("profile-metadata.conf");
    const QString treeFile = directory.filePath("device-tree.conf");
    AgentProfileRepository(agentsFile).Save({});
    QString applyError;
    ok &= check(ProfileImportStorage::apply(plan, agentsFile, metadataFile,
                                            treeFile, {}, {}, {}, &applyError),
                "transactional import apply");
    ok &= check(AgentProfileRepository(agentsFile).Load().size() == 2 &&
                ProfileMetadataRepository(metadataFile).load().size() == 2 &&
                DeviceTreeRepository(treeFile).Load().folders.size() == 2,
                "import apply did not persist all boundaries");

    const QList<AgentProfileRecord> existingProfiles = {profile("profile-a", "existing")};
    DeviceTreeState existingTree; existingTree.folders = {{"folder-root", "", "Existing", 0}};
    ProfileImportPlan remapped;
    ok &= check(ProfileTransfer::planImport(json, existingProfiles, existingTree, &remapped) ==
                    ProfileTransferError::None &&
                remapped.profileIdMap.value("profile-a") != "profile-a" &&
                remapped.folderIdMap.value("folder-root") != "folder-root" &&
                remapped.metadata.first().profileId == remapped.profileIdMap.value("profile-a"),
                "profile/folder collision remapping");
    ok &= check(remapped.metadata.first().preferredMibs.contains("IF-MIB"),
                "ID remapping lost preferred MIB associations");
    ok &= check(remapped.profiles[1].name == "same",
                "same-name different-ID profile was not retained");

    ProfileImportPlan sentinel; sentinel.profileIdMap["unchanged"] = "unchanged";
    ok &= check(ProfileTransfer::planImport("not json", {}, {}, &sentinel) ==
                    ProfileTransferError::InvalidJson && sentinel.profileIdMap.contains("unchanged"),
                "malformed input changed proposed state");
    QJsonObject future = QJsonDocument::fromJson(json).object(); future["version"] = 99;
    ok &= check(ProfileTransfer::planImport(QJsonDocument(future).toJson(), {}, {}, &sentinel) ==
                    ProfileTransferError::UnsupportedVersion,
                "future version accepted");
    QJsonObject legacy = QJsonDocument::fromJson(json).object(); legacy["version"] = 1;
    QJsonArray legacyMetadata = legacy["metadata"].toArray();
    for (int i = 0; i < legacyMetadata.size(); ++i)
    {
        QJsonObject record = legacyMetadata[i].toObject();
        record.remove("preferredMibs"); legacyMetadata[i] = record;
    }
    legacy["metadata"] = legacyMetadata;
    ProfileImportPlan legacyPlan;
    ok &= check(ProfileTransfer::planImport(QJsonDocument(legacy).toJson(), {}, {},
                                            &legacyPlan) == ProfileTransferError::None &&
                legacyPlan.metadata.first().preferredMibs.isEmpty(),
                "legacy transfer v1 compatibility");
    QJsonObject duplicate = QJsonDocument::fromJson(json).object();
    QJsonArray profiles = duplicate["profiles"].toArray(); profiles.append(profiles.first());
    duplicate["profiles"] = profiles;
    ok &= check(ProfileTransfer::planImport(QJsonDocument(duplicate).toJson(), {}, {}, &sentinel) ==
                    ProfileTransferError::DuplicateId, "duplicate import ID accepted");
    QJsonObject missingParent = QJsonDocument::fromJson(json).object();
    QJsonArray folders = missingParent["folders"].toArray();
    QJsonObject child = folders[1].toObject(); child["parentId"] = "missing";
    folders[1] = child; missingParent["folders"] = folders;
    ok &= check(ProfileTransfer::planImport(QJsonDocument(missingParent).toJson(), {}, {}, &sentinel) ==
                    ProfileTransferError::MissingFolderParent,
                "missing folder parent accepted");
    return ok ? 0 : 1;
}
