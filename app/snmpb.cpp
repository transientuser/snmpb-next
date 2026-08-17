/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <functional>
#include <QtGui>
#include <qfileinfo.h>
#include <qdir.h>
#include <qmessagebox.h>
#include <QFileDialog>
#include <QFile>
#include <qdockwidget.h>
#include <QGridLayout>
#include <QComboBox>
#include <QLabel>
#include <QSplitter>
#include <QElapsedTimer>
#include <qmenu.h>
#include "snmpb.h"
#include "mibmodule.h"
#include "agent.h"
#include "trap.h"
#if SNMPB_ENABLE_QWT
#include "graphmanager.h"
#endif
#include "logsnmpb.h"
#include "mibeditor.h"
#include "discovery.h"
#include "devicepane.h"
#include "mainwindowlayout.h"
#include "productidentity.h"
#include "diagnosticlogger.h"
#include "miblibrarywidget.h"
#include "mibprofile.h"

#include "agentprofile.h"
#include "agentprofileservice.h"
#include "profilemetadataservice.h"
#include "profiletransfer.h"
#include "discoverydestination.h"
#include "mibcollection.h"
#include "usmprofile.h"
#include "communitycredentialservice.h"
#include "communitycredentialmanager.h"
#include "usmcredentialservice.h"
#include "preferences.h"

// These are needed to get the libraries version strings for the about box
#include "smi.h"
#include "tomcrypt.h"
#if SNMPB_ENABLE_QWT
#include "qwt.h"
#endif
#include "snmp_pp/config_snmp_pp.h"

#define SMI_CONFIG_FILE          "smi.conf"
#define BOOT_COUNTER_CONFIG_FILE "boot_counter.conf"
#define USM_USERS_CONFIG_FILE    "usm_users.conf"
#define AGENTS_CONFIG_FILE       "agents.conf"
#define LOG_CONFIG_FILE          "log.conf"
#define GRAPHS_CONFIG_FILE       "graphs.conf"
#define DEVICE_TREE_CONFIG_FILE  "device-tree.conf"
#define PROFILE_METADATA_CONFIG_FILE "profile-metadata.conf"
#define CREDENTIAL_IDENTITIES_CONFIG_FILE "credential-identities.conf"
#define COMMUNITY_CREDENTIALS_CONFIG_FILE "community-credentials.conf"
#define CREDENTIAL_BINDINGS_CONFIG_FILE "credential-bindings.conf"

#if SNMPB_ENABLE_QWT
#define SNMPB_QWT_ABOUT_LINE \
    "QWT [v%5] (<a href=http://qwt.sourceforge.net>http://qwt.sourceforge.net</a>)<br>"
#define SNMPB_QT_ABOUT_LINE \
    "QT [v%6] (<a href=http://qt.io>http://qt.io</a>)"
#else
#define SNMPB_QWT_ABOUT_LINE ""
#define SNMPB_QT_ABOUT_LINE \
    "QT [v%5] (<a href=http://qt.io>http://qt.io</a>)"
#endif


Snmpb::Snmpb(bool offline)
{
    QElapsedTimer settingsTimer; settingsTimer.start();
    DiagnosticLogger::log("Startup", "settings initialization begin");
    // First thing to do is to give up root privileges that allow permission to
    // bind on privileged ports (<1024). This is needed to bind on 
    // the RFC-defined trap port number 162 on UNIX machines.
    prefs = new Preferences(this);
    DiagnosticLogger::log("Startup", QStringLiteral("preferences load complete elapsed_ms=%1").arg(settingsTimer.elapsed()));
#ifndef WIN32 
    // Allows to bind on privileged ports only if it is the standard trap port...
    if (! (prefs->ShouldListenStdTrapPort4() || prefs->ShouldListenStdTrapPort6()))
    {
        setuid(getuid());
    }
#endif
    agent = new Agent(this, offline);
    DiagnosticLogger::log("Traps", "trap receiver construction complete");
    start_issuccess = agent->GetStartupResult(start_msg);    
#ifndef WIN32 
    // Drop root privileges
    if (setuid(getuid()) < 0)
    {
        printf("Unable to drop root privileges: %m\n");
    }
#endif
    // Note: beware as anything BEFORE this point is run as root on UNIX ... 

    CheckForConfigFiles();
    DiagnosticLogger::log("Startup", QStringLiteral("settings initialization complete elapsed_ms=%1").arg(settingsTimer.elapsed()));
}

Snmpb::~Snmpb()
{
    delete agent;
    agent = nullptr;
}

