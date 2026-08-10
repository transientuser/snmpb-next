#include "devicepane.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QSortFilterProxyModel>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>
#include <functional>

DevicePane::DevicePane(const QString &sidecarFile,
                       const QList<AgentProfileRecord> &profiles,
                       QWidget *parent)
    : QWidget(parent), deviceModel(new DeviceTreeModel(sidecarFile, profiles, this)),
      filterModel(new QSortFilterProxyModel(this)), tree(new QTreeView(this)),
      filter(new QLineEdit(this))
{
    connect(deviceModel, &DeviceTreeModel::organizationPersisted,
            this, &DevicePane::organizationPersisted);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    QAction *newFolder = toolbar->addAction(QIcon::fromTheme("folder-new"),
                                            tr("New Folder"));
    QAction *expand = toolbar->addAction(tr("Expand All"));
    QAction *collapse = toolbar->addAction(tr("Collapse All"));
    connect(newFolder, &QAction::triggered, this, &DevicePane::createFolder);
    connect(expand, &QAction::triggered, tree, &QTreeView::expandAll);
    connect(collapse, &QAction::triggered, tree, &QTreeView::collapseAll);
    layout->addWidget(toolbar);

    filter->setPlaceholderText(tr("Search devices"));
    filter->setClearButtonEnabled(true);
    layout->addWidget(filter);
    filterModel->setSourceModel(deviceModel);
    filterModel->setFilterRole(DeviceTreeModel::SearchTextRole);
    filterModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    filterModel->setRecursiveFilteringEnabled(true);
    filterModel->setAutoAcceptChildRows(true);
    connect(filter, &QLineEdit::textChanged, filterModel,
            &QSortFilterProxyModel::setFilterFixedString);

    tree->setModel(filterModel);
    tree->setHeaderHidden(true);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    layout->addWidget(tree);
    connect(tree, &QTreeView::clicked, this, &DevicePane::activate);
    connect(tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QModelIndex source = sourceIndex(index);
        if (deviceModel->isProfile(source))
            emit editProfileRequested(deviceModel->profileId(source));
    });
    connect(tree, &QTreeView::customContextMenuRequested,
            this, &DevicePane::showContextMenu);
    tree->expandAll();
}

DeviceTreeModel *DevicePane::model() const { return deviceModel; }
QTreeView *DevicePane::treeView() const { return tree; }
QLineEdit *DevicePane::filterEdit() const { return filter; }

void DevicePane::setProfiles(const QList<AgentProfileRecord> &profiles)
{
    const QString selectedId = deviceModel->profileId(sourceIndex(selectedIndex()));
    deviceModel->setProfiles(profiles);
    tree->expandAll();
    selectProfile(selectedId);
}

void DevicePane::renameProfile(const QString &profileId, const QString &newName)
{
    deviceModel->renameProfile(profileId, newName);
}

void DevicePane::placeDuplicate(const QString &sourceId, const QString &newId)
{
    deviceModel->placeDuplicate(sourceId, newId);
    tree->expandAll();
    selectProfile(newId);
}

void DevicePane::placeCreatedProfile(const QString &profileId)
{
    if (!pendingFolderId.isNull())
        deviceModel->moveProfileToFolderId(profileId, pendingFolderId);
    pendingFolderId = QString();
    tree->expandAll();
    selectProfile(profileId);
}

void DevicePane::activate(const QModelIndex &index)
{
    const QModelIndex source = sourceIndex(index);
    if (deviceModel->isProfile(source))
        emit profileSelected(deviceModel->profileId(source));
}

