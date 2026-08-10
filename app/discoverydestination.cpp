#include "discoverydestination.h"

#include <QHash>
#include <QSet>
#include <algorithm>

QString DiscoveryDestinationSettings::load(QSettings &settings)
{
    return settings.value("discovery/destinationfolderid").toString();
}

void DiscoveryDestinationSettings::save(QSettings &settings,
                                         const QString &folderId)
{
    if (folderId.isEmpty()) settings.remove("discovery/destinationfolderid");
    else settings.setValue("discovery/destinationfolderid", folderId);
}

QString DiscoveryDestinationSettings::resolve(
    const QString &saved, const QList<DeviceFolderChoice> &folders)
{
    for (const DeviceFolderChoice &folder : folders)
        if (folder.folderId == saved) return saved;
    return {};
}

DeviceTreePlacementService::DeviceTreePlacementService(const QString &fileName,
                                                       QObject *parent)
    : QObject(parent), repository(fileName)
{
}

QList<DeviceFolderChoice> DeviceTreePlacementService::folderChoices() const
{
    const DeviceTreeState state = repository.Load();
    QHash<QString, DeviceFolderRecord> byId;
    for (const DeviceFolderRecord &folder : state.folders) byId.insert(folder.id, folder);
    QList<DeviceFolderChoice> result;
    for (const DeviceFolderRecord &folder : state.folders)
    {
        QStringList parts{folder.name};
        QString parent = folder.parentId;
        QSet<QString> visited{folder.id};
        while (!parent.isEmpty() && byId.contains(parent) && !visited.contains(parent))
        {
            visited.insert(parent); parts.prepend(byId[parent].name);
            parent = byId[parent].parentId;
        }
        result.append({folder.id, parts.join(QStringLiteral(" / "))});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.displayPath.compare(b.displayPath, Qt::CaseInsensitive) < 0;
    });
    return result;
}

bool DeviceTreePlacementService::placeProfile(const QString &profileId,
                                              const QString &folderId)
{
    if (profileId.isEmpty()) return false;
    if (folderId.isEmpty()) return true;
    DeviceTreeState state = repository.Load();
    bool folderExists = false;
    for (const DeviceFolderRecord &folder : state.folders)
        if (folder.id == folderId) { folderExists = true; break; }
    if (!folderExists || !DeviceTree::MoveProfile(&state, profileId, folderId))
        return false;
    if (!repository.Save(state)) return false;
    emit placementChanged(profileId);
    return true;
}
