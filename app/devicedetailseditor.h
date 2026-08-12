#ifndef DEVICEDETAILSEDITOR_H
#define DEVICEDETAILSEDITOR_H

#include "agentprofilerepository.h"
#include "profilemetadatarepository.h"

#include <QWidget>

class AgentProfileService;
class CommunityCredentialService;
class ProfileMetadataService;
class UsmCredentialService;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabWidget;

class DeviceDetailsEditor : public QWidget
{
    Q_OBJECT
public:
    DeviceDetailsEditor(AgentProfileService *profiles,
                        ProfileMetadataService *metadata,
                        CommunityCredentialService *communities,
                        UsmCredentialService *usm,
                        QWidget *parent = nullptr);

    bool isDirty() const;
    QString currentProfileId() const;
    QString currentFolderId() const;
    QString contextSummary() const;
    void showProfile(const QString &profileId);
    void showFolder(const QString &folderId, const QString &name, int deviceCount);
    void clearSelection();

    QComboBox *protocolEditor() const;
    QStackedWidget *protocolPages() const;
    QTabWidget *configurationTabs() const;
    QLineEdit *nameEditor() const;
    QLineEdit *hostEditor() const;
    QLineEdit *tagsEditor() const;
    QPushButton *applyButton() const;
    QPushButton *revertButton() const;
    QComboBox *usmCredentialEditor() const;

public slots:
    bool apply();
    void revert();

signals:
    void dirtyChanged(bool dirty);
    void profileApplied(const QString &profileId);
    void folderRenameRequested(const QString &folderId, const QString &name);
    void editPreferredMibsRequested(const QString &profileId);
    void editConnectionSettingsRequested(const QString &profileId);
    void manageUsmCredentialsRequested();

private:
    void buildUi();
    void setDirty(bool value);
    void loadProfileControls();
    void refreshProtocolPage();
    void refreshCredentialChoices();
    void selectNewUsmCredential();
    void showAdvancedSettings();

    AgentProfileService *profileService;
    ProfileMetadataService *metadataService;
    CommunityCredentialService *communityService;
    UsmCredentialService *usmService;
    AgentProfileRecord workingProfile{};
    ProfileMetadataRecord workingMetadata{};
    QString folderId;
    QString folderName;
    int folderDevices = 0;
    bool loading = false;
    bool dirty = false;
    bool activeProtocolExplicitlyChanged = false;
    bool usmCredentialExplicitlyChanged = false;
    QString selectedUsmCredentialId;
    int legacyProtocol = 0;

    QLabel *heading;
    QWidget *profilePage;
    QWidget *folderPage;
    QWidget *emptyPage;
    QStackedWidget *selectionPages;
    QLineEdit *name;
    QLineEdit *host;
    QComboBox *protocol;
    QStackedWidget *protocolStack;
    QTabWidget *protocolTabs;
    QComboBox *credentialSource;
    QComboBox *communityCredential;
    QLabel *readCommunityStatus;
    QLabel *writeCommunityStatus;
    QLineEdit *securityName;
    QLabel *securityLevel;
    QComboBox *usmCredential;
    QLabel *authProtocol;
    QLabel *privacyProtocol;
    QLineEdit *tags;
    QPlainTextEdit *notes;
    QLabel *preferredMibs;
    QPushButton *editMibs;
    QPushButton *advancedAction;
    QPushButton *settingsEditAction;
    QLineEdit *folderNameEdit;
    QLabel *folderCount;
    QPushButton *applyAction;
    QPushButton *revertAction;
};

#endif
