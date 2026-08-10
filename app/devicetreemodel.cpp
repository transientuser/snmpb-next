#include "devicetreemodel.h"

#include <QIcon>
#include <QMimeData>
#include <QUuid>
#include <algorithm>
#include <functional>

namespace
{
constexpr auto ProfileMimeType = "application/x-snmpb-profile-name";
}

DeviceTreeModel::DeviceTreeModel(const QString &sidecarFile,
                                 const QList<AgentProfileRecord> &profiles,
                                 QObject *parent)
    : QAbstractItemModel(parent), repository(sidecarFile),
      treeState(DeviceTree::Reconcile(repository.Load(), profiles)), profiles(profiles)
{
    rebuild();
}

QModelIndex DeviceTreeModel::index(int row, int column,
                                   const QModelIndex &parentIndex) const
{
    if (column != 0 || row < 0)
        return {};
    Node *parentNode = nodeForIndex(parentIndex);
    if (!parentNode || row >= static_cast<int>(parentNode->children.size()))
        return {};
    return createIndex(row, column, parentNode->children[row].get());
}

QModelIndex DeviceTreeModel::parent(const QModelIndex &child) const
{
    Node *node = nodeForIndex(child);
    if (!node || !node->parent || node->parent == root.get())
        return {};
    return indexForNode(node->parent);
}

int DeviceTreeModel::rowCount(const QModelIndex &parentIndex) const
{
    if (parentIndex.column() > 0)
        return 0;
    Node *node = nodeForIndex(parentIndex);
    return node ? static_cast<int>(node->children.size()) : 0;
}

int DeviceTreeModel::columnCount(const QModelIndex &) const
{
    return 1;
}

QVariant DeviceTreeModel::data(const QModelIndex &modelIndex, int role) const
{
    Node *node = nodeForIndex(modelIndex);
    if (!node || node == root.get())
        return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return node->text;
    if (role == NodeTypeRole)
        return static_cast<int>(node->type);
    if (role == ProfileIdRole)
        return node->profileId;
    if (role == ProfileNameRole)
        return node->profileName;
    if (role == FolderIdRole)
        return node->id;
    if (role == Qt::DecorationRole)
        return QIcon::fromTheme(node->type == NodeType::Profile ?
                                "network-server" : "folder");
    if (role == Qt::ToolTipRole && node->type == NodeType::Profile)
    {
        const AgentProfileRecord *record = profile(node->profileId);
        if (!record)
            return {};
        QStringList protocols;
        if (record->v1) protocols.append("SNMPv1");
        if (record->v2) protocols.append("SNMPv2c");
        if (record->v3) protocols.append("SNMPv3");
        return QString("%1\n%2").arg(record->address, protocols.join(", "));
    }
    return {};
}

Qt::ItemFlags DeviceTreeModel::flags(const QModelIndex &modelIndex) const
{
    if (!modelIndex.isValid())
        return Qt::ItemIsDropEnabled;
    Node *node = nodeForIndex(modelIndex);
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (node->type == NodeType::Folder)
        result |= Qt::ItemIsEditable | Qt::ItemIsDropEnabled;
    else if (node->type == NodeType::Unfiled)
        result |= Qt::ItemIsDropEnabled;
    else if (node->type == NodeType::Profile)
        result |= Qt::ItemIsDragEnabled;
    return result;
}

bool DeviceTreeModel::setData(const QModelIndex &modelIndex,
                              const QVariant &value, int role)
{
    Node *node = nodeForIndex(modelIndex);
    const QString name = value.toString().trimmed();
    if (role != Qt::EditRole || !node || node->type != NodeType::Folder ||
        name.isEmpty())
        return false;
    for (DeviceFolderRecord &folder : treeState.folders)
        if (folder.id == node->id)
        {
            folder.name = name;
            node->text = name;
            persist();
            emit dataChanged(modelIndex, modelIndex, {Qt::DisplayRole});
            return true;
        }
    return false;
}

QStringList DeviceTreeModel::mimeTypes() const
{
    return {ProfileMimeType};
}

QMimeData *DeviceTreeModel::mimeData(const QModelIndexList &indexes) const
{
    auto *mime = new QMimeData;
    for (const QModelIndex &modelIndex : indexes)
        if (isProfile(modelIndex))
        {
            mime->setData(ProfileMimeType, profileId(modelIndex).toUtf8());
            break;
        }
    return mime;
}

