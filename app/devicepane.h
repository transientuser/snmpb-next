#ifndef DEVICEPANE_H
#define DEVICEPANE_H

#include "devicetreemodel.h"

#include <QWidget>

class QTreeView;
class QLineEdit;
class QSortFilterProxyModel;

class DevicePane : public QWidget
{
    Q_OBJECT

public:
    DevicePane(const QString &sidecarFile,
               const QList<AgentProfileRecord> &profiles,
               const QList<ProfileMetadataRecord> &metadata = {},
               QWidget *parent = nullptr);
    DeviceTreeModel *model() const;
    QTreeView *treeView() const;
    QLineEdit *filterEdit() const;
    void setProfiles(const QList<AgentProfileRecord> &profiles);
    void setMetadata(const QList<ProfileMetadataRecord> &metadata);
    void renameProfile(const QString &profileId, const QString &newName);
    void placeDuplicate(const QString &sourceId, const QString &newId);
    void placeCreatedProfile(const QString &profileId);
    bool importTreeState(const DeviceTreeState &state);
    void reloadTree();

signals:
    void profileSelected(const QString &profileId);
    void editProfileRequested(const QString &profileId);
    void duplicateProfileRequested(const QString &profileId);
    void newProfileRequested(const QString &folderId);
    void deleteProfileRequested(const QString &profileId);
    void organizationPersisted();
    void importRequested();
    void exportRequested(int scope, const QString &id);

private slots:
    void activate(const QModelIndex &index);
    void showContextMenu(const QPoint &position);
    void createFolder();
    void renameFolder();
    void deleteFolder();
    void editProfile();
    void duplicateProfile();
    void newProfile();
    void deleteProfile();
    void importProfiles();
    void exportAll();
    void exportSelectedProfile();
    void exportFolder();

private:
    QModelIndex selectedIndex() const;
    QModelIndex sourceIndex(const QModelIndex &index) const;
    void selectProfile(const QString &profileId);
    DeviceTreeModel *deviceModel;
    QSortFilterProxyModel *filterModel;
    QTreeView *tree;
    QLineEdit *filter;
    QString pendingFolderId;
};

#endif
