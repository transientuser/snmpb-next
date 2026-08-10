#include "profiletransfer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QUuid>
#include "agentprofilerepository.h"
#include "devicetreerepository.h"
#include "profilemetadatarepository.h"
#include <QFile>
#include <QFileInfo>

namespace {
QJsonObject profileJson(const AgentProfileRecord &p)
{
    QJsonObject o;
    o["profileId"] = p.profileId; o["name"] = p.name;
    o["address"] = p.address; o["port"] = p.port;
    o["v1"] = p.v1; o["v2c"] = p.v2; o["v3"] = p.v3;
    o["retries"] = p.retries; o["timeout"] = p.timeout;
    o["maxRepetitions"] = p.maxrepetitions;
    o["nonRepeaters"] = p.nonrepeaters;
    o["securityName"] = p.secname; o["securityLevel"] = p.seclevel;
    o["contextName"] = p.contextname;
    o["contextEngineId"] = p.contextengineid;
    o["credentialsOmitted"] = true;
    return o;
}

bool stringField(const QJsonObject &o, const char *name, QString *value)
{
    const QJsonValue field = o.value(QLatin1String(name));
    if (!field.isString()) return false;
    *value = field.toString();
    return true;
}

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
}

QByteArray ProfileTransfer::exportJson(const ProfileTransferDocument &document)
{
    QJsonObject root;
    root["format"] = "snmpb-next-profile-transfer";
    root["version"] = CurrentVersion;
    root["credentialPolicy"] = "omitted";
    QJsonArray profiles;
    for (const AgentProfileRecord &profile : document.profiles)
        profiles.append(profileJson(profile));
    root["profiles"] = profiles;
    QJsonArray metadata;
    for (const ProfileMetadataRecord &record : document.metadata)
    {
        QJsonObject o; o["profileId"] = record.profileId; o["notes"] = record.notes;
        QJsonArray tags; for (const QString &tag : record.tags) tags.append(tag);
        o["tags"] = tags; metadata.append(o);
    }
    root["metadata"] = metadata;
    QJsonArray folders;
    for (const DeviceFolderRecord &folder : document.tree.folders)
    {
        QJsonObject o; o["id"] = folder.id; o["parentId"] = folder.parentId;
        o["name"] = folder.name; o["order"] = folder.order; folders.append(o);
    }
    root["folders"] = folders;
    QJsonArray placements;
    for (const DeviceProfilePlacement &placement : document.tree.placements)
    {
        QJsonObject o; o["id"] = placement.id; o["parentId"] = placement.parentId;
        o["profileId"] = placement.profileId; o["order"] = placement.order;
        placements.append(o);
    }
    root["placements"] = placements;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

ProfileTransferDocument ProfileTransfer::selectProfiles(
    const ProfileTransferDocument &document, const QStringList &ids)
{
    const QSet<QString> selected(ids.begin(), ids.end());
    ProfileTransferDocument result;
    for (const AgentProfileRecord &p : document.profiles)
        if (selected.contains(p.profileId)) result.profiles.append(p);
    for (const ProfileMetadataRecord &m : document.metadata)
        if (selected.contains(m.profileId)) result.metadata.append(m);
    for (const DeviceProfilePlacement &p : document.tree.placements)
        if (selected.contains(p.profileId))
        {
            DeviceProfilePlacement placement = p;
            placement.parentId.clear();
            result.tree.placements.append(placement);
        }
    return result;
}

ProfileTransferDocument ProfileTransfer::selectFolder(
    const ProfileTransferDocument &document, const QString &folderId)
{
    QSet<QString> folders{folderId};
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (const DeviceFolderRecord &folder : document.tree.folders)
            if (folders.contains(folder.parentId) && !folders.contains(folder.id))
            { folders.insert(folder.id); changed = true; }
    }
    QStringList profiles;
    ProfileTransferDocument result;
    for (const DeviceFolderRecord &folder : document.tree.folders)
        if (folders.contains(folder.id)) result.tree.folders.append(folder);
    for (DeviceFolderRecord &folder : result.tree.folders)
        if (folder.id == folderId) folder.parentId.clear();
    for (const DeviceProfilePlacement &placement : document.tree.placements)
        if (folders.contains(placement.parentId))
        { result.tree.placements.append(placement); profiles.append(placement.profileId); }
    ProfileTransferDocument selected = selectProfiles(document, profiles);
    result.profiles = selected.profiles; result.metadata = selected.metadata;
    return result;
}