bool DeviceTreeModel::canDropMimeData(const QMimeData *mime, Qt::DropAction action,
                                      int, int, const QModelIndex &parentIndex) const
{
    if (!mime || !mime->hasFormat(ProfileMimeType) ||
        (action != Qt::MoveAction && action != Qt::IgnoreAction))
        return false;
    return !parentIndex.isValid() || isFolder(parentIndex) || isUnfiled(parentIndex);
}

bool DeviceTreeModel::dropMimeData(const QMimeData *mime, Qt::DropAction action,
                                   int, int, const QModelIndex &parentIndex)
{
    if (action == Qt::IgnoreAction)
        return true;
    if (!canDropMimeData(mime, action, -1, -1, parentIndex))
        return false;
    return moveProfile(QString::fromUtf8(mime->data(ProfileMimeType)), parentIndex);
}

Qt::DropActions DeviceTreeModel::supportedDropActions() const
{
    return Qt::MoveAction;
}

QModelIndex DeviceTreeModel::createFolder(const QString &rawName,
                                          const QModelIndex &parentIndex)
{
    const QString name = rawName.trimmed();
    Node *parentNode = nodeForIndex(parentIndex);
    QString parentId;
    if (parentIndex.isValid())
    {
        if (!parentNode || parentNode->type != NodeType::Folder)
            return {};
        parentId = parentNode->id;
    }
    if (name.isEmpty())
        return {};
    DeviceFolderRecord folder;
    folder.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    folder.parentId = parentId;
    folder.name = name;
    folder.order = treeState.folders.size();
    treeState.folders.append(folder);
    persist();
    beginResetModel();
    rebuild();
    endResetModel();
    std::function<QModelIndex(const QModelIndex &)> findFolder =
        [&](const QModelIndex &parent) -> QModelIndex {
            for (int row = 0; row < rowCount(parent); ++row)
            {
                QModelIndex candidate = index(row, 0, parent);
                if (candidate.data(FolderIdRole).toString() == folder.id)
                    return candidate;
                QModelIndex nested = findFolder(candidate);
                if (nested.isValid())
                    return nested;
            }
            return {};
        };
    return findFolder({});
}

bool DeviceTreeModel::deleteFolder(const QModelIndex &modelIndex)
{
    Node *node = nodeForIndex(modelIndex);
    if (!node || node->type != NodeType::Folder ||
        !DeviceTree::DeleteFolder(&treeState, node->id))
        return false;
    persist();
    beginResetModel();
    rebuild();
    endResetModel();
    return true;
}

bool DeviceTreeModel::moveProfile(const QString &id,
                                  const QModelIndex &destinationFolder)
{
    QString parentId;
    if (destinationFolder.isValid())
    {
        Node *destination = nodeForIndex(destinationFolder);
        if (!destination || (destination->type != NodeType::Folder &&
                             destination->type != NodeType::Unfiled))
            return false;
        if (destination->type == NodeType::Folder)
            parentId = destination->id;
    }
    if (!profile(id))
        return false;
    if (parentId.isEmpty())
        treeState.placements.removeIf([&id](const DeviceProfilePlacement &placement) {
            return placement.profileId == id;
        });
    else if (!DeviceTree::MoveProfile(&treeState, id, parentId))
        return false;
    persist();
    beginResetModel();
    rebuild();
    endResetModel();
    return true;
}

void DeviceTreeModel::setProfiles(const QList<AgentProfileRecord> &newProfiles)
{
    profiles = newProfiles;
    treeState = DeviceTree::Reconcile(treeState, profiles);
    persist();
    beginResetModel();
    rebuild();
    endResetModel();
}

void DeviceTreeModel::renameProfile(const QString &id, const QString &newName)
{
    for (AgentProfileRecord &record : profiles)
        if (record.profileId == id)
        {
            record.name = newName;
            break;
        }
    persist();
    beginResetModel();
    rebuild();
    endResetModel();
}

void DeviceTreeModel::placeDuplicate(const QString &sourceId,
                                     const QString &newId)
{
    QString parentId;
    for (const DeviceProfilePlacement &placement : treeState.placements)
        if (placement.profileId == sourceId)
        {
            parentId = placement.parentId;
            break;
        }
    if (!parentId.isEmpty())
        DeviceTree::MoveProfile(&treeState, newId, parentId);
    persist();
    beginResetModel();
    rebuild();
    endResetModel();
}

QString DeviceTreeModel::profileId(const QModelIndex &modelIndex) const
{
    Node *node = nodeForIndex(modelIndex);
    return node && node->type == NodeType::Profile ? node->profileId : QString();
}