void Snmpb::BindToGUI(QMainWindow* mw)
{
    if (start_issuccess == false)
    {
        QMessageBox::critical(nullptr, tr("MIB Navigator"), start_msg, QMessageBox::Ok);

        // Desperate measures: delete the preferences file so 
        // at the next startup, the app might have a chance to start
        QFile(GetSmiConfigFile()).remove();
        QFile(QSettings().fileName()).remove(); // this one is probably overkill
        exit (-1);
    }
    else
    {
        if (start_msg != "")
            QMessageBox::warning(nullptr, tr("MIB Navigator"), start_msg, QMessageBox::Ok);
        agent->StartTrapTimer();
    }

    w.setupUi(mw);
    DiagnosticLogger::log("UI", "main window UI setup complete");
    // The Connections navigator is the visible authoritative selector. These
    // controls remain hidden temporarily as compatibility state for legacy
    // request consumers while selection is synchronized by stable profile ID.
    w.AgentProperties->hide();
    w.actionManageAgentProfiles->setVisible(false);
    prefs->RestoreWindowGeometry(*mw);
    static auto saver = std::bind(&Preferences::SaveWindowGeometry, std::ref(*mw));
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit, saver);

    connect(&loader, SIGNAL ( LogError(QString) ),
            w.LogOutput, SLOT ( append (QString) ));

    // Creation order is VERY important here
    logsnmpb = new LogSnmpb(this);
    DiagnosticLogger::attachLogWidget(w.LogOutput);
    QSettings mibRootSettings;
    const bool migrationComplete = mibRootSettings.value(
        "mib-library/migration-v1-complete", false).toBool();
    MibCollectionResult collectionResult = MibCollection(
        MibCollection::configuredRoot(mibRootSettings)).initialize(
            prefs->DefaultMibPaths(), migrationComplete ? QString() : MibCollection::legacyManagedRoot());
    if (!collectionResult.success)
        DiagnosticLogger::log("MIB", tr("MIB collection initialization failed: %1")
                              .arg(collectionResult.error));
    else {
        mibRootSettings.setValue("mib-library/migration-v1-complete", true);
        DiagnosticLogger::log("MIB", tr("MIB collection initialized standards-copied=%1 imported-copied=%2 identical=%3 conflicts=%4")
            .arg(collectionResult.standardsCopied).arg(collectionResult.importedCopied)
            .arg(collectionResult.identicalSkipped).arg(collectionResult.conflicts.size()));
    }
    QElapsedTimer mibStartupTimer; mibStartupTimer.start();
    modules = new MibModule(this);
    DiagnosticLogger::log("Startup", QStringLiteral("MIB initialization complete elapsed_ms=%1").arg(mibStartupTimer.elapsed()));
    profileService = new AgentProfileService(GetAgentsConfigFile(), this);
    DiagnosticLogger::log("Connections", QStringLiteral("profiles loaded count=%1")
                          .arg(profileService->profiles().size()));
    communityCredentialService = new CommunityCredentialService(
        GetCommunityCredentialsConfigFile(), GetCredentialBindingsConfigFile(), this);
    profileMetadataService = new ProfileMetadataService(
        GetProfileMetadataConfigFile(), this);
    DiagnosticLogger::log("Connections", QStringLiteral("profile metadata loaded count=%1")
                          .arg(profileMetadataService->allMetadata().size()));
    devicePlacementService = new DeviceTreePlacementService(
        GetDeviceTreeConfigFile(), this);
    apm = new AgentProfileManager(this, profileService, profileMetadataService);
    QElapsedTimer phaseTimer; phaseTimer.start(); prefs->Init();
    DiagnosticLogger::log("Startup", QStringLiteral("settings UI load complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    trap = new Trap(this);
    agent->Init();
    upm = new USMProfileManager(this);
    connect(upm, &USMProfileManager::CredentialsChanged,
            apm, &AgentProfileManager::RefreshCredentialChoices);
#if SNMPB_ENABLE_QWT
    gm = new GraphManager(this);
#else
    w.TabW->setTabVisible(w.TabW->indexOf(w.GraphsTab), false);
#endif
    editor = new MibEditor(this);
    MibLibraryWidget::Callbacks mibLibraryCallbacks;
    mibLibraryCallbacks.validate = [this](const QString &path, QString *error) {
        return modules->ValidateModuleFile(path, error);
    };
    mibLibraryCallbacks.downloadsCompleted = [this](const QStringList &requested, bool load) {
        modules->RescanPath(); QString indexError; modules->RefreshDependencyIndex(&indexError);
        if (!indexError.isEmpty()) DiagnosticLogger::log("MIB", indexError);
        if (load) modules->LoadPreferredModules(requested);
    };
    mibLibraryCallbacks.metadata = [this](const QString &module, const QString &path) {
        return modules->ModuleMetadata(module, path);
    };
    mibLibraryCallbacks.localInventory = [this]() {
        return modules->AvailableModuleRecords();
    };
    const auto libraryDependencySummary = [this]() {
        MibLibraryWidget::DependencySummary summary;
        MibDependencyIndex *index = modules->DependencyIndex();
        const QStringList moduleNames = index->moduleNames();
        summary.stale = modules->DependencyIndexStale();
        summary.knownModules = moduleNames.size();
        QSet<QString> unresolved;
        QSet<QString> ambiguous;
        for (const QString &module : moduleNames) {
            const QStringList imports = index->imports(module);
            summary.relationships += imports.size();
            for (const QString &dependency : imports) {
                const MibProviderStatus providerStatus = index->provider(dependency).status;
                if (providerStatus == MibProviderStatus::Missing) unresolved.insert(dependency);
                else if (providerStatus == MibProviderStatus::Ambiguous) ambiguous.insert(dependency);
            }
        }
        summary.unresolved = unresolved.size();
        summary.ambiguous = ambiguous.size();
        for (const MibDependencyFileRecord &record : index->files())
            if (record.lastCheckedUtc > summary.lastCheckedUtc)
                summary.lastCheckedUtc = record.lastCheckedUtc;
        return summary;
    };
    mibLibraryCallbacks.libraryDependencySummary = libraryDependencySummary;
    mibLibraryCallbacks.collectionChanged = [this]() {
        modules->RescanPath();
        QString error;
        modules->RefreshDependencyIndex(&error);
        if (!error.isEmpty()) DiagnosticLogger::log("MIB", error);
    };
    mibLibraryCallbacks.checkDependencies = [this](QString *error) {
        modules->CheckProfileDependencies(QStringLiteral("mib-library"),
            modules->DependencyIndex()->moduleNames(), false, error);
        return !error || error->isEmpty();
    };
    mibLibraryCallbacks.effectivePlan = [this](const MibProfileRecord &profile) {
        return modules->BuildEffectivePlan(profile);
    };
    phaseTimer.restart();
    mibLibrary = new MibLibraryWidget(prefs->DefaultMibPaths(), w.TabW, nullptr,
                                      mibLibraryCallbacks);
    DiagnosticLogger::log("Startup", QStringLiteral("Inventory construction complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    const int legacyModulesTab = w.TabW->indexOf(w.ModulesTab);
    if (legacyModulesTab >= 0) w.TabW->removeTab(legacyModulesTab);
    const int mibsTab = w.TabW->insertTab(1, mibLibrary, tr("MIBs"));
    w.TabW->setTabToolTip(mibsTab,
        tr("Browse the MIB library and choose Automatic or Custom profiles."));
    const auto refreshLibraryDependencyStatus = [this, libraryDependencySummary]() {
        const MibLibraryWidget::DependencySummary summary = libraryDependencySummary();
        if (summary.stale) w.MibLibraryDependencyState->setText(tr("Dependencies need checking"));
        else if (summary.unresolved > 0 || summary.ambiguous > 0)
            w.MibLibraryDependencyState->setText(tr("Dependencies: Has unresolved dependencies"));
        else w.MibLibraryDependencyState->setText(tr("Dependencies: Up to date"));
        QString text = tr("%1 modules · %2 relationships · %3 unresolved · %4 ambiguous")
            .arg(summary.knownModules).arg(summary.relationships)
            .arg(summary.unresolved).arg(summary.ambiguous);
        if (summary.lastCheckedUtc.isValid()) text += tr(" · Last checked: %1")
            .arg(summary.lastCheckedUtc.toLocalTime().toString(Qt::ISODate));
        w.MibLibraryDependencySummary->setText(text);
    };
    refreshLibraryDependencyStatus();
    connect(w.MibLibraryCheckDependencies, &QPushButton::clicked, this,
            [this, refreshLibraryDependencyStatus]() {
        QElapsedTimer dependencyTimer;
        dependencyTimer.start();
        w.MibLibraryCheckDependencies->setEnabled(false);
        w.MibLibraryDependencyState->setText(tr("Dependencies: Checking…"));
        QString error;
        const MibProfileDependencyCheck before = modules->CachedProfileDependencies(
            QStringLiteral("mib-library"), MibDependencyIndex::profileSignature(
                modules->DependencyIndex()->moduleNames(), false));
        MibProfileDependencyCheck after;
        if (error.isEmpty()) after = modules->CheckProfileDependencies(QStringLiteral("mib-library"),
            modules->DependencyIndex()->moduleNames(), false, &error);
        const bool semanticVerificationRan = before.checkedUtc != after.checkedUtc;
        QElapsedTimer restoreTimer;
        restoreTimer.start();
        if (semanticVerificationRan) modules->Refresh();
        const qint64 restoreMs = semanticVerificationRan ? restoreTimer.elapsed() : 0;
        w.MibLibraryCheckDependencies->setEnabled(true);
        if (!error.isEmpty()) {
            w.MibLibraryDependencyState->setText(error);
            return;
        }
        mibLibrary->refresh();
        refreshLibraryDependencyStatus();
        DiagnosticLogger::log("MIB", tr("Check Dependencies UI total-ms=%1 runtime-restore-ms=%2 semantic-ran=%3")
            .arg(dependencyTimer.elapsed()).arg(restoreMs)
            .arg(semanticVerificationRan ? QStringLiteral("yes") : QStringLiteral("no")));
    });
    connect(modules, &MibModule::inventoryChanged, mibLibrary, &MibLibraryWidget::refresh);
    connect(modules, &MibModule::inventoryChanged, this, refreshLibraryDependencyStatus);
    connect(mibLibrary, &MibLibraryWidget::openModuleRequested, mw,
            [this](const QString &path, bool readOnly) {
        if (readOnly) editor->MibFileOpenReadOnly(path); else editor->MibFileOpen(path);
        w.TabW->setCurrentIndex(2);
    });
    auto *browserProfile = new QComboBox(w.MIBTreeLayout);
    browserProfile->setObjectName(QStringLiteral("MibBrowserProfileSelector"));
    auto *browserProfileRow = new QWidget(w.MIBTreeLayout);
    auto *browserProfileLayout = new QHBoxLayout(browserProfileRow);
    browserProfileLayout->setContentsMargins(0, 0, 0, 4);
    browserProfileLayout->addWidget(new QLabel(tr("MIB Profile:"), browserProfileRow));
    browserProfileLayout->addWidget(browserProfile, 1);
    qobject_cast<QVBoxLayout *>(w.MIBTreeLayout->layout())->insertWidget(1, browserProfileRow);
    const auto refreshMibProfiles = [this, browserProfile]() {
        const QString selected = QSettings().value("mib-library/current-profile",
            MibProfileDefinitions::allId()).toString();
        const QSignalBlocker blocker(browserProfile);
        browserProfile->clear();
        for (const MibProfileRecord &profile : mibLibrary->profileService()->profiles())
            browserProfile->addItem(profile.name, profile.id);
        int index = browserProfile->findData(selected);
        if (index < 0) index = browserProfile->findData(MibProfileDefinitions::allId());
        browserProfile->setCurrentIndex(index);
    };
    refreshMibProfiles();
    connect(mibLibrary, &MibLibraryWidget::profilesChanged, mw, refreshMibProfiles);
    connect(browserProfile, &QComboBox::currentIndexChanged, mw, [this, browserProfile]() {
        mibLibrary->selectProfile(browserProfile->currentData().toString());
    });
    connect(mibLibrary, &MibLibraryWidget::profileSelectionChanged, mw,
            [this, browserProfile](const QString &id, const MibEffectivePlan &plan) {
        { const QSignalBlocker blocker(browserProfile);
          const int index = browserProfile->findData(id); if (index >= 0) browserProfile->setCurrentIndex(index); }
        QString error;
        if (!modules->ApplyProfileRuntime(plan, &error)) {
            if (!error.isEmpty())
                DiagnosticLogger::log("MIB", tr("Unable to apply MIB profile: %1").arg(error));
            return;
        }
        if (id == MibProfileDefinitions::allId()) w.MIBTree->showAllModules();
        else w.MIBTree->setVisibleModules(plan.effectiveModules);
        w.MIBTree->Populate();
    });
    phaseTimer.restart(); mibLibrary->selectProfile(browserProfile->currentData().toString());
    initializingMibProfile = false;
    DiagnosticLogger::log("Startup", QStringLiteral("MIB profile initialization complete elapsed_ms=%1").arg(phaseTimer.elapsed()));
    discovery = new Discovery(this);

    auto *contextWidget = new QWidget(w.widget);
    contextWidget->setObjectName(QStringLiteral("CurrentDeviceContext"));
    auto *contextLayout = new QVBoxLayout(contextWidget);
    contextLayout->setContentsMargins(10, 6, 10, 6);
    contextLayout->setSpacing(1);
    auto *contextName = new QLabel(tr("No device selected"), contextWidget);
    contextName->setObjectName(QStringLiteral("CurrentDeviceName"));
    QFont contextFont = contextName->font(); contextFont.setBold(true);
    contextName->setFont(contextFont);
    auto *contextSummary = new QLabel(tr("Select a device from the sidebar"), contextWidget);
    contextSummary->setObjectName(QStringLiteral("CurrentDeviceSummary"));
    contextLayout->addWidget(contextName);
    contextLayout->addWidget(contextSummary);
    if (auto *centralLayout = qobject_cast<QGridLayout *>(w.widget->layout())) {
        centralLayout->removeWidget(w.TabW);
        centralLayout->setContentsMargins(8, 8, 8, 8);
        centralLayout->setSpacing(6);
        centralLayout->addWidget(contextWidget, 0, 0);
        centralLayout->addWidget(w.TabW, 1, 0);
        centralLayout->setRowStretch(1, 1);
    }

    devicesDock = new QDockWidget(tr("Devices"), mw);
    devicesDock->setObjectName("DevicesDock");
    devicesDock->setAllowedAreas(Qt::LeftDockWidgetArea);
    devicesDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
    auto *dockTitle = new QWidget(devicesDock);
    dockTitle->setFixedHeight(0);
    devicesDock->setTitleBarWidget(dockTitle);
    devicePane = new DevicePane(GetDeviceTreeConfigFile(),
                                apm->GetAgentProfileRecords(),
                                profileMetadataService->allMetadata(),
                                profileService, profileMetadataService,
                                communityCredentialService, UsmCredentials(), devicesDock);
    DiagnosticLogger::log("Connections", "Connections model and Device Details constructed");
    devicesDock->setWidget(devicePane);
    auto refreshCredentialHealth = [this]() {
        QHash<QString, QString> health;
        for (const AgentProfileRecord &profile : profileService->profiles())
        {
            QString text;
            if (profile.v3)
            {
                const UsmReferenceResult result = UsmCredentials()->validate(
                    profile, profileMetadataService->metadataForProfile(profile.profileId));
                switch (result.status) {
                case UsmReferenceStatus::Valid: text = tr("SNMPv3 credential available"); break;
                case UsmReferenceStatus::Missing: text = tr("SNMPv3 credential missing"); break;
                case UsmReferenceStatus::Ambiguous: text = tr("SNMPv3 security name ambiguous"); break;
                case UsmReferenceStatus::IncompatibleSecurityLevel:
                    text = tr("SNMPv3 security-level mismatch"); break;
                default: break;
                }
            }
            else if (profile.v1 || profile.v2)
                text = communityCredentialService->healthText(profile);
            health.insert(profile.profileId, text);
        }
        devicePane->setCredentialHealth(health);
    };
    refreshCredentialHealth();
    connect(communityCredentialService, &CommunityCredentialService::credentialsChanged,
            devicePane, refreshCredentialHealth);
    connect(communityCredentialService, &CommunityCredentialService::bindingsChanged,
            devicePane, refreshCredentialHealth);
    connect(profileService, &AgentProfileService::profilesChanged,
            devicePane, refreshCredentialHealth);
    connect(UsmCredentials(), &UsmCredentialService::credentialsChanged,
            devicePane, refreshCredentialHealth);
    devicesDock->setMinimumWidth(280);
    mw->addDockWidget(Qt::LeftDockWidgetArea, devicesDock);
    mw->resizeDocks({devicesDock}, {330}, Qt::Horizontal);

    connect(devicePane, &DevicePane::profileSelected,
            agent, &Agent::SelectProfileById);
    connect(devicePane, &DevicePane::currentContextChanged, mw,
            [contextName, contextSummary](const QString &name, const QString &summary) {
        DiagnosticLogger::log("UI", QStringLiteral(
            "current-profile header update begin name=%1").arg(name));
        contextName->setText(name.isEmpty() ? QObject::tr("No device selected") : name);
        contextSummary->setText(summary.isEmpty() ?
            QObject::tr("Select a device from the sidebar") : summary);
        DiagnosticLogger::log("UI", "current-profile header update end");
    });
    connect(devicePane->detailsEditor(), &DeviceDetailsEditor::profileApplied,
            devicePane, [this](const QString &profileId) {
        DiagnosticLogger::log("Connections", QStringLiteral(
            "model refresh begin profile=%1").arg(profileId));
        devicePane->setProfiles(profileService->profiles());
        DiagnosticLogger::log("Connections", QStringLiteral(
            "tree reselection begin profile=%1").arg(profileId));
        agent->SelectProfileById(profileId);
        DiagnosticLogger::log("Connections", "tree reselection/model refresh end");
    });
    connect(devicePane, &DevicePane::editProfileRequested,
            apm, &AgentProfileManager::EditProfile);
    connect(devicePane, &DevicePane::duplicateProfileRequested,
            apm, [this](const QString &name) { apm->DuplicateProfile(name); });
    connect(devicePane, &DevicePane::newProfileRequested,
            apm, [this](const QString &) { apm->NewProfile(); });
    connect(devicePane, &DevicePane::deleteProfileRequested,
            apm, &AgentProfileManager::DeleteProfile);
    connect(apm, &AgentProfileManager::NewProfileCompleted,
            devicePane, &DevicePane::placeCreatedProfile);
    connect(apm, &AgentProfileManager::NewProfileCompleted, this,
            [this](const QString &profileId) {
        const AgentProfileRecord *profile = profileService->findById(profileId);
        if (!profile) return;
        ProfileMetadataRecord metadata =
            profileMetadataService->metadataForProfile(profileId);
        metadata.hasActiveProtocol = true;
        metadata.activeProtocol = profile->v1 ? 0 : (profile->v2 ? 1 : 2);
        metadata.hasRequestSettingsMode = true;
        metadata.requestSettingsMode = 1;
        profileMetadataService->update(metadata);
    });
    connect(devicePane, &DevicePane::organizationPersisted,
            apm, &AgentProfileManager::PersistProfiles);
    connect(devicePane, &DevicePane::organizationPersisted,
            discovery, &Discovery::RefreshDestinationFolders);
    connect(devicePlacementService, &DeviceTreePlacementService::placementChanged,
            devicePane, [this](const QString &) { devicePane->reloadTree(); });
    connect(apm, &AgentProfileManager::AgentProfileListChanged,
            devicePane, [this]() {
                devicePane->setProfiles(apm->GetAgentProfileRecords());
            });
    connect(apm, &AgentProfileManager::AgentProfileRenamed,
            devicePane, [this](const QString &profileId, const QString &,
                               const QString &newName) {
                devicePane->renameProfile(profileId, newName);
            });
    connect(apm, &AgentProfileManager::AgentProfileDuplicated,
            devicePane, &DevicePane::placeDuplicate);
    connect(profileService, &AgentProfileService::profileDuplicated,
            profileMetadataService, &ProfileMetadataService::copy);
    connect(profileService, &AgentProfileService::profileDuplicated,
            communityCredentialService, &CommunityCredentialService::copyBinding);
    connect(profileService, &AgentProfileService::profileDeleted,
            profileMetadataService, &ProfileMetadataService::remove);
    connect(profileService, &AgentProfileService::profileDeleted,
            communityCredentialService,
            &CommunityCredentialService::removeProfileBinding);
    connect(profileMetadataService, &ProfileMetadataService::metadataChanged,
            devicePane, [this](const QString &) {
                devicePane->setMetadata(profileMetadataService->allMetadata());
            });
    connect(devicePane, &DevicePane::loadPreferredMibsRequested, this,
            [this](const QString &profileId) {
        modules->LoadPreferredModules(
            profileMetadataService->metadataForProfile(profileId).preferredMibs);
    });
    connect(devicePane, &DevicePane::manageUsmCredentialsRequested, this, [this]() {
        if (upm) upm->ExecuteNewCredential();
    });
    connect(devicePane, &DevicePane::exportRequested, this,
            [this](int scope, const QString &id) {
        ProfileTransferDocument document;
        document.profiles = profileService->profiles();
        document.metadata = profileMetadataService->allMetadata();
        document.tree = devicePane->model()->state();
        if (scope == 1)
            document = ProfileTransfer::selectProfiles(document, {id});
        else if (scope == 2)
            document = ProfileTransfer::selectFolder(document, id);
        const QString fileName = QFileDialog::getSaveFileName(
            nullptr, tr("Export Device Profiles"), QString(), tr("JSON files (*.json)"));
        if (fileName.isEmpty()) return;
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(ProfileTransfer::exportJson(document)) < 0)
            QMessageBox::warning(nullptr, tr("Export Failed"),
                                 tr("The profile export could not be written."));
    });
    connect(devicePane, &DevicePane::importRequested, this, [this]() {
        const QString fileName = QFileDialog::getOpenFileName(
            nullptr, tr("Import Device Profiles"), QString(), tr("JSON files (*.json)"));
        if (fileName.isEmpty()) return;
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly))
        {
            QMessageBox::warning(nullptr, tr("Import Failed"),
                                 tr("The selected import file could not be read."));
            return;
        }
        ProfileImportPlan plan;
        QString detail;
        const ProfileTransferError error = ProfileTransfer::planImport(
            file.readAll(), profileService->profiles(), devicePane->model()->state(),
            &plan, &detail);
        if (error != ProfileTransferError::None)
        {
            QMessageBox::warning(nullptr, tr("Import Failed"),
                                 tr("The import file is invalid or unsupported. No profiles were imported."));
            return;
        }
        QString applyError;
        if (!ProfileImportStorage::apply(plan, GetAgentsConfigFile(),
                                         GetProfileMetadataConfigFile(),
                                         GetDeviceTreeConfigFile(),
                                         profileService->profiles(),
                                         profileMetadataService->allMetadata(),
                                         devicePane->model()->state(), &applyError))
        {
            QMessageBox::warning(nullptr, tr("Import Failed"),
                                 tr("The import could not be saved. Existing configuration was restored."));
            return;
        }
        profileService->reload();
        profileMetadataService->reload();
        devicePane->setProfiles(profileService->profiles());
        devicePane->setMetadata(profileMetadataService->allMetadata());
        devicePane->reloadTree();
    });

    QSettings windowSettings;
    RestoreMainWindowStateWithRequiredDevicesDock(
        mw, devicesDock, windowSettings.value("mainwindow/state").toByteArray());
    DiagnosticLogger::log("UI", "saved QMainWindow state restore complete");
    if (windowSettings.contains("mainwindow/device-sidebar-sizes"))
        devicePane->verticalSplitter()->restoreState(
            windowSettings.value("mainwindow/device-sidebar-sizes").toByteArray());
    connect(QApplication::instance(), &QCoreApplication::aboutToQuit,
            mw, [this, mw]() {
                QSettings settings;
                settings.setValue("mainwindow/state", mw->saveState());
                settings.setValue("mainwindow/device-sidebar-sizes",
                                  devicePane->verticalSplitter()->saveState());
            });

    // Connect some signals
    connect( w.TabW, SIGNAL( currentChanged(int) ),
             this, SLOT( TabSelected() ) );
    connect( w.actionManageAgentProfiles, SIGNAL( triggered(bool) ),
             this, SLOT( ManageAgentProfiles(bool) ) );
    connect( w.actionManageUSMProfiles, SIGNAL( triggered(bool) ),
             this, SLOT( ManageUSMProfiles(bool) ) );
    QAction *manageCommunities = w.optionsMenu->addAction(
        tr("Manage SNMPv1/v2c Community Credentials..."));
    connect(manageCommunities, &QAction::triggered, mw, [this, mw]() {
        CommunityCredentialManager manager(communityCredentialService,
                                           profileService, mw);
        manager.execute();
    });
    connect( w.actionPreferences, SIGNAL( triggered(bool) ),
             this, SLOT( ManagePreferences(bool) ) );
    connect( w.helpAboutAction, SIGNAL( triggered(bool) ),
             this, SLOT( AboutBox(bool) ) );

    // Register every MIB tree to the MIB loader object
    w.MIBTree->RegisterToLoader(&loader);
    TabSelected();
}