ProfileTransferError ProfileTransfer::planImport(
    const QByteArray &json, const QList<AgentProfileRecord> &existingProfiles,
    const DeviceTreeState &existingTree, ProfileImportPlan *plan, QString *detail)
{
    if (!plan) return ProfileTransferError::InvalidRecord;
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
        return ProfileTransferError::InvalidJson;
    const QJsonObject root = parsed.object();
    if (root.value("format").toString() != "snmpb-next-profile-transfer" ||
        !root.value("version").isDouble())
        return ProfileTransferError::InvalidRecord;
    if (root.value("version").toInt() != CurrentVersion)
        return ProfileTransferError::UnsupportedVersion;
    if (!root.value("profiles").isArray() || !root.value("metadata").isArray() ||
        !root.value("folders").isArray() || !root.value("placements").isArray())
        return ProfileTransferError::InvalidRecord;

    ProfileImportPlan proposed;
    QSet<QString> incomingProfileIds, usedProfileIds;
    for (const AgentProfileRecord &p : existingProfiles) usedProfileIds.insert(p.profileId);
    const QJsonArray profiles = root.value("profiles").toArray();
    for (const QJsonValue &value : profiles)
    {
        if (!value.isObject()) return ProfileTransferError::InvalidRecord;
        const QJsonObject o = value.toObject();
        AgentProfileRecord p{};
        QString incomingId;
        if (!stringField(o, "profileId", &incomingId) || incomingId.isEmpty() ||
            !stringField(o, "name", &p.name) || !stringField(o, "address", &p.address) ||
            !stringField(o, "port", &p.port))
            return ProfileTransferError::InvalidRecord;
        if (incomingProfileIds.contains(incomingId))
            return ProfileTransferError::DuplicateId;
        incomingProfileIds.insert(incomingId);
        p.profileId = usedProfileIds.contains(incomingId) ? newId() : incomingId;
        usedProfileIds.insert(p.profileId); proposed.profileIdMap[incomingId] = p.profileId;
        p.v1 = o.value("v1").toBool(); p.v2 = o.value("v2c").toBool();
        p.v3 = o.value("v3").toBool(); p.retries = o.value("retries").toInt(1);
        p.timeout = o.value("timeout").toInt(3); p.maxrepetitions = o.value("maxRepetitions").toInt(10);
        p.nonrepeaters = o.value("nonRepeaters").toInt();
        p.secname = o.value("securityName").toString();
        p.seclevel = o.value("securityLevel").toInt();
        p.contextname = o.value("contextName").toString();
        p.contextengineid = o.value("contextEngineId").toString();
        p.readcomm.clear(); p.writecomm.clear();
        if (!p.v1 && !p.v2 && !p.v3) return ProfileTransferError::InvalidRecord;
        proposed.profiles.append(p);
    }

    QSet<QString> incomingFolderIds, usedFolderIds;
    for (const DeviceFolderRecord &f : existingTree.folders) usedFolderIds.insert(f.id);
    const QJsonArray folders = root.value("folders").toArray();
    for (const QJsonValue &value : folders)
    {
        if (!value.isObject()) return ProfileTransferError::InvalidRecord;
        const QJsonObject o = value.toObject(); DeviceFolderRecord f;
        QString incomingId;
        if (!stringField(o, "id", &incomingId) || incomingId.isEmpty() ||
            !stringField(o, "name", &f.name)) return ProfileTransferError::InvalidRecord;
        if (incomingFolderIds.contains(incomingId)) return ProfileTransferError::DuplicateId;
        incomingFolderIds.insert(incomingId);
        f.id = usedFolderIds.contains(incomingId) ? newId() : incomingId;
        usedFolderIds.insert(f.id); proposed.folderIdMap[incomingId] = f.id;
        f.parentId = o.value("parentId").toString(); f.order = o.value("order").toInt();
        proposed.tree.folders.append(f);
    }
    for (DeviceFolderRecord &f : proposed.tree.folders)
    {
        if (f.parentId.isEmpty()) continue;
        if (!proposed.folderIdMap.contains(f.parentId))
        { if (detail) *detail = f.parentId; return ProfileTransferError::MissingFolderParent; }
        f.parentId = proposed.folderIdMap.value(f.parentId);
    }

    QSet<QString> metadataIds;
    for (const QJsonValue &value : root.value("metadata").toArray())
    {
        if (!value.isObject()) return ProfileTransferError::InvalidRecord;
        const QJsonObject o = value.toObject(); const QString sourceId = o.value("profileId").toString();
        if (!proposed.profileIdMap.contains(sourceId) || metadataIds.contains(sourceId))
            return ProfileTransferError::InvalidRecord;
        metadataIds.insert(sourceId); ProfileMetadataRecord m;
        m.profileId = proposed.profileIdMap.value(sourceId); m.notes = o.value("notes").toString();
        QStringList tags; for (const QJsonValue &tag : o.value("tags").toArray())
            if (tag.isString()) tags.append(tag.toString());
        m.tags = ProfileMetadataRepository::normalizeTags(tags); proposed.metadata.append(m);
    }

    QSet<QString> placementIds;
    for (const DeviceProfilePlacement &existing : existingTree.placements)
        placementIds.insert(existing.id);
    for (const QJsonValue &value : root.value("placements").toArray())
    {
        if (!value.isObject()) return ProfileTransferError::InvalidRecord;
        const QJsonObject o = value.toObject(); DeviceProfilePlacement p;
        const QString sourceProfile = o.value("profileId").toString();
        const QString sourceParent = o.value("parentId").toString();
        if (!proposed.profileIdMap.contains(sourceProfile) ||
            (!sourceParent.isEmpty() && !proposed.folderIdMap.contains(sourceParent)))
            return ProfileTransferError::InvalidRecord;
        p.id = o.value("id").toString();
        if (p.id.isEmpty() || placementIds.contains(p.id)) p.id = newId();
        placementIds.insert(p.id); p.profileId = proposed.profileIdMap.value(sourceProfile);
        p.parentId = proposed.folderIdMap.value(sourceParent); p.order = o.value("order").toInt();
        proposed.tree.placements.append(p);
    }
    *plan = proposed;
    return ProfileTransferError::None;
}

