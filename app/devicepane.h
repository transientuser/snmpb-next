#ifndef DEVICEPANE_H
#define DEVICEPANE_H

#include "devicetreemodel.h"
#include "devicedetailseditor.h"

#include <QWidget>
#include <QHash>
#include <functional>

class QTreeView;
class QLineEdit;
class QSortFilterProxyModel;
class QSplitter;
class AgentProfileService;
class ProfileMetadataService;
class CommunityCredentialService;
class UsmCredentialService;

class DevicePane : public QWidget
{
    Q_OBJECT

public:
    enum class DirtyNavigationDecision { Apply, Discard, Cancel };
    DevicePane(const QString &sidecarFile,
               const QList<AgentProfileRecord> &profiles,
               const QList<ProfileMetadataRecord> &metadata = {},
               AgentProfileService *profileService = nullptr,
               ProfileMetadataService *metadataService = nullptr,
               CommunityCredentialService *communityService = nullptr,
               UsmCredentialService *usmService = nullptr,
               QWidget *parent = nullptr);
    DeviceTreeModel *model() const;
    QTreeView *treeView() const;
    QLineEdit *filterEdit() const;
    DeviceDetailsEditor *detailsEditor() const;
    QSplitter *verticalSplitter() const;
    void setProfiles(const QList<AgentProfileRecord> &profiles);
    void setMetadata(const QList<ProfileMetadataRecord> &metadata);
    void setCredentialHealth(const QHash<QString, QString> &health);
    void renameProfile(const QString &profileId, const QString &newName);
    void placeDuplicate(const QString &sourceId, const QString &newId);
    void placeCreatedProfile(const QString &profileId);
    bool importTreeState(const DeviceTreeState &state);
    void reloadTree();
    void setDirtyNavigationDecisionProvider(
        std::function<DirtyNavigationDecision()> provider);

signals:
    void profileSelected(const QString &profileId);
    void editProfileRequested(const QString &profileId);
    void duplicateProfileRequested(const QString &profileId);
    void newProfileRequested(const QString &folderId);
    void deleteProfileRequested(const QString &profileId);
    void organizationPersisted();
    void importRequested();
    void exportRequested(int scope, const QString &id);
    void loadPreferredMibsRequested(const QString &profileId);
    void manageUsmCredentialsRequested();
    void currentContextChanged(const QString &name, const QString &summary);

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
    void loadPreferredMibs();
    void currentChanged(const QModelIndex &current, const QModelIndex &previous);
    void renameFolderFromDetails(const QString &folderId, const QString &name);

private:
    QModelIndex selectedIndex() const;
    QModelIndex sourceIndex(const QModelIndex &index) const;
    void selectProfile(const QString &profileId);
    int profileCount(const QModelIndex &sourceFolder) const;
    QModelIndex findFolder(const QString &folderId) const;
    DirtyNavigationDecision requestDirtyNavigationDecision();
    void presentSelection(const QModelIndex &proxyIndex);
    DeviceTreeModel *deviceModel;
    QSortFilterProxyModel *filterModel;
    QTreeView *tree;
    QLineEdit *filter;
    DeviceDetailsEditor *details;
    QSplitter *splitter;
    QString pendingFolderId;
    bool restoringSelection = false;
    bool navigationTransition = false;
    std::function<DirtyNavigationDecision()> dirtyNavigationDecisionProvider;
};

#endif