Ui_MainW* Snmpb::MainUI(void)
{
    return (&w);
}

Agent* Snmpb::AgentObj(void)
{
    return (agent);
}

Trap* Snmpb::TrapObj(void)
{
    return (trap);
}

MibViewLoader* Snmpb::MibLoaderObj(void)
{
    return (&loader);
}

MibModule* Snmpb::MibModuleObj(void)
{
    return (modules);
}

MibEditor* Snmpb::MibEditorObj(void)
{
    return (editor);
}

AgentProfileManager* Snmpb::APManagerObj(void)
{
    return (apm);
}

void Snmpb::Shutdown()
{
    DiagnosticLogger::log("Shutdown", "trap listener shutdown begin", false);
    if (agent) agent->Shutdown();
    DiagnosticLogger::log("Shutdown", "trap listener shutdown end", false);
}

AgentProfileService* Snmpb::AgentProfiles(void)
{
    return profileService;
}

ProfileMetadataService* Snmpb::ProfileMetadata(void)
{
    return profileMetadataService;
}

DeviceTreePlacementService* Snmpb::DevicePlacements(void)
{
    return devicePlacementService;
}

USMProfileManager* Snmpb::UPManagerObj(void)
{
    return (upm);
}

UsmCredentialService* Snmpb::UsmCredentials(void)
{
    return upm ? upm->Credentials() : nullptr;
}