bool ProfileImportStorage::apply(const ProfileImportPlan &plan,
                                 const QString &agentsFile,
                                 const QString &metadataFile,
                                 const QString &deviceTreeFile,
                                 const QList<AgentProfileRecord> &existingProfiles,
                                 const QList<ProfileMetadataRecord> &existingMetadata,
                                 const DeviceTreeState &existingTree,
                                 QString *error)
{
    QList<AgentProfileRecord> profiles = existingProfiles;
    profiles.append(plan.profiles);
    QList<ProfileMetadataRecord> metadata = existingMetadata;
    metadata.append(plan.metadata);
    DeviceTreeState tree = existingTree;
    tree.folders.append(plan.tree.folders);
    tree.placements.append(plan.tree.placements);

    const QString suffix = QStringLiteral(".import-%1").arg(newId());
    const QStringList targets{agentsFile, metadataFile, deviceTreeFile};
    const QStringList staged{agentsFile + suffix, metadataFile + suffix,
                             deviceTreeFile + suffix};
    AgentProfileRepository(staged[0]).Save(profiles);
    const bool stagedMetadata = ProfileMetadataRepository(staged[1]).save(metadata);
    const bool stagedTree = DeviceTreeRepository(staged[2]).Save(tree);
    if (!QFile::exists(staged[0]) || !stagedMetadata || !stagedTree)
    {
        for (const QString &file : staged) QFile::remove(file);
        if (error) *error = QStringLiteral("Could not stage imported configuration");
        return false;
    }

    QStringList backups;
    QList<bool> hadOriginal;
    int committed = 0;
    for (int i = 0; i < targets.size(); ++i)
    {
        const QString backup = targets[i] + suffix + QStringLiteral(".backup");
        backups.append(backup);
        hadOriginal.append(QFile::exists(targets[i]));
        if (hadOriginal[i] && !QFile::rename(targets[i], backup))
            break;
        if (!QFile::rename(staged[i], targets[i]))
        {
            if (hadOriginal[i]) QFile::rename(backup, targets[i]);
            break;
        }
        ++committed;
    }
    if (committed != targets.size())
    {
        for (int i = committed - 1; i >= 0; --i)
        {
            QFile::remove(targets[i]);
            if (hadOriginal[i]) QFile::rename(backups[i], targets[i]);
        }
        for (const QString &file : staged) QFile::remove(file);
        for (const QString &file : backups) QFile::remove(file);
        if (error) *error = QStringLiteral("Could not replace imported configuration");
        return false;
    }
    for (const QString &file : backups) QFile::remove(file);
    return true;
}
