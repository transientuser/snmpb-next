#ifndef DEVICETREEMODEL_H
#define DEVICETREEMODEL_H

#include "agentprofilerepository.h"
#include "devicetreerepository.h"
#include "profilemetadatarepository.h"

#include <QAbstractItemModel>
#include <memory>
#include <vector>

class DeviceTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum class SortMode { Manual = 0, NameAscending = 1, NameDescending = 2 };
    enum Roles
    {
        NodeTypeRole = Qt::UserRole + 1,
        ProfileIdRole,
        ProfileNameRole,
        FolderIdRole,
        SearchTextRole,
        CredentialHealthRole
    };
    enum class NodeType { Root, Connections, Folder, Profile, Unfiled };

    DeviceTreeModel(const QString &sidecarFile,
                    const QList<AgentProfileRecord> &profiles,
                    const QList<ProfileMetadataRecord> &metadata = {},
                    QObject *parent = nullptr);

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value,
                 int role = Qt::EditRole) override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    bool canDropMimeData(const QMimeData *data, Qt::DropAction action,
                         int row, int column,
                         const QModelIndex &parent) const override;
    bool dropMimeData(const QMimeData *data, Qt::DropAction action,
                      int row, int column, const QModelIndex &parent) override;
    Qt::DropActions supportedDropActions() const override;

    QModelIndex createFolder(const QString &name,
                             const QModelIndex &parent = QModelIndex());
    bool deleteFolder(const QModelIndex &index);
    bool moveProfile(const QString &profileId,
                     const QModelIndex &destinationFolder, int row = -1);
    bool moveProfileToFolderId(const QString &profileId, const QString &folderId);
    bool moveFolder(const QString &folderId, const QModelIndex &destinationFolder,
                    int row = -1);
    bool importState(const DeviceTreeState &state);
    void reload();
    void setProfiles(const QList<AgentProfileRecord> &profiles);
    void setMetadata(const QList<ProfileMetadataRecord> &metadata);
    void setCredentialHealth(const QHash<QString, QString> &health);
    void renameProfile(const QString &profileId, const QString &newName);
    void placeDuplicate(const QString &sourceId, const QString &newId);
    QString profileId(const QModelIndex &index) const;
    QString profileName(const QModelIndex &index) const;
    bool isFolder(const QModelIndex &index) const;
    bool isConnections(const QModelIndex &index) const;
    bool isProfile(const QModelIndex &index) const;
    bool isUnfiled(const QModelIndex &index) const;
    SortMode sortMode(const QModelIndex &index) const;
    bool setSortMode(const QModelIndex &index, SortMode mode);
    const DeviceTreeState &state() const;

signals:
    void organizationPersisted();

private:
    struct Node
    {
        NodeType type = NodeType::Root;
        QString id;
        QString text;
        QString profileId;
        QString profileName;
        Node *parent = nullptr;
        std::vector<std::unique_ptr<Node>> children;
    };

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(Node *node) const;
    void rebuild();
    void sortChildren(Node *parent, SortMode mode, bool keepUnfiledFirst = false);
    bool persist();
    const AgentProfileRecord *profile(const QString &profileId) const;
    const ProfileMetadataRecord *metadata(const QString &profileId) const;

    DeviceTreeRepository repository;
    DeviceTreeState treeState;
    QList<AgentProfileRecord> profiles;
    QList<ProfileMetadataRecord> profileMetadata;
    QHash<QString, QString> credentialHealth;
    std::unique_ptr<Node> root;
};

#endif
