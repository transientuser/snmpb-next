#include "devicepane.h"
#include "agentprofileservice.h"
#include "communitycredentialservice.h"
#include "profilemetadataservice.h"
#include "usmcredentialservice.h"
#include "mainwindowlayout.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QTemporaryDir>
#include <QTreeView>
#include <QLineEdit>
#include <QLayout>
#include <QLabel>
#include <QDockWidget>
#include <QDialog>
#include <QMainWindow>
#include <QMenu>
#include <QPushButton>
#include <QTimer>
#include <QToolBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>

#include <iostream>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

QModelIndex Find(QAbstractItemModel *model, const QString &text,
                 const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        QModelIndex index = model->index(row, 0, parent);
        if (index.data().toString() == text)
            return index;
        QModelIndex nested = Find(model, text, index);
        if (nested.isValid())
            return nested;
    }
    return {};
}

QStringList ContextActions(DevicePane *pane, const QModelIndex &index)
{
    pane->treeView()->expandAll();
    pane->treeView()->setCurrentIndex(index);
    QStringList labels;
    QTimer::singleShot(0, [&labels]() {
        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
            for (QAction *action : menu->actions())
                if (!action->isSeparator()) labels.append(action->text());
            menu->close();
        }
    });
    QMetaObject::invokeMethod(pane, "showContextMenu", Qt::DirectConnection,
        Q_ARG(QPoint, index.isValid() ? pane->treeView()->visualRect(index).center() :
            QPoint(-1, -1)));
    return labels;
}
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);

    QMainWindow cleanWindow;
    QDockWidget cleanDevices("Devices", &cleanWindow);
    cleanDevices.setObjectName("DevicesDock");
    cleanDevices.setAllowedAreas(Qt::LeftDockWidgetArea);
    cleanDevices.setFeatures(QDockWidget::NoDockWidgetFeatures);
    cleanDevices.setWidget(new QWidget);
    cleanWindow.addDockWidget(Qt::LeftDockWidgetArea, &cleanDevices);
    RestoreMainWindowStateWithRequiredDevicesDock(
        &cleanWindow, &cleanDevices, QByteArray());
    cleanWindow.show();
    application.processEvents();
    if (!Check(cleanDevices.isVisible() && !cleanDevices.isFloating() &&
                   cleanWindow.dockWidgetArea(&cleanDevices) ==
                       Qt::LeftDockWidgetArea,
               "Devices dock is not visible and docked left by default"))
        return 1;
    cleanWindow.hide();

    QMainWindow legacyWindow;
    QDockWidget legacyDevices("Devices", &legacyWindow);
    legacyDevices.setObjectName("DevicesDock");
    legacyDevices.setWidget(new QWidget);
    legacyWindow.addDockWidget(Qt::LeftDockWidgetArea, &legacyDevices);
    legacyDevices.setFloating(true);
    legacyDevices.hide();
    const QByteArray legacyState = legacyWindow.saveState();

    QMainWindow upgradedWindow;
    QDockWidget upgradedDevices("Devices", &upgradedWindow);
    upgradedDevices.setObjectName("DevicesDock");
    upgradedDevices.setAllowedAreas(Qt::LeftDockWidgetArea);
    upgradedDevices.setFeatures(QDockWidget::NoDockWidgetFeatures);
    upgradedDevices.setWidget(new QWidget);
    upgradedWindow.addDockWidget(Qt::LeftDockWidgetArea, &upgradedDevices);
    RestoreMainWindowStateWithRequiredDevicesDock(
        &upgradedWindow, &upgradedDevices, legacyState);
    upgradedWindow.show();
    application.processEvents();
    if (!Check(upgradedDevices.isVisible(),
               "required Devices dock remained hidden after state restore") ||
        !Check(!upgradedDevices.isFloating(),
               "required Devices dock remained floating after state restore") ||
        !Check(upgradedWindow.dockWidgetArea(&upgradedDevices) ==
                   Qt::LeftDockWidgetArea,
               "required Devices dock was not restored on the left") ||
        !Check(upgradedDevices.widget() != nullptr,
               "required Devices dock has no integrated content"))
        return 1;
    upgradedWindow.hide();

    QTemporaryDir temporary;
    const QString sentinel = temporary.filePath("sentinel-agents.conf");
    QFile agentsFile(sentinel);
    if (!agentsFile.open(QIODevice::WriteOnly))
        return 1;
    agentsFile.write("unchanged");
    agentsFile.close();

    AgentProfileRecord profile =
        AgentProfileRepository::DefaultProfile("core-01", "192.0.2.1");
    profile.port = "1161";
    profile.v3 = true;
    profile.secname = "operator";
    AgentProfileRecord navigationTarget =
        AgentProfileRepository::DefaultProfile("edge-02", "192.0.2.2");
    AgentProfileRepository(temporary.filePath("agents.conf")).Save(
        {profile, navigationTarget});
    AgentProfileService profiles(temporary.filePath("agents.conf"));
    ProfileMetadataService metadata(temporary.filePath("profile-metadata.conf"));
    CommunityCredentialService communities(
        temporary.filePath("community-credentials.conf"),
        temporary.filePath("credential-bindings.conf"));
    CommunityCredentialRecord genericCommunity;
    genericCommunity.displayName = "Generic";
    genericCommunity.readCommunity = CredentialSecret("public");
    genericCommunity.writeCommunity = CredentialSecret("private");
    const QString genericCommunityId = communities.create(genericCommunity);
    communities.bind(profile.profileId, genericCommunityId);
    UsmCredentialService usm({}, UsmCredentialRepository(
        temporary.filePath("credential-identities.conf")));
    UsmCredentialRecord usmRecord;
    usmRecord.identity = {"usm-operator", CredentialKind::Usm};
    usmRecord.displayName = "Operations";
    usmRecord.securityName = "operator";
    usmRecord.authProtocol = 4;
    usm.applyCommitted({usmRecord});
    DevicePane pane(temporary.filePath("device-tree.conf"), profiles.profiles(), {},
                    &profiles, &metadata, &communities, &usm);
    if (!Check(pane.model() != nullptr && pane.treeView() != nullptr,
               "pane construction failed"))
        return 1;
    if (!Check(pane.verticalSplitter() && pane.verticalSplitter()->count() == 2 &&
               pane.detailsEditor(), "integrated tree/details splitter missing"))
        return 1;
    if (!Check(pane.findChild<QToolBar *>() == nullptr,
               "redundant device action toolbar remains visible"))
        return 1;
    const QModelIndex viewBoundary = pane.treeView()->rootIndex();
    const QModelIndex visualRoot = pane.treeView()->model()->index(0, 0);
    QModelIndex unfiled = pane.treeView()->model()->index(0, 0, visualRoot);
    if (!Check(!viewBoundary.isValid() && visualRoot.isValid() &&
                   visualRoot.data().toString() == "Connections",
               "permanent Connections root is not visible") ||
        !Check(unfiled.isValid() && unfiled.row() == 0,
               "Unfiled is not the first visible connection group") ||
        !Check(!(unfiled.flags() & Qt::ItemIsEditable),
               "system roots can be renamed"))
        return 1;
    const QStringList rootActions = ContextActions(&pane, visualRoot);
    if (!Check(rootActions == QStringList({"New Device", "New Folder", "Import...",
                                           "Export...", "Sort"}),
               "Connections context actions are incorrect") ||
        !Check(ContextActions(&pane, unfiled) == QStringList({"New Device", "Sort"}),
               "Unfiled exposes normal-folder actions"))
        return 1;
    int exportScope = -1;
    QString exportId;
    int importRequests = 0;
    QObject::connect(&pane, &DevicePane::exportRequested,
                     [&](int scope, const QString &id) {
        exportScope = scope; exportId = id;
    });
    QObject::connect(&pane, &DevicePane::importRequested,
                     [&]() { ++importRequests; });
    QMetaObject::invokeMethod(&pane, "importProfiles");
    QMetaObject::invokeMethod(&pane, "exportAll");
    if (!Check(importRequests == 1 && exportScope == 0 && exportId.isEmpty(),
               "Connections Import/Export-all scope is incorrect"))
        return 1;
    QModelIndex profileIndex = Find(pane.treeView()->model(), "core-01");
    if (!Check(profileIndex.isValid(), "profile missing from pane"))
        return 1;
    const QStringList deviceActions = ContextActions(&pane, profileIndex);
    if (!Check(deviceActions == QStringList({"Activate / Select", "Properties",
                                             "Duplicate", "Move to Folder",
                                             "Export...", "Delete"}),
               "device context actions are incorrect"))
        return 1;
    pane.treeView()->setCurrentIndex(profileIndex);
    QMetaObject::invokeMethod(&pane, "exportSelectedProfile");
    if (!Check(exportScope == 1 && exportId == profile.profileId,
               "connection Export scope is incorrect"))
        return 1;
    QString selected;
    QObject::connect(&pane, &DevicePane::profileSelected,
                     [&selected](const QString &id) { selected = id; });
    pane.treeView()->setCurrentIndex(profileIndex);
    pane.treeView()->clicked(profileIndex);
    if (!Check(selected == profile.profileId,
               "pane emitted wrong profile identity"))
        return 1;
    DeviceDetailsEditor *details = pane.detailsEditor();
    if (!Check(details->currentProfileId() == profile.profileId &&
               details->nameEditor()->text() == "core-01" &&
               details->hostEditor()->text() == "192.0.2.1",
               "device selection did not populate details"))
        return 1;
    pane.resize(500, 900);
    pane.verticalSplitter()->setSizes({250, 650});
    pane.show();
    QApplication::processEvents();
    const QLabel *nameLabel = details->findChild<QLabel *>("DeviceNameLabel");
    const QLabel *hostLabel = details->findChild<QLabel *>("DeviceHostLabel");
    int visibleNameLabels = 0;
    for (const QLabel *label : details->findChildren<QLabel *>())
        if (label->isVisible() && label->text() == "Name")
            ++visibleNameLabels;
    int visibleNameEditors = 0;
    int visibleHostEditors = 0;
    for (const QLineEdit *editor : details->findChildren<QLineEdit *>())
        if (editor->isVisible() && editor->objectName() == "DeviceNameEditor")
            ++visibleNameEditors;
        else if (editor->isVisible() && editor->objectName() == "DeviceHostEditor")
            ++visibleHostEditors;
    int visiblePortLabels = 0;
    for (const QLabel *label : details->findChildren<QLabel *>())
        if (label->isVisible() &&
            (label->text() == "Port" || label->text() == "SNMP Port"))
            ++visiblePortLabels;
    if (!Check(nameLabel && hostLabel && visibleNameLabels == 1 &&
               visibleNameEditors == 1 && visibleHostEditors == 1 &&
               visiblePortLabels == 0 &&
               !details->findChild<QLineEdit *>("DevicePortEditor") &&
               nameLabel->width() >= nameLabel->sizeHint().width() &&
               nameLabel->geometry().right() < details->nameEditor()->geometry().left() &&
               hostLabel->geometry().right() < details->hostEditor()->geometry().left() &&
               details->nameEditor()->geometry().left() ==
                   details->hostEditor()->geometry().left() &&
               details->nameEditor()->width() == details->hostEditor()->width(),
               "Name and Host / IP rows do not share non-overlapping form geometry"))
        return 1;
    if (!Check(details->usmCredentialEditor()->findText("No credential selected") < 0 &&
                   details->usmCredentialEditor()->findData("usm-operator") >= 0 &&
                   details->usmCredentialEditor()->itemText(
                       details->usmCredentialEditor()->count() - 1) ==
                       "<New...>" &&
                   details->usmCredentialEditor()->findText(
                       "<New Credential...>") < 0,
               "SNMPv3 credential choices are incorrect"))
        return 1;
    const int newCredentialIndex = details->usmCredentialEditor()->count() - 1;
    const QMetaObject::Connection cancelledCreation = QObject::connect(
        details, &DeviceDetailsEditor::manageUsmCredentialsRequested, []() {});
    details->usmCredentialEditor()->setCurrentIndex(newCredentialIndex);
    QObject::disconnect(cancelledCreation);
    if (!Check(details->usmCredentialEditor()->currentData().toString() ==
                   "usm-operator",
               "cancelled USM creation did not preserve the previous selection"))
        return 1;
    const QMetaObject::Connection savedCreation = QObject::connect(
        details, &DeviceDetailsEditor::manageUsmCredentialsRequested, [&]() {
            UsmCredentialRecord created;
            created.identity = {"usm-created", CredentialKind::Usm};
            created.displayName = "Created Credential";
            created.securityName = "new-user";
            usm.applyCommitted({usmRecord, created});
        });
    details->usmCredentialEditor()->setCurrentIndex(
        details->usmCredentialEditor()->count() - 1);
    QObject::disconnect(savedCreation);
    if (!Check(details->usmCredentialEditor()->currentData().toString() ==
                   "usm-created" && details->isDirty(),
               "saved USM credential was not refreshed and auto-selected"))
        return 1;
    const auto useAdvancedPort = [&](const QString &expected,
                                     const QString &replacement, bool accept) {
        bool foundExpectedPort = false;
        QTimer::singleShot(0, [&]() {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            if (QLineEdit *advancedPort = dialog->findChild<QLineEdit *>(
                    "AdvancedSnmpPort")) {
                foundExpectedPort = advancedPort->text() == expected;
                advancedPort->setText(replacement);
            }
            accept ? dialog->accept() : dialog->reject();
        });
        details->findChild<QPushButton *>("AdvancedConnectionSettings")->click();
        return foundExpectedPort;
    };
    if (!Check(useAdvancedPort("1161", "2161", true) &&
                   useAdvancedPort("2161", "3161", false) &&
                   useAdvancedPort("2161", "2161", false),
               "Advanced Port edit/cancel transaction is incorrect"))
        return 1;
    const int activeBeforeTab = details->protocolEditor()->currentIndex();
    details->configurationTabs()->setCurrentIndex(1);
    if (!Check(details->protocolEditor()->currentIndex() == activeBeforeTab,
               "SNMPv3 configuration tab changed the active protocol"))
        return 1;
    details->protocolEditor()->setCurrentIndex(1);
    if (!Check(details->protocolEditor()->count() == 3 &&
                   details->protocolEditor()->itemText(0) == "SNMPv1" &&
                   details->protocolEditor()->itemText(1) == "SNMPv2c" &&
                   details->protocolEditor()->itemText(2) == "SNMPv3" &&
                   details->protocolEditor()->currentText() == "SNMPv2c" &&
                   details->configurationTabs()->count() == 2 &&
                   details->configurationTabs()->tabText(0) == "SNMPv1/v2c" &&
                   details->configurationTabs()->tabText(1) == "SNMPv3",
               "active protocol selector or configuration tabs are incorrect"))
        return 1;
    bool legacyExplanationFound = false;
    for (QLabel *label : details->findChildren<QLabel *>())
        if (label->text().contains("share the connection's community"))
            legacyExplanationFound = true;
    QComboBox *communitySource = details->findChild<QComboBox *>(
        "CommunityCredentialSource");
    QComboBox *communityChoice = details->findChild<QComboBox *>(
        "CommunityCredentialSelection");
    if (!Check(!legacyExplanationFound && communitySource && communityChoice,
               "combined community tab or explanatory-text removal is incorrect"))
        return 1;
    communitySource->setCurrentIndex(0);
    if (!Check(!communityChoice->isEnabled(),
               "Inline community mode did not disable reusable selection"))
        return 1;
    communitySource->setCurrentIndex(1);
    if (!Check(communityChoice->isEnabled() && communityChoice->count() > 0,
               "Reusable community mode is not functional"))
        return 1;
    details->hostEditor()->setText("198.51.100.8");
    details->tagsEditor()->setText("Core, Lab");
    if (!Check(details->apply(), "details Apply failed") ||
        !Check(profiles.findById(profile.profileId)->address == "198.51.100.8" &&
                   profiles.findById(profile.profileId)->port == "2161" &&
                   profiles.findById(profile.profileId)->secname == "new-user" &&
                   metadata.metadataForProfile(profile.profileId).usmCredentialId ==
                       "usm-created" &&
               metadata.metadataForProfile(profile.profileId).tags ==
                   QStringList({"Core", "Lab"}),
               "details Apply did not commit through services"))
        return 1;
    details->hostEditor()->setText("203.0.113.9");
    QMetaObject::invokeMethod(details->hostEditor(), "textEdited",
                              Q_ARG(QString, QStringLiteral("203.0.113.9")));
    if (!Check(details->isDirty(), "details edit did not mark dirty"))
        return 1;
    details->revert();
    if (!Check(!details->isDirty() && details->hostEditor()->text() == "198.51.100.8",
               "Revert did not restore the committed profile"))
        return 1;
    if (!Check(useAdvancedPort("2161", "3261", true),
               "Advanced did not reopen with the persisted Port"))
        return 1;
    details->revert();
    if (!Check(useAdvancedPort("2161", "2161", false),
               "Revert did not restore the persisted Port working value"))
        return 1;

    // Reproduce the original failure conditions: Apply synchronously emits
    // profileApplied/metadataChanged, both of which rebuild the model while a
    // dirty-navigation selection transition is in progress.
    QObject::connect(details, &DeviceDetailsEditor::profileApplied, &pane,
                     [&pane, &profiles](const QString &) {
        pane.setProfiles(profiles.profiles());
    });
    QObject::connect(&metadata, &ProfileMetadataService::metadataChanged, &pane,
                     [&pane, &metadata](const QString &) {
        pane.setMetadata(metadata.allMetadata());
    });
    int profileUpdates = 0;
    int targetSelections = 0;
    int targetHeaders = 0;
    QObject::connect(&profiles, &AgentProfileService::profileUpdated,
                     [&profileUpdates](const QString &) { ++profileUpdates; });
    QObject::connect(&pane, &DevicePane::profileSelected,
                     [&targetSelections, &navigationTarget](const QString &id) {
        if (id == navigationTarget.profileId) ++targetSelections;
    });
    QObject::connect(&pane, &DevicePane::currentContextChanged,
                     [&targetHeaders](const QString &name, const QString &) {
        if (name == "edge-02") ++targetHeaders;
    });
    const auto selectAndDirtyA = [&](const QString &address) {
        pane.treeView()->setCurrentIndex(Find(pane.treeView()->model(), "core-01"));
        details->hostEditor()->setText(address);
        QMetaObject::invokeMethod(details->hostEditor(), "textEdited",
                                  Q_ARG(QString, address));
    };
    const auto requestB = [&]() {
        pane.treeView()->setCurrentIndex(Find(pane.treeView()->model(), "edge-02"));
        application.processEvents();
    };

    selectAndDirtyA("198.51.100.21");
    pane.setDirtyNavigationDecisionProvider([] {
        return DevicePane::DirtyNavigationDecision::Apply;
    });
    requestB();
    if (!Check(profileUpdates == 1, "popup Apply persisted A more than once") ||
        !Check(profiles.findById(profile.profileId)->address == "198.51.100.21",
               "popup Apply did not persist A") ||
        !Check(details->currentProfileId() == navigationTarget.profileId &&
                   targetSelections == 1 && targetHeaders == 1 && !details->isDirty(),
               "popup Apply did not select and present B exactly once"))
        return 1;

    selectAndDirtyA("198.51.100.22");
    pane.setDirtyNavigationDecisionProvider([] {
        return DevicePane::DirtyNavigationDecision::Discard;
    });
    requestB();
    if (!Check(profiles.findById(profile.profileId)->address == "198.51.100.21" &&
                   details->currentProfileId() == navigationTarget.profileId,
               "popup Discard persisted A or failed to select B"))
        return 1;

    selectAndDirtyA("198.51.100.23");
    pane.setDirtyNavigationDecisionProvider([] {
        return DevicePane::DirtyNavigationDecision::Cancel;
    });
    requestB();
    if (!Check(details->currentProfileId() == profile.profileId &&
                   details->isDirty() &&
                   details->hostEditor()->text() == "198.51.100.23",
               "popup Cancel did not preserve A and its working copy"))
        return 1;
    details->revert();
    pane.setDirtyNavigationDecisionProvider({});

    QModelIndex folder = pane.model()->createFolder("Datacenter");
    if (!Check(folder.isValid() &&
               pane.model()->moveProfile(profile.profileId, folder),
               "pane hierarchy setup failed") ||
        !Check(Find(pane.model(), "Datacenter").isValid(),
               "folder missing from pane"))
        return 1;
    QModelIndex folderProxy = Find(pane.treeView()->model(), "Datacenter");
    pane.treeView()->setCurrentIndex(folderProxy);
    if (!Check(!details->currentFolderId().isEmpty() &&
               details->currentProfileId().isEmpty(),
               "folder selection did not present folder-specific details"))
        return 1;
    folder = Find(pane.model(), "Datacenter");
    if (!Check(folder.parent().isValid() &&
                   pane.model()->isConnections(folder.parent()),
               "top-level user folder was not created below Connections"))
        return 1;
    const QStringList folderActions = ContextActions(
        &pane, Find(pane.treeView()->model(), "Datacenter"));
    if (!Check(folderActions == QStringList({"New Device", "New Folder", "Rename",
                                             "Delete", "Export...", "Sort"}) &&
                   !folderActions.contains("New Subfolder"),
               "normal folder context actions are incorrect"))
        return 1;
    pane.treeView()->setCurrentIndex(Find(pane.treeView()->model(), "Datacenter"));
    QMetaObject::invokeMethod(&pane, "exportFolder");
    if (!Check(exportScope == 2 && !exportId.isEmpty(),
               "folder subtree Export scope is incorrect"))
        return 1;

    QFile verify(sentinel);
    if (!verify.open(QIODevice::ReadOnly))
        return 1;
    if (!Check(verify.readAll() == "unchanged", "opening pane changed agents.conf"))
        return 1;

    AgentProfileRecord lab =
        AgentProfileRepository::DefaultProfile("lab-switch", "2001:db8::10");
    pane.setProfiles({profile, lab});
    pane.filterEdit()->setText("2001:db8::10");
    if (!Check(Find(pane.treeView()->model(), "lab-switch").isValid(),
               "address filter did not retain matching profile") ||
        !Check(!Find(pane.treeView()->model(), "core-01").isValid(),
               "address filter retained nonmatching profile"))
        return 1;
    pane.filterEdit()->setText("core-01");
    if (!Check(Find(pane.treeView()->model(), "Datacenter").isValid(),
               "filter did not retain matching parent folder"))
        return 1;
    pane.setMetadata({{profile.profileId, "Primary aggregation router", {"Backbone"},
                       {"IF-MIB"}},
                      {lab.profileId, "Temporary validation device", {"QA"},
                       {"SNMPv2-MIB"}}});
    pane.filterEdit()->setText("backbone");
    if (!Check(Find(pane.treeView()->model(), "core-01").isValid() &&
               Find(pane.treeView()->model(), "Datacenter").isValid(),
               "tag filter or parent visibility failed"))
        return 1;
    pane.filterEdit()->setText("validation device");
    if (!Check(Find(pane.treeView()->model(), "lab-switch").isValid() &&
               !Find(pane.treeView()->model(), "core-01").isValid(),
               "notes filter/nonmatch exclusion failed"))
        return 1;
    pane.filterEdit()->setText("IF-MIB");
    if (!Check(Find(pane.treeView()->model(), "core-01").isValid(),
               "preferred MIB filter failed"))
        return 1;
    pane.filterEdit()->clear();
    QModelIndex proxyFolder = Find(pane.treeView()->model(), "Datacenter");
    pane.treeView()->setCurrentIndex(proxyFolder);
    QString requestedFolder;
    QObject::connect(&pane, &DevicePane::newProfileRequested,
                     [&requestedFolder](const QString &id) { requestedFolder = id; });
    QMetaObject::invokeMethod(&pane, "newProfile");
    if (!Check(!requestedFolder.isEmpty(), "new profile did not retain folder target"))
        return 1;
    pane.setProfiles({profile, lab});
    pane.placeCreatedProfile(lab.profileId);
    bool labPlaced = false;
    for (const DeviceProfilePlacement &placement : pane.model()->state().placements)
        if (placement.profileId == lab.profileId && placement.parentId == requestedFolder)
            labPlaced = true;
    if (!Check(labPlaced, "created profile was not placed in selected folder"))
        return 1;
    pane.treeView()->setCurrentIndex(QModelIndex());
    requestedFolder = QStringLiteral("not-empty");
    QMetaObject::invokeMethod(&pane, "newProfile");
    if (!Check(requestedFolder.isEmpty(),
               "root New Device did not target Unfiled"))
        return 1;
    return 0;
}
