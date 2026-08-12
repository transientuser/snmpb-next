#include "devicetree.h"

#include <QHash>
#include <QSet>
#include <QUuid>

namespace
{
QString NewId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

}

DeviceTreeState DeviceTree::Reconcile(const DeviceTreeState &state,
                                      const QList<AgentProfileRecord> &profiles)
{
    DeviceTreeState result;
    result.rootSortMode = qBound(0, state.rootSortMode, 2);
    result.unfiledSortMode = qBound(0, state.unfiledSortMode, 2);
    QSet<QString> usedIds;
    QSet<QString> folderIds;

    for (const DeviceFolderRecord &source : state.folders)
    {
        if (source.id.isEmpty() || source.name.isEmpty() ||
            usedIds.contains(source.id))
            continue;
        DeviceFolderRecord folder = source;
        folder.order = qMax(0, folder.order);
        folder.sortMode = qBound(0, folder.sortMode, 2);
        result.folders.append(folder);
        usedIds.insert(folder.id);
        folderIds.insert(folder.id);
    }

    // Invalid parents and cycles are safely promoted to the root.
    for (DeviceFolderRecord &folder : result.folders)
    {
        if (folder.parentId.isEmpty())
            continue;
        if (!folderIds.contains(folder.parentId) || folder.parentId == folder.id)
        {
            folder.parentId.clear();
            continue;
        }
        QSet<QString> ancestors;
        QString parent = folder.parentId;
        while (!parent.isEmpty())
        {
            if (parent == folder.id || ancestors.contains(parent))
            {
                folder.parentId.clear();
                break;
            }
            ancestors.insert(parent);
            QString next;
            for (const DeviceFolderRecord &candidate : result.folders)
                if (candidate.id == parent)
                {
                    next = candidate.parentId;
                    break;
                }
            parent = next;
        }
    }

    QHash<QString, const AgentProfileRecord *> profilesById;
    QHash<QString, QList<const AgentProfileRecord *>> profilesByName;
    for (const AgentProfileRecord &profile : profiles)
    {
        if (!profile.profileId.isEmpty())
            profilesById.insert(profile.profileId, &profile);
        profilesByName[profile.name].append(&profile);
    }
    QSet<QString> placed;
    for (const DeviceProfilePlacement &source : state.placements)
    {
        if (source.id.isEmpty() || usedIds.contains(source.id))
            continue;
        DeviceProfilePlacement placement = source;
        if (!placement.profileId.isEmpty())
        {
            if (!profilesById.contains(placement.profileId))
                continue;
        }
        else
        {
            const auto matches = profilesByName.value(placement.legacyProfileName);
            if (matches.size() != 1)
                continue;
            placement.profileId = matches.first()->profileId;
        }
        if (placement.profileId.isEmpty() || placed.contains(placement.profileId))
            continue;
        if (!placement.parentId.isEmpty() && !folderIds.contains(placement.parentId))
            continue;
        placement.legacyProfileName.clear();
        placement.order = qMax(0, placement.order);
        result.placements.append(placement);
        usedIds.insert(placement.id);
        placed.insert(placement.profileId);
    }
    return result;
}

QStringList DeviceTree::UnfiledProfiles(const DeviceTreeState &state,
                                        const QList<AgentProfileRecord> &profiles)
{
    QSet<QString> placed;
    for (const DeviceProfilePlacement &placement : state.placements)
        placed.insert(placement.profileId);
    QStringList result;
    for (const AgentProfileRecord &profile : profiles)
        if (!profile.profileId.isEmpty() && !placed.contains(profile.profileId))
            result.append(profile.profileId);
    return result;
}

bool DeviceTree::DeleteFolder(DeviceTreeState *state, const QString &folderId)
{
    if (!state || folderId.isEmpty())
        return false;
    QSet<QString> removed{folderId};
    bool changed;
    do
    {
        changed = false;
        for (const DeviceFolderRecord &folder : state->folders)
            if (removed.contains(folder.parentId) && !removed.contains(folder.id))
            {
                removed.insert(folder.id);
                changed = true;
            }
    } while (changed);

    const qsizetype oldFolders = state->folders.size();
    state->folders.removeIf([&removed](const DeviceFolderRecord &folder) {
        return removed.contains(folder.id);
    });
    state->placements.removeIf([&removed](const DeviceProfilePlacement &placement) {
        return removed.contains(placement.parentId);
    });
    return state->folders.size() != oldFolders;
}

bool DeviceTree::MoveProfile(DeviceTreeState *state, const QString &profileId,
                             const QString &parentId, int order)
{
    if (!state || profileId.isEmpty())
        return false;
    if (!parentId.isEmpty())
    {
        bool found = false;
        for (const DeviceFolderRecord &folder : state->folders)
            if (folder.id == parentId)
            {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    for (DeviceProfilePlacement &placement : state->placements)
        if (placement.profileId == profileId)
        {
            placement.parentId = parentId;
            if (order >= 0)
                placement.order = order;
            return true;
        }
    DeviceProfilePlacement placement;
    placement.id = NewId();
    placement.parentId = parentId;
    placement.profileId = profileId;
    placement.order = order >= 0 ? order : state->placements.size();
    state->placements.append(placement);
    return true;
}