void DevicePane::showContextMenu(const QPoint &position)
{
    const QModelIndex index = tree->indexAt(position);
    if (index.isValid())
        tree->setCurrentIndex(index);
    QMenu menu(this);
    const QModelIndex source = sourceIndex(index);
    if (!source.isValid() || deviceModel->isFolder(source))
    {
        menu.addAction(tr("New Profile"), this, &DevicePane::newProfile);
        menu.addAction(tr("New Folder"), this, &DevicePane::createFolder);
    }
    if (deviceModel->isFolder(source))
    {
        menu.addAction(tr("Rename Folder"), this, &DevicePane::renameFolder);
        menu.addAction(tr("Delete Folder"), this, &DevicePane::deleteFolder);
    }
    if (deviceModel->isProfile(source))
    {
        menu.addAction(tr("Edit Profile"), this, &DevicePane::editProfile);
        menu.addAction(tr("Duplicate Profile"), this, &DevicePane::duplicateProfile);
        menu.addAction(tr("Delete Profile"), this, &DevicePane::deleteProfile);
    }
    if (!menu.isEmpty())
        menu.exec(tree->viewport()->mapToGlobal(position));
}

void DevicePane::createFolder()
{
    QModelIndex parent = sourceIndex(selectedIndex());
    if (!deviceModel->isFolder(parent))
        parent = {};
    bool accepted = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"),
                                               tr("Folder name:"), QLineEdit::Normal,
                                               tr("New Folder"), &accepted);
    if (!accepted)
        return;
    const QModelIndex created = deviceModel->createFolder(name, parent);
    if (created.isValid())
    {
        const QModelIndex proxy = filterModel->mapFromSource(created);
        tree->expand(proxy.parent());
        tree->setCurrentIndex(proxy);
        tree->edit(proxy);
    }
}

void DevicePane::renameFolder()
{
    const QModelIndex index = sourceIndex(selectedIndex());
    if (deviceModel->isFolder(index))
        tree->edit(filterModel->mapFromSource(index));
}

void DevicePane::deleteFolder()
{
    const QModelIndex index = sourceIndex(selectedIndex());
    if (!deviceModel->isFolder(index))
        return;
    if (QMessageBox::question(this, tr("Delete Folder"),
                              tr("Delete this folder? Its profiles will remain available under Unfiled."))
        == QMessageBox::Yes)
        deviceModel->deleteFolder(index);
}

void DevicePane::editProfile()
{
    const QString id = deviceModel->profileId(sourceIndex(selectedIndex()));
    if (!id.isEmpty())
        emit editProfileRequested(id);
}

void DevicePane::duplicateProfile()
{
    const QString id = deviceModel->profileId(sourceIndex(selectedIndex()));
    if (!id.isEmpty())
        emit duplicateProfileRequested(id);
}

QModelIndex DevicePane::selectedIndex() const
{
    return tree->currentIndex();
}

QModelIndex DevicePane::sourceIndex(const QModelIndex &index) const
{
    return index.isValid() ? filterModel->mapToSource(index) : QModelIndex();
}

void DevicePane::selectProfile(const QString &id)
{
    if (id.isEmpty())
        return;
    std::function<QModelIndex(const QModelIndex &)> find =
        [&](const QModelIndex &parent) -> QModelIndex {
            for (int row = 0; row < deviceModel->rowCount(parent); ++row)
            {
                const QModelIndex candidate = deviceModel->index(row, 0, parent);
                if (candidate.data(DeviceTreeModel::ProfileIdRole).toString() == id)
                    return candidate;
                const QModelIndex nested = find(candidate);
                if (nested.isValid()) return nested;
            }
            return {};
        };
    const QModelIndex source = find({});
    if (source.isValid())
        tree->setCurrentIndex(filterModel->mapFromSource(source));
}

void DevicePane::newProfile()
{
    const QModelIndex source = sourceIndex(selectedIndex());
    pendingFolderId = deviceModel->isFolder(source) ?
        source.data(DeviceTreeModel::FolderIdRole).toString() : QStringLiteral("");
    emit newProfileRequested(pendingFolderId);
}

void DevicePane::deleteProfile()
{
    const QModelIndex source = sourceIndex(selectedIndex());
    const QString id = deviceModel->profileId(source);
    if (id.isEmpty())
        return;
    if (QMessageBox::question(this, tr("Delete Profile"),
                              tr("Delete this Agent Profile?")) == QMessageBox::Yes)
        emit deleteProfileRequested(id);
}
