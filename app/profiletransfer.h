#ifndef PROFILETRANSFER_H
#define PROFILETRANSFER_H

#include "agentprofilerepository.h"
#include "devicetree.h"
#include "profilemetadatarepository.h"

#include <QByteArray>
#include <QHash>

struct ProfileTransferDocument
{
    QList<AgentProfileRecord> profiles;
    QList<ProfileMetadataRecord> metadata;
    DeviceTreeState tree;
};

struct ProfileImportPlan
{
    QList<AgentProfileRecord> profiles;
    QList<ProfileMetadataRecord> metadata;
    DeviceTreeState tree;
    QHash<QString, QString> profileIdMap;
    QHash<QString, QString> folderIdMap;
};

enum class ProfileTransferError
{
    None,
    InvalidJson,
    UnsupportedVersion,
    InvalidRecord,
    DuplicateId,
    MissingFolderParent
};

class ProfileTransfer
{
public:
    static constexpr int CurrentVersion = 1;

    static QByteArray exportJson(const ProfileTransferDocument &document);
    static ProfileTransferDocument selectProfiles(
        const ProfileTransferDocument &document, const QStringList &profileIds);
    static ProfileTransferDocument selectFolder(
        const ProfileTransferDocument &document, const QString &folderId);
    static ProfileTransferError planImport(
        const QByteArray &json, const QList<AgentProfileRecord> &existingProfiles,
        const DeviceTreeState &existingTree, ProfileImportPlan *plan,
        QString *detail = nullptr);
};

class ProfileImportStorage
{
public:
    static bool apply(const ProfileImportPlan &plan, const QString &agentsFile,
                      const QString &metadataFile, const QString &deviceTreeFile,
                      const QList<AgentProfileRecord> &existingProfiles,
                      const QList<ProfileMetadataRecord> &existingMetadata,
                      const DeviceTreeState &existingTree,
                      QString *error = nullptr);
};

#endif