CommunityCredentialService* Snmpb::CommunityCredentials(void)
{
    return communityCredentialService;
}

Preferences* Snmpb::PreferencesObj(void)
{
    return (prefs);
}

void Snmpb::CheckForConfigFiles(void)
{
    QSettings settings;

    if (!settings.isWritable())
    {
        QMessageBox::warning(nullptr, tr("MIB Navigator"),
                             tr("MIB Navigator config file is not writable:\n%1\n"
                                "If it continues to be, changes in preferences will not be saved!")
                             .arg(settings.fileName()),
                             QMessageBox::Ok);
    }
}

QString Snmpb::GetBootCounterConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(BOOT_COUNTER_CONFIG_FILE);
}

QString Snmpb::GetSmiConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(SMI_CONFIG_FILE);
}

QString Snmpb::GetUsmUsersConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(USM_USERS_CONFIG_FILE);
}

QString Snmpb::GetAgentsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(AGENTS_CONFIG_FILE);
}

QString Snmpb::GetLogConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(LOG_CONFIG_FILE);
}

QString Snmpb::GetGraphsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(GRAPHS_CONFIG_FILE);
}

void Snmpb::ManageAgentProfiles(bool)
{
    apm->Execute();
}

void Snmpb::ManageUSMProfiles(bool)
{
    upm->Execute();
}

