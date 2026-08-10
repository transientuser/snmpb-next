#ifndef DEVICEPANE_H
#define DEVICEPANE_H

#include "devicetreemodel.h"

#include <QWidget>

class QTreeView;

class DevicePane : public QWidget
{
    Q_OBJECT

public:
    DevicePane(const QString &sidecarFile,
               const QList<AgentProfileRecord> &profiles,
               QWidget *parent = nullptr);
    DeviceTreeModel *model() const;
    QTreeView *treeView() const;
    void setProfiles(const QList<AgentProfileRecord> &profiles);
    void renameProfile(const QString &profileId, const QString &newName);
    void placeDuplicate(const QString &sourceId, const QString &newId);

signals:
    void profileSelected(const QString &profileId);
    void editProfileRequested(const QString &profileId);
    void duplicateProfileRequested(const QString &profileId);
    void organizationPersisted();

private slots:
    void activate(const QModelIndex &index);
    void showContextMenu(const QPoint &position);
    void createFolder();
    void renameFolder();
    void deleteFolder();
    void editProfile();
    void duplicateProfile();

private:
    QModelIndex selectedIndex() const;
    DeviceTreeModel *deviceModel;
    QTreeView *tree;
};

#endif
