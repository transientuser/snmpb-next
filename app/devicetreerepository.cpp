#include "devicetreerepository.h"

#include <QSettings>

DeviceTreeRepository::DeviceTreeRepository(const QString &filename)
    : filename(filename)
{
}

DeviceTreeState DeviceTreeRepository::Load() const
{
    QSettings settings(filename, QSettings::IniFormat);
    DeviceTreeState state;
    state.rootSortMode = settings.value("sort/connections", 0).toInt();
    state.unfiledSortMode = settings.value("sort/unfiled", 0).toInt();
    const int folderCount = settings.beginReadArray("folders");
    for (int i = 0; i < folderCount; ++i)
    {
        settings.setArrayIndex(i);
        DeviceFolderRecord folder;
        folder.id = settings.value("id").toString();
        folder.parentId = settings.value("parent").toString();
        folder.name = settings.value("name").toString();
        folder.order = settings.value("order", i).toInt();
        folder.sortMode = settings.value("sortMode", 0).toInt();
        state.folders.append(folder);
    }
    settings.endArray();

    const int placementCount = settings.beginReadArray("profiles");
    for (int i = 0; i < placementCount; ++i)
    {
        settings.setArrayIndex(i);
        DeviceProfilePlacement placement;
        placement.id = settings.value("id").toString();
        placement.parentId = settings.value("parent").toString();
        placement.profileId = settings.value("profileId").toString();
        placement.legacyProfileName = settings.value("profile").toString();
        placement.order = settings.value("order", i).toInt();
        state.placements.append(placement);
    }
    settings.endArray();
    return state;
}

bool DeviceTreeRepository::Save(const DeviceTreeState &state) const
{
    QSettings settings(filename, QSettings::IniFormat);
    settings.clear();
    settings.setValue("schema/version", 2);
    if (state.rootSortMode != 0)
        settings.setValue("sort/connections", state.rootSortMode);
    if (state.unfiledSortMode != 0)
        settings.setValue("sort/unfiled", state.unfiledSortMode);
    settings.beginWriteArray("folders");
    for (int i = 0; i < state.folders.size(); ++i)
    {
        settings.setArrayIndex(i);
        const DeviceFolderRecord &folder = state.folders[i];
        settings.setValue("id", folder.id);
        settings.setValue("parent", folder.parentId);
        settings.setValue("name", folder.name);
        settings.setValue("order", folder.order);
        if (folder.sortMode != 0)
            settings.setValue("sortMode", folder.sortMode);
    }
    settings.endArray();
    settings.beginWriteArray("profiles");
    for (int i = 0; i < state.placements.size(); ++i)
    {
        settings.setArrayIndex(i);
        const DeviceProfilePlacement &placement = state.placements[i];
        settings.setValue("id", placement.id);
        settings.setValue("parent", placement.parentId);
        settings.setValue("profileId", placement.profileId);
        settings.setValue("order", placement.order);
    }
    settings.endArray();
    settings.sync();
    return settings.status() == QSettings::NoError;
}

QString DeviceTreeRepository::fileName() const
{
    return filename;
}
