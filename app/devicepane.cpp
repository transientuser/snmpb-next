#include "devicepane.h"

#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

DevicePane::DevicePane(const QString &sidecarFile,
                       const QList<AgentProfileRecord> &profiles,
                       QWidget *parent)
    : QWidget(parent), deviceModel(new DeviceTreeModel(sidecarFile, profiles, this)),
      tree(new QTreeView(this))
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

    tree->setModel(deviceModel);
    tree->setHeaderHidden(true);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setDragDropMode(QAbstractItemView::InternalMove);
    layout->addWidget(tree);
    connect(tree, &QTreeView::clicked, this, &DevicePane::activate);
    connect(tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (deviceModel->isProfile(index))
            emit editProfileRequested(deviceModel->profileId(index));
    });
    connect(tree, &QTreeView::customContextMenuRequested,
            this, &DevicePane::showContextMenu);
    tree->expandAll();
}

DeviceTreeModel *DevicePane::model() const { return deviceModel; }
QTreeView *DevicePane::treeView() const { return tree; }

void DevicePane::setProfiles(const QList<AgentProfileRecord> &profiles)
{
    deviceModel->setProfiles(profiles);
    tree->expandAll();
}

void DevicePane::renameProfile(const QString &profileId, const QString &newName)
{
    deviceModel->renameProfile(profileId, newName);
}

void DevicePane::placeDuplicate(const QString &sourceId, const QString &newId)
{
    deviceModel->placeDuplicate(sourceId, newId);
    tree->expandAll();
}

void DevicePane::activate(const QModelIndex &index)
{
    if (deviceModel->isProfile(index))
        emit profileSelected(deviceModel->profileId(index));
}

void DevicePane::showContextMenu(const QPoint &position)
{
    const QModelIndex index = tree->indexAt(position);
    if (index.isValid())
        tree->setCurrentIndex(index);
    QMenu menu(this);
    if (!index.isValid() || deviceModel->isFolder(index))
        menu.addAction(tr("New Folder"), this, &DevicePane::createFolder);
    if (deviceModel->isFolder(index))
    {
        menu.addAction(tr("Rename Folder"), this, &DevicePane::renameFolder);
        menu.addAction(tr("Delete Folder"), this, &DevicePane::deleteFolder);
    }
    if (deviceModel->isProfile(index))
    {
        menu.addAction(tr("Edit Profile"), this, &DevicePane::editProfile);
        menu.addAction(tr("Duplicate Profile"), this, &DevicePane::duplicateProfile);
    }
    if (!menu.isEmpty())
        menu.exec(tree->viewport()->mapToGlobal(position));
}

void DevicePane::createFolder()
{
    QModelIndex parent = selectedIndex();
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
        tree->expand(created.parent());
        tree->setCurrentIndex(created);
        tree->edit(created);
    }
}

void DevicePane::renameFolder()
{
    const QModelIndex index = selectedIndex();
    if (deviceModel->isFolder(index))
        tree->edit(index);
}

void DevicePane::deleteFolder()
{
    const QModelIndex index = selectedIndex();
    if (!deviceModel->isFolder(index))
        return;
    if (QMessageBox::question(this, tr("Delete Folder"),
                              tr("Delete this folder? Its profiles will remain available under Unfiled."))
        == QMessageBox::Yes)
        deviceModel->deleteFolder(index);
}

void DevicePane::editProfile()
{
    const QString id = deviceModel->profileId(selectedIndex());
    if (!id.isEmpty())
        emit editProfileRequested(id);
}

void DevicePane::duplicateProfile()
{
    const QString id = deviceModel->profileId(selectedIndex());
    if (!id.isEmpty())
        emit duplicateProfileRequested(id);
}

QModelIndex DevicePane::selectedIndex() const
{
    return tree->currentIndex();
}