void Snmpb::ManagePreferences(bool)
{
    prefs->Execute();
}

void Snmpb::SetEditorMenus(bool value)
{
    MainUI()->fileNewAction->setEnabled(value);
    MainUI()->fileOpenAction->setEnabled(value);
    MainUI()->fileSaveAction->setEnabled(value);
    MainUI()->fileSaveAsAction->setEnabled(value);
    MainUI()->actionVerifyMIB->setEnabled(value);
    MainUI()->actionExtractMIBfromRFC->setEnabled(value);
    MainUI()->actionGotoLine->setEnabled(value);
    MainUI()->actionFind->setEnabled(value);
    MainUI()->actionReplace->setEnabled(value);
    MainUI()->actionFindNext->setEnabled(value);
}

/* 
 * This is where anything related to a tab being selected happens:
 * graying-out GUI parts, refreshing MIB trees, ...
 */
void Snmpb::TabSelected(void)
{
    QWidget *currentTab = w.TabW->currentWidget();
    if (currentTab == w.TreeTab) {
        SetEditorMenus(false);
        // Set find func to MIB tree
        MainUI()->actionFind->setEnabled(true);
        MainUI()->actionFindNext->setEnabled(true);
        disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
        disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
        connect( MainUI()->actionFind, SIGNAL( triggered() ),
                w.MIBTree, SLOT( FindFromNode() ) );
        connect( MainUI()->actionFindNext, SIGNAL( triggered() ),
                w.MIBTree, SLOT( ExecuteFindNext() ) );
        MainUI()->actionMultipleVarbinds->setEnabled(true);
        // Refresh MIB tree if needed
        w.MIBTree->Populate();
    } else if (currentTab == mibLibrary) {
        QElapsedTimer activationTimer; activationTimer.start();
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
        mibLibrary->activate();
        DiagnosticLogger::log("UI", tr("MIBs workspace activation total-ms=%1")
            .arg(activationTimer.elapsed()));
    } else if (currentTab == w.EditorTab) {
        SetEditorMenus(true);
        // Set find func to MIB editor 
        disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
        disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
        connect( MainUI()->actionFind, SIGNAL( triggered() ),
                editor, SLOT( Find() ) );
        connect( MainUI()->actionFindNext, SIGNAL( triggered() ),
                editor, SLOT( ExecuteFindNext() ) );
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.DiscoveryTab) {
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.TrapsTab) {
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.GraphsTab) {
        SetEditorMenus(false);
        disconnect(MainUI()->actionFind, SIGNAL( triggered() ), 0, 0);
        disconnect(MainUI()->actionFindNext, SIGNAL( triggered() ), 0, 0);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    } else if (currentTab == w.LogTab) {
        SetEditorMenus(false);
        MainUI()->actionMultipleVarbinds->setEnabled(false);
    }
}