QString DeviceTreeModel::profileName(const QModelIndex &modelIndex) const
{
    Node *node = nodeForIndex(modelIndex);
    return node && node->type == NodeType::Profile ? node->profileName : QString();
}

bool DeviceTreeModel::isFolder(const QModelIndex &modelIndex) const
{
    Node *node = nodeForIndex(modelIndex);
    return node && node->type == NodeType::Folder;
}

bool DeviceTreeModel::isProfile(const QModelIndex &modelIndex) const
{
    Node *node = nodeForIndex(modelIndex);
    return node && node->type == NodeType::Profile;
}

bool DeviceTreeModel::isUnfiled(const QModelIndex &modelIndex) const
{
    Node *node = nodeForIndex(modelIndex);
    return node && node->type == NodeType::Unfiled;
}

const DeviceTreeState &DeviceTreeModel::state() const
{
    return treeState;
}

DeviceTreeModel::Node *DeviceTreeModel::nodeForIndex(const QModelIndex &modelIndex) const
{
    return modelIndex.isValid() ? static_cast<Node *>(modelIndex.internalPointer()) : root.get();
}

QModelIndex DeviceTreeModel::indexForNode(Node *node) const
{
    if (!node || !node->parent || node == root.get())
        return {};
    const auto &siblings = node->parent->children;
    for (int row = 0; row < static_cast<int>(siblings.size()); ++row)
        if (siblings[row].get() == node)
            return createIndex(row, 0, node);
    return {};
}

void DeviceTreeModel::rebuild()
{
    root = std::make_unique<Node>();
    QHash<QString, Node *> folderNodes;
    QList<DeviceFolderRecord> pending = treeState.folders;
    std::stable_sort(pending.begin(), pending.end(), [](const auto &a, const auto &b) {
        return a.order < b.order;
    });
    while (!pending.isEmpty())
    {
        bool progress = false;
        for (int i = 0; i < pending.size();)
        {
            const DeviceFolderRecord folder = pending[i];
            Node *parentNode = folder.parentId.isEmpty() ? root.get() :
                               folderNodes.value(folder.parentId, nullptr);
            if (!parentNode)
            {
                ++i;
                continue;
            }
            auto node = std::make_unique<Node>();
            node->type = NodeType::Folder;
            node->id = folder.id;
            node->text = folder.name;
            node->parent = parentNode;
            folderNodes.insert(folder.id, node.get());
            parentNode->children.push_back(std::move(node));
            pending.removeAt(i);
            progress = true;
            // The next item shifted into this index.
        }
        if (!progress)
            break;
    }

    QList<DeviceProfilePlacement> placements = treeState.placements;
    std::stable_sort(placements.begin(), placements.end(), [](const auto &a, const auto &b) {
        return a.order < b.order;
    });
    for (const DeviceProfilePlacement &placement : placements)
    {
        Node *parentNode = placement.parentId.isEmpty() ? root.get() :
                           folderNodes.value(placement.parentId, nullptr);
        if (!parentNode)
            continue;
        auto node = std::make_unique<Node>();
        node->type = NodeType::Profile;
        const AgentProfileRecord *record = profile(placement.profileId);
        if (!record)
            continue;
        node->text = record->name;
        node->profileId = record->profileId;
        node->profileName = record->name;
        node->id = placement.id;
        node->parent = parentNode;
        parentNode->children.push_back(std::move(node));
    }

    const QStringList unfiled = DeviceTree::UnfiledProfiles(treeState, profiles);
    if (!unfiled.isEmpty())
    {
        auto unfiledNode = std::make_unique<Node>();
        unfiledNode->type = NodeType::Unfiled;
        unfiledNode->text = tr("Unfiled");
        unfiledNode->parent = root.get();
        for (const QString &id : unfiled)
        {
            const AgentProfileRecord *record = profile(id);
            if (!record)
                continue;
            auto node = std::make_unique<Node>();
            node->type = NodeType::Profile;
            node->text = record->name;
            node->profileId = record->profileId;
            node->profileName = record->name;
            node->parent = unfiledNode.get();
            unfiledNode->children.push_back(std::move(node));
        }
        root->children.push_back(std::move(unfiledNode));
    }
}

bool DeviceTreeModel::persist()
{
    const bool saved = repository.Save(treeState);
    if (saved)
        emit organizationPersisted();
    return saved;
}

const AgentProfileRecord *DeviceTreeModel::profile(const QString &id) const
{
    for (const AgentProfileRecord &record : profiles)
        if (record.profileId == id)
            return &record;
    return nullptr;
}
