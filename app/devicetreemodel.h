#ifndef DEVICETREEMODEL_H
#define DEVICETREEMODEL_H

#include "agentprofilerepository.h"
#include "devicetreerepository.h"

#include <QAbstractItemModel>
#include <memory>
#include <vector>

class DeviceTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    enum Roles
    {
        NodeTypeRole = Qt::UserRole + 1,
        ProfileIdRole,
        ProfileNameRole,
        FolderIdRole,
        SearchTextRole
    };
    enum class NodeType { Root, Folder, Profile, Unfiled };

    DeviceTreeModel(const QString &sidecarFile,
                    const QList<AgentProfileRecord> &profiles,
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
                     const QModelIndex &destinationFolder);
    bool moveProfileToFolderId(const QString &profileId, const QString &folderId);
    void setProfiles(const QList<AgentProfileRecord> &profiles);
    void renameProfile(const QString &profileId, const QString &newName);
    void placeDuplicate(const QString &sourceId, const QString &newId);
    QString profileId(const QModelIndex &index) const;
    QString profileName(const QModelIndex &index) const;
    bool isFolder(const QModelIndex &index) const;
    bool isProfile(const QModelIndex &index) const;
    bool isUnfiled(const QModelIndex &index) const;
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
    bool persist();
    const AgentProfileRecord *profile(const QString &profileId) const;

    DeviceTreeRepository repository;
    DeviceTreeState treeState;
    QList<AgentProfileRecord> profiles;
    std::unique_ptr<Node> root;
};

#endif