void Snmpb::AboutBox(bool)
{
    QString dependencies = QStringLiteral(
        "SNMP++ %1; LibTomCrypt %2; libsmi %3; Qt %4")
        .arg(SNMP_PP_VERSION_STRING)
        .arg(SCRYPT)
        .arg(SMI_VERSION_STRING)
        .arg(qVersion());
#if SNMPB_ENABLE_QWT
    dependencies += QStringLiteral("; Qwt %1").arg(QWT_VERSION_STR);
#endif
    QMessageBox::about(MainUI()->TabW, tr("About MIB Navigator"),
                       ProductIdentity::aboutHtml(SNMPB_VERSION_STRING,
                                                  dependencies));
}

QString Snmpb::GetDeviceTreeConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(DEVICE_TREE_CONFIG_FILE);
}

QString Snmpb::GetProfileMetadataConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(PROFILE_METADATA_CONFIG_FILE);
}

QString Snmpb::GetCredentialIdentitiesConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(CREDENTIAL_IDENTITIES_CONFIG_FILE);
}

QString Snmpb::GetCommunityCredentialsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(COMMUNITY_CREDENTIALS_CONFIG_FILE);
}

QString Snmpb::GetCredentialBindingsConfigFile(void)
{
    QSettings settings;
    QDir cfgdir = QFileInfo(settings.fileName()).dir();
    return cfgdir.filePath(CREDENTIAL_BINDINGS_CONFIG_FILE);
}

