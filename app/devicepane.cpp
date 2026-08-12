#include "devicepane.h"

#include "agentprofileservice.h"
#include "communitycredentialservice.h"
#include "profilemetadataservice.h"
#include "usmcredentialservice.h"

#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>
#include <QDebug>
#include <functional>

DevicePane::DevicePane(const QString &sidecarFile,
                       const QList<AgentProfileRecord> &profiles,
                       const QList<ProfileMetadataRecord> &metadata,
                       AgentProfileService *profileService,
                       ProfileMetadataService *metadataService,
                       CommunityCredentialService *communityService,
                       UsmCredentialService *usmService,
                       QWidget *parent)
    : QWidget(parent), deviceModel(new DeviceTreeModel(sidecarFile, profiles, metadata, this)),
      filterModel(new QSortFilterProxyModel(this)), tree(new QTreeView(this)),
      filter(new QLineEdit(this)),
      details(new DeviceDetailsEditor(profileService, metadataService,
                                      communityService, usmService, this)),
      splitter(new QSplitter(Qt::Vertical, this))
{
    setObjectName(QStringLiteral("IntegratedDeviceSidebar"));
    setMinimumWidth(280);
    connect(deviceModel, &DeviceTreeModel::organizationPersisted,
            this, &DevicePane::organizationPersisted);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *treePane = new QWidget(splitter);
    auto *treeLayout = new QVBoxLayout(treePane);
    treeLayout->setContentsMargins(8, 8, 8, 4);
    treeLayout->setSpacing(6);
    auto *title = new QLabel(tr("CONNECTIONS"), treePane);
    title->setObjectName(QStringLiteral("ConnectionsHeading"));
    QFont titleFont = title->font(); titleFont.setBold(true); title->setFont(titleFont);
    treeLayout->addWidget(title);
    filter->setPlaceholderText(tr("Search connections..."));
    filter->setAccessibleName(tr("Search connections"));
    filter->setClearButtonEnabled(true);
    treeLayout->addWidget(filter);
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
    tree->setAccessibleName(tr("Connections and devices"));
    treeLayout->addWidget(tree, 1);
    connect(tree, &QTreeView::clicked, this, &DevicePane::activate);
    connect(tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        const QModelIndex source = sourceIndex(index);
        if (deviceModel->isProfile(source)) {
            tree->setCurrentIndex(index);
            editProfile();
        }
    });
    connect(tree, &QTreeView::customContextMenuRequested,
            this, &DevicePane::showContextMenu);
    connect(tree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &DevicePane::currentChanged);
    connect(details, &DeviceDetailsEditor::folderRenameRequested,
            this, &DevicePane::renameFolderFromDetails);
    connect(details, &DeviceDetailsEditor::editPreferredMibsRequested,
            this, &DevicePane::editProfileRequested);
    connect(details, &DeviceDetailsEditor::editConnectionSettingsRequested,
            this, &DevicePane::editProfileRequested);
    connect(details, &DeviceDetailsEditor::manageUsmCredentialsRequested,
            this, &DevicePane::manageUsmCredentialsRequested);
    splitter->addWidget(treePane);
    splitter->addWidget(details);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({420, 300});
    layout->addWidget(splitter);
    tree->expandAll();
}

DeviceTreeModel *DevicePane::model() const { return deviceModel; }
QTreeView *DevicePane::treeView() const { return tree; }
QLineEdit *DevicePane::filterEdit() const { return filter; }
DeviceDetailsEditor *DevicePane::detailsEditor() const { return details; }
QSplitter *DevicePane::verticalSplitter() const { return splitter; }

void DevicePane::setProfiles(const QList<AgentProfileRecord> &profiles)
{
    const QString selectedId = deviceModel->profileId(sourceIndex(selectedIndex()));
    deviceModel->setProfiles(profiles);
    tree->setRootIndex({});
    tree->expandAll();
    selectProfile(selectedId);
}

void DevicePane::setMetadata(const QList<ProfileMetadataRecord> &metadata)
{
    const QString selectedId = deviceModel->profileId(sourceIndex(selectedIndex()));
    deviceModel->setMetadata(metadata);
    tree->setRootIndex({});
    tree->expandAll();
    selectProfile(selectedId);
}

