#include "discoverydestination.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    const QString treeFile = dir.filePath("device-tree.conf");
    DeviceTreeState state;
    state.folders = {{"parent", "", "Datacenter", 0},
                     {"child", "parent", "Core", 1}};
    DeviceTreeRepository(treeFile).Save(state);
    DeviceTreePlacementService service(treeFile);
    int placementSignals = 0;
    QObject::connect(&service, &DeviceTreePlacementService::placementChanged,
                     [&placementSignals](const QString &) { ++placementSignals; });
    const QList<DeviceFolderChoice> choices = service.folderChoices();
    bool ok = choices.size() == 2 && choices[1].displayPath == "Datacenter / Core";
    QSettings settings(dir.filePath("SnmpB.ini"), QSettings::IniFormat);
    ok &= DiscoveryDestinationSettings::load(settings).isEmpty();
    DiscoveryDestinationSettings::save(settings, "child");
    ok &= DiscoveryDestinationSettings::resolve(
              DiscoveryDestinationSettings::load(settings), choices) == "child";
    state.folders[1].name = "Renamed"; DeviceTreeRepository(treeFile).Save(state);
    ok &= DiscoveryDestinationSettings::resolve("child", service.folderChoices()) == "child";
    state.folders.removeLast(); DeviceTreeRepository(treeFile).Save(state);
    ok &= DiscoveryDestinationSettings::resolve("child", service.folderChoices()).isEmpty();
    ok &= !service.placeProfile("profile-a", "child");
    ok &= DeviceTreeRepository(treeFile).Load().placements.isEmpty();
    ok &= service.placeProfile("profile-a", "parent");
    ok &= service.placeProfile("profile-b", "parent");
    const DeviceTreeState placed = DeviceTreeRepository(treeFile).Load();
    ok &= placed.placements.size() == 2 &&
          placed.placements[0].profileId != placed.placements[1].profileId;
    ok &= placementSignals == 2;
    if (!ok) std::cerr << "discovery destination regression failed\n";
    return ok ? 0 : 1;
}
