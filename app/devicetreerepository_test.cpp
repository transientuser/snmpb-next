#include "devicetree.h"
#include "devicetreerepository.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <iostream>
#include <algorithm>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

AgentProfileRecord Profile(const QString &id, const QString &name)
{
    AgentProfileRecord profile =
        AgentProfileRepository::DefaultProfile(name, "192.0.2.1");
    profile.profileId = id;
    return profile;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!Check(temporary.isValid(), "temporary directory failed"))
        return 1;

    const QString sidecar = temporary.filePath("device-tree.conf");
    DeviceTreeRepository repository(sidecar);
    if (!Check(repository.Load().folders.isEmpty(), "missing sidecar not empty") ||
        !Check(!QFile::exists(sidecar), "load created missing sidecar"))
        return 1;

    DeviceTreeState source;
    source.rootSortMode = 1;
    source.unfiledSortMode = 2;
    source.folders = {{"dc", "", "Datacenter", 1},
                      {"lab", "dc", "Lab", 2}};
    source.folders[0].sortMode = 2;
    source.placements = {{"p1", "dc", "id-core", "", 3},
                         {"p2", "lab", "id-switch", "", 1}};
    if (!Check(repository.Save(source), "sidecar save failed"))
        return 1;
    DeviceTreeState loaded = repository.Load();
    if (!Check(loaded.folders.size() == 2 && loaded.placements.size() == 2,
               "round-trip count changed") ||
        !Check(loaded.folders[1].parentId == "dc", "hierarchy changed") ||
        !Check(loaded.placements[0].order == 3, "ordering changed") ||
        !Check(loaded.rootSortMode == 1 && loaded.unfiledSortMode == 2 &&
                   loaded.folders[0].sortMode == 2,
               "sort metadata did not round-trip"))
        return 1;

    DeviceTreeState malformed = loaded;
    malformed.folders.append({"dc", "", "duplicate", 0});
    malformed.folders.append({"bad-parent", "missing", "Promoted", -1});
    malformed.folders.append({"self", "self", "Self", 0});
    malformed.placements.append({"p3", "dc", "id-core", "", 0});
    malformed.placements.append({"p4", "missing", "id-new", "", 0});
    malformed.placements.append({"p5", "dc", "id-deleted", "", 0});
    const QList<AgentProfileRecord> profiles = {
        Profile("id-core", "same"), Profile("id-switch", "switch-test"),
        Profile("id-new", "new")};
    const DeviceTreeState reconciled =
        DeviceTree::Reconcile(malformed, profiles);
    if (!Check(reconciled.folders.size() == 4, "folder reconciliation changed") ||
        !Check(reconciled.folders[2].parentId.isEmpty(), "invalid parent retained") ||
        !Check(reconciled.folders[3].parentId.isEmpty(), "self parent retained") ||
        !Check(reconciled.placements.size() == 2, "invalid profile references retained") ||
        !Check(DeviceTree::UnfiledProfiles(reconciled, profiles) ==
                   QStringList{"id-new"},
               "new profile not derived as Unfiled"))
        return 1;

    DeviceTreeState changed = reconciled;
    if (!Check(DeviceTree::MoveProfile(&changed, "id-new", "lab"),
               "profile move failed") ||
        !Check(DeviceTree::UnfiledProfiles(changed, profiles).isEmpty(),
               "moved profile remained Unfiled") ||
        !Check(DeviceTree::DeleteFolder(&changed, "dc"), "folder deletion failed") ||
        !Check(std::none_of(changed.folders.begin(), changed.folders.end(),
                           [](const DeviceFolderRecord &folder) {
                               return folder.id == "dc" || folder.id == "lab";
                           }), "descendant folder not deleted") ||
        !Check(DeviceTree::UnfiledProfiles(changed, profiles).size() == 3,
               "folder deletion did not preserve profiles as Unfiled"))
        return 1;

    if (!Check(source.placements[0].profileId == "id-core",
               "ID reference changed unexpectedly"))
        return 1;

    const QString legacySidecar = temporary.filePath("legacy-device-tree.conf");
    QFile legacyTree(legacySidecar);
    if (!legacyTree.open(QIODevice::WriteOnly | QIODevice::Text))
        return 1;
    legacyTree.write("[schema]\nversion=1\n[profiles]\n"
                     "1\\id=legacy-unique\n1\\parent=dc\n1\\profile=switch-test\n"
                     "2\\id=legacy-ambiguous\n2\\parent=dc\n2\\profile=same\nsize=2\n"
                     "[folders]\n1\\id=dc\n1\\name=Datacenter\nsize=1\n");
    legacyTree.close();
    DeviceTreeRepository legacyRepository(legacySidecar);
    const QList<AgentProfileRecord> duplicateNames = {
        Profile("id-a", "same"), Profile("id-b", "same"),
        Profile("id-switch", "switch-test")};
    const DeviceTreeState migrated =
        DeviceTree::Reconcile(legacyRepository.Load(), duplicateNames);
    if (!Check(migrated.placements.size() == 1,
               "ambiguous legacy reference was silently selected") ||
        !Check(migrated.placements[0].profileId == "id-switch",
               "unique legacy reference did not migrate") ||
        !Check(DeviceTree::UnfiledProfiles(migrated, duplicateNames).size() == 2,
               "ambiguous profiles not preserved in Unfiled"))
        return 1;
    if (!Check(migrated.rootSortMode == 0 && migrated.unfiledSortMode == 0 &&
                   migrated.folders[0].sortMode == 0,
               "legacy tree did not preserve manual ordering defaults"))
        return 1;
    if (!Check(legacyRepository.Save(migrated), "migrated sidecar save failed"))
        return 1;
    QSettings migratedSettings(legacySidecar, QSettings::IniFormat);
    if (!Check(migratedSettings.value("schema/version").toInt() == 2 &&
               migratedSettings.value("profiles/1/profileId").toString() ==
                   "id-switch" &&
               !migratedSettings.contains("profiles/1/profile"),
               "v2 sidecar schema not persisted"))
        return 1;

    const QString agents = temporary.filePath("agents.conf");
    QFile agentsFile(agents);
    if (!agentsFile.open(QIODevice::WriteOnly) || agentsFile.write("legacy") != 6)
        return 1;
    agentsFile.close();
    const QByteArray before = [&]() { QFile f(agents); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    repository.Save(reconciled);
    const QByteArray after = [&]() { QFile f(agents); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    if (!Check(before == after, "device tree rewrote agents.conf"))
        return 1;

    return 0;
}