void DevicePane::setCredentialHealth(const QHash<QString, QString> &health)
{
    deviceModel->setCredentialHealth(health);
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

bool DevicePane::importTreeState(const DeviceTreeState &state)
{
    const bool imported = deviceModel->importState(state);
    if (imported) {
        tree->setRootIndex({});
        tree->expandAll();
    }
    return imported;
}

void DevicePane::reloadTree()
{
    deviceModel->reload();
    tree->setRootIndex({});
    tree->expandAll();
}

void DevicePane::setDirtyNavigationDecisionProvider(
    std::function<DirtyNavigationDecision()> provider)
{
    dirtyNavigationDecisionProvider = std::move(provider);
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
    const auto addSortMenu = [this, &menu, source]() {
        QMenu *sort = menu.addMenu(tr("Sort"));
        const auto addMode = [this, sort, source](const QString &label,
                                                  DeviceTreeModel::SortMode mode) {
            QAction *action = sort->addAction(label);
            action->setCheckable(true);
            action->setChecked(deviceModel->sortMode(source) == mode);
            connect(action, &QAction::triggered, this, [this, source, mode]() {
                deviceModel->setSortMode(source, mode);
                tree->expandAll();
            });
        };
        addMode(tr("Manual Order"), DeviceTreeModel::SortMode::Manual);
        addMode(tr("Name A -> Z"), DeviceTreeModel::SortMode::NameAscending);
        addMode(tr("Name Z -> A"), DeviceTreeModel::SortMode::NameDescending);
    };
    if (!source.isValid() || deviceModel->isConnections(source))
    {
        menu.addAction(tr("New Device"), this, &DevicePane::newProfile);
        menu.addAction(tr("New Folder"), this, &DevicePane::createFolder);
        menu.addSeparator();
        menu.addAction(tr("Import..."), this, &DevicePane::importProfiles);
        menu.addAction(tr("Export..."), this, &DevicePane::exportAll);
        menu.addSeparator();
        addSortMenu();
    }
    else if (deviceModel->isUnfiled(source))
    {
        menu.addAction(tr("New Device"), this, &DevicePane::newProfile);
        menu.addSeparator();
        addSortMenu();
    }
    else if (deviceModel->isFolder(source))
    {
        menu.addAction(tr("New Device"), this, &DevicePane::newProfile);
        menu.addAction(tr("New Folder"), this, &DevicePane::createFolder);
        menu.addSeparator();
        menu.addAction(tr("Rename"), this, &DevicePane::renameFolder);
        menu.addAction(tr("Delete"), this, &DevicePane::deleteFolder);
        menu.addSeparator();
        menu.addAction(tr("Export..."), this, &DevicePane::exportFolder);
        menu.addSeparator();
        addSortMenu();
    }
    else if (deviceModel->isProfile(source))
    {
        menu.addAction(tr("Activate / Select"), this, [this]() {
            activate(selectedIndex());
        });
        menu.addAction(tr("Properties"), this, &DevicePane::editProfile);
        menu.addAction(tr("Duplicate"), this, &DevicePane::duplicateProfile);
        QMenu *moveMenu = menu.addMenu(tr("Move to Folder"));
        std::function<void(const QModelIndex &, QMenu *)> addFolders =
            [&](const QModelIndex &parent, QMenu *parentMenu) {
                for (int row = 0; row < deviceModel->rowCount(parent); ++row) {
                    const QModelIndex candidate = deviceModel->index(row, 0, parent);
                    if (deviceModel->isConnections(candidate)) {
                        addFolders(candidate, parentMenu);
                        continue;
                    }
                    if (!deviceModel->isFolder(candidate)) continue;
                    QMenu *child = parentMenu->addMenu(candidate.data().toString());
                    child->addAction(tr("Move here"), this, [this, candidate]() {
                        deviceModel->moveProfile(deviceModel->profileId(
                            sourceIndex(selectedIndex())), candidate);
                    });
                    addFolders(candidate, child);
                }
            };
        moveMenu->addAction(tr("Unfiled"), this, [this]() {
            deviceModel->moveProfile(deviceModel->profileId(sourceIndex(selectedIndex())), {});
        });
        addFolders({}, moveMenu);
        menu.addAction(tr("Export..."), this, &DevicePane::exportSelectedProfile);
        menu.addAction(tr("Delete"), this, &DevicePane::deleteProfile);
    }
    if (!menu.isEmpty())
        menu.exec(tree->viewport()->mapToGlobal(position));
}

void DevicePane::currentChanged(const QModelIndex &current,
                                const QModelIndex &previous)
{
    if (restoringSelection || navigationTransition) return;
    const QModelIndex requestedSource = sourceIndex(current);
    const QString requestedProfileId = deviceModel->profileId(requestedSource);
    const QString requestedFolderId =
        requestedSource.data(DeviceTreeModel::FolderIdRole).toString();
    const bool requestedUnfiled = deviceModel->isUnfiled(requestedSource);
    const bool requestedConnections = deviceModel->isConnections(requestedSource);
    const QString currentProfileId = details->currentProfileId();
    qInfo().noquote() << "[Connections] selection requested current="
                      << currentProfileId << "target=" << requestedProfileId
                      << "dirty=" << details->isDirty();
    if (details->isDirty()) {
        qInfo().noquote() << "[Connections] dirty-navigation prompt opened current="
                          << currentProfileId << "target=" << requestedProfileId;
        restoringSelection = true;
        tree->setCurrentIndex(previous);
        restoringSelection = false;
        const DirtyNavigationDecision decision = requestDirtyNavigationDecision();
        const char *decisionText = decision == DirtyNavigationDecision::Apply ? "Apply" :
            (decision == DirtyNavigationDecision::Discard ? "Discard" : "Cancel");
        qInfo() << "[Connections] popup result=" << decisionText;
        if (decision == DirtyNavigationDecision::Cancel) {
            qInfo() << "[Connections] dirty-navigation handler complete: cancelled";
            return;
        }
        navigationTransition = true;
        if (decision == DirtyNavigationDecision::Apply) {
            qInfo().noquote() << "[Connections] popup Apply begin profile="
                              << currentProfileId;
            if (!details->apply()) {
                navigationTransition = false;
                selectProfile(currentProfileId);
                qWarning().noquote() << "[Connections] popup Apply failed profile="
                                     << currentProfileId;
                return;
            }
            qInfo().noquote() << "[Connections] popup Apply end profile="
                              << currentProfileId;
        } else {
            details->revert();
        }
        navigationTransition = false;
        qInfo().noquote() << "[Connections] deferred target selection begin target="
                          << requestedProfileId;
        if (!requestedProfileId.isEmpty())
            selectProfile(requestedProfileId);
        else if (!requestedFolderId.isEmpty()) {
            const QModelIndex folder = findFolder(requestedFolderId);
            tree->setCurrentIndex(filterModel->mapFromSource(folder));
        } else if (requestedConnections || requestedUnfiled) {
            const QModelIndex connections = deviceModel->index(0, 0);
            const QModelIndex target = requestedConnections ? connections :
                deviceModel->index(0, 0, connections);
            tree->setCurrentIndex(filterModel->mapFromSource(target));
        }
        qInfo() << "[Connections] dirty-navigation handler complete";
        return;
    }
    presentSelection(current);
}

DevicePane::DirtyNavigationDecision DevicePane::requestDirtyNavigationDecision()
{
    if (dirtyNavigationDecisionProvider)
        return dirtyNavigationDecisionProvider();
    QMessageBox box(QMessageBox::Question, tr("Unsaved Device Changes"),
                    tr("Apply changes before changing selection?"),
                    QMessageBox::NoButton, this);
    QPushButton *apply = box.addButton(tr("Apply"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == apply) return DirtyNavigationDecision::Apply;
    if (box.clickedButton() == discard) return DirtyNavigationDecision::Discard;
    return DirtyNavigationDecision::Cancel;
}

void DevicePane::presentSelection(const QModelIndex &current)
{
    const QModelIndex source = sourceIndex(current);
    if (deviceModel->isProfile(source)) {
        const QString id = deviceModel->profileId(source);
        qInfo().noquote() << "[Connections] Device Details repopulate begin profile=" << id;
        details->showProfile(id);
        emit profileSelected(id);
        emit currentContextChanged(deviceModel->profileName(source),
                                   details->contextSummary());
        qInfo().noquote() << "[Connections] Device Details repopulate end profile=" << id;
    } else if (deviceModel->isFolder(source)) {
        details->showFolder(source.data(DeviceTreeModel::FolderIdRole).toString(),
                            source.data().toString(), profileCount(source));
        emit currentContextChanged(source.data().toString(), tr("Folder"));
    } else if (deviceModel->isConnections(source) || deviceModel->isUnfiled(source)) {
        details->clearSelection();
        emit currentContextChanged(source.data().toString(), tr("System folder"));
    } else {
        details->clearSelection();
        emit currentContextChanged({}, {});
    }
}

int DevicePane::profileCount(const QModelIndex &parent) const
{
    int count = 0;
    for (int row = 0; row < deviceModel->rowCount(parent); ++row) {
        const QModelIndex child = deviceModel->index(row, 0, parent);
        count += deviceModel->isProfile(child) ? 1 : profileCount(child);
    }
    return count;
}

QModelIndex DevicePane::findFolder(const QString &id) const
{
    std::function<QModelIndex(const QModelIndex &)> find =
        [&](const QModelIndex &parent) -> QModelIndex {
            for (int row = 0; row < deviceModel->rowCount(parent); ++row) {
                const QModelIndex child = deviceModel->index(row, 0, parent);
                if (child.data(DeviceTreeModel::FolderIdRole).toString() == id) return child;
                const QModelIndex nested = find(child);
                if (nested.isValid()) return nested;
            }
            return {};
        };
    return find({});
}

void DevicePane::renameFolderFromDetails(const QString &id, const QString &name)
{
    const QModelIndex folder = findFolder(id);
    if (folder.isValid()) deviceModel->setData(folder, name, Qt::EditRole);
}

void DevicePane::importProfiles() { emit importRequested(); }
void DevicePane::exportAll() { emit exportRequested(0, {}); }
void DevicePane::exportSelectedProfile()
{
    emit exportRequested(1, deviceModel->profileId(sourceIndex(selectedIndex())));
}
void DevicePane::exportFolder()
{
    const QModelIndex source = sourceIndex(selectedIndex());
    emit exportRequested(2, source.data(DeviceTreeModel::FolderIdRole).toString());
}
void DevicePane::loadPreferredMibs()
{
    const QString id = deviceModel->profileId(sourceIndex(selectedIndex()));
    if (!id.isEmpty()) emit loadPreferredMibsRequested(id);
}

void DevicePane::createFolder()
{
    QModelIndex parent = sourceIndex(selectedIndex());
    if (!deviceModel->isFolder(parent) && !deviceModel->isConnections(parent))
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
    {
        details->showProfile(id);
        details->setFocus(Qt::OtherFocusReason);
    }
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
