#include "devicedetailseditor.h"

#include "agentprofileservice.h"
#include "communitycredentialservice.h"
#include "profilemetadataservice.h"
#include "usmcredentialservice.h"
#include "connectionrequestsettings.h"
#include "preferencesettings.h"
#include "diagnosticlogger.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
QString protocolName(int value)
{
    switch (value) {
    case 1: return QObject::tr("HMAC-MD5");
    case 2: return QObject::tr("HMAC-SHA");
    case 3: return QObject::tr("HMAC-SHA-224");
    case 4: return QObject::tr("HMAC-SHA-256");
    case 5: return QObject::tr("HMAC-SHA-384");
    case 6: return QObject::tr("HMAC-SHA-512");
    default: return QObject::tr("None");
    }
}
QString privacyName(int value)
{
    switch (value) {
    case 1: return QObject::tr("DES");
    case 2: return QObject::tr("IDEA");
    case 3: return QObject::tr("AES-128");
    case 4: return QObject::tr("AES-192");
    case 5: return QObject::tr("AES-256");
    case 6: return QObject::tr("3DES");
    default: return QObject::tr("None");
    }
}
}

DeviceDetailsEditor::DeviceDetailsEditor(
    AgentProfileService *profiles, ProfileMetadataService *metadata,
    CommunityCredentialService *communities, UsmCredentialService *usm,
    QWidget *parent)
    : QWidget(parent), profileService(profiles), metadataService(metadata),
      communityService(communities), usmService(usm)
{
    setObjectName(QStringLiteral("DeviceDetailsEditor"));
    buildUi();
    clearSelection();
}

void DeviceDetailsEditor::buildUi()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 8, 10, 10);
    outer->setSpacing(8);
    heading = new QLabel(tr("CONNECTION DETAILS"), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);
    outer->addWidget(heading);

    selectionPages = new QStackedWidget(this);
    emptyPage = new QWidget(selectionPages);
    auto *emptyLayout = new QVBoxLayout(emptyPage);
    auto *emptyText = new QLabel(tr("Select a device or folder to view its details."), emptyPage);
    emptyText->setWordWrap(true);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addStretch();

    profilePage = new QWidget(selectionPages);
    auto *profileLayout = new QVBoxLayout(profilePage);
    profileLayout->setContentsMargins(0, 0, 0, 0);
    profileLayout->setSpacing(8);
    auto *general = new QGridLayout;
    general->setColumnStretch(1, 1);
    name = new QLineEdit(profilePage); name->setObjectName("DeviceNameEditor");
    host = new QLineEdit(profilePage); host->setObjectName("DeviceHostEditor");
    auto *nameLabel = new QLabel(tr("Name"), profilePage);
    nameLabel->setObjectName("DeviceNameLabel");
    nameLabel->setBuddy(name);
    auto *hostLabel = new QLabel(tr("Host / IP"), profilePage);
    hostLabel->setObjectName("DeviceHostLabel");
    hostLabel->setBuddy(host);
    general->addWidget(nameLabel, 0, 0);
    general->addWidget(name, 0, 1);
    general->addWidget(hostLabel, 1, 0);
    general->addWidget(host, 1, 1);
    profileLayout->addLayout(general);

    auto *versions = new QGroupBox(tr("SNMP"), profilePage);
    auto *versionLayout = new QVBoxLayout(versions);
    protocol = new QComboBox(versions); protocol->setObjectName("DeviceProtocolEditor");
    protocol->addItems({tr("SNMPv1"), tr("SNMPv2c"), tr("SNMPv3")});
    auto *activeForm = new QFormLayout;
    activeForm->addRow(tr("Active SNMP Version"), protocol);
    versionLayout->addLayout(activeForm);
    protocolTabs = new QTabWidget(versions);
    protocolTabs->setObjectName("ProtocolConfigurationTabs");
    protocolStack = new QStackedWidget(versions); // retained test/access boundary
    auto *communityPage = new QWidget(protocolTabs);
    communityPage->setObjectName("SnmpV1V2cConfiguration");
    auto *communityForm = new QFormLayout(communityPage);
    credentialSource = new QComboBox(communityPage);
    credentialSource->setObjectName("CommunityCredentialSource");
    credentialSource->addItems({tr("Inline"), tr("Reusable credential")});
    communityCredential = new QComboBox(communityPage);
    communityCredential->setObjectName("CommunityCredentialSelection");
    readCommunityStatus = new QLabel(communityPage);
    writeCommunityStatus = new QLabel(communityPage);
    communityForm->addRow(tr("Credential source"), credentialSource);
    communityForm->addRow(tr("Credential"), communityCredential);
    communityForm->addRow(tr("Read community"), readCommunityStatus);
    communityForm->addRow(tr("Write community"), writeCommunityStatus);
    auto *v3Page = new QWidget(protocolTabs);
    auto *v3Form = new QFormLayout(v3Page);
    securityName = new QLineEdit(v3Page); securityName->setReadOnly(true);
    securityLevel = new QLabel(v3Page);
    usmCredential = new QComboBox(v3Page);
    authProtocol = new QLabel(v3Page);
    privacyProtocol = new QLabel(v3Page);
    v3Form->addRow(tr("USM Credential"), usmCredential);
    v3Form->addRow(tr("Security Name"), securityName);
    v3Form->addRow(tr("Security Level"), securityLevel);
    v3Form->addRow(tr("Authentication Protocol"), authProtocol);
    v3Form->addRow(tr("Privacy Protocol"), privacyProtocol);
    protocolTabs->addTab(communityPage, tr("SNMPv1/v2c"));
    protocolTabs->addTab(v3Page, tr("SNMPv3"));
    versionLayout->addWidget(protocolTabs);
    profileLayout->addWidget(versions);

    auto *metadataForm = new QFormLayout;
    tags = new QLineEdit(profilePage); tags->setObjectName("DeviceTagsEditor");
    tags->setPlaceholderText(tr("comma-separated tags"));
    notes = new QPlainTextEdit(profilePage);
    notes->setMaximumHeight(72);
    preferredMibs = new QLabel(profilePage);
    editMibs = new QPushButton(tr("Edit..."), profilePage);
    metadataForm->addRow(tr("Tags"), tags);
    metadataForm->addRow(tr("Notes"), notes);
    // Retain the legacy preferred-MIB fields for persistence compatibility,
    // but Profiles are now the only user-facing MIB selection authority.
    preferredMibs->hide();
    editMibs->hide();
    profileLayout->addLayout(metadataForm);
    settingsEditAction = new QPushButton(tr("Edit..."), profilePage);
    settingsEditAction->setObjectName("EditConnectionSettings");
    advancedAction = new QPushButton(tr("Advanced..."), profilePage);
    advancedAction->setObjectName("AdvancedConnectionSettings");
    auto *settingsRow = new QHBoxLayout;
    settingsRow->addWidget(new QLabel(tr("Settings:"), profilePage));
    settingsRow->addWidget(settingsEditAction);
    settingsRow->addWidget(advancedAction);
    settingsRow->addStretch();
    profileLayout->addLayout(settingsRow);
    profileLayout->addStretch();

    folderPage = new QWidget(selectionPages);
    auto *folderLayout = new QFormLayout(folderPage);
    folderNameEdit = new QLineEdit(folderPage);
    folderCount = new QLabel(folderPage);
    folderLayout->addRow(tr("Folder name"), folderNameEdit);
    folderLayout->addRow(tr("Contained devices"), folderCount);

    selectionPages->addWidget(emptyPage);
    selectionPages->addWidget(profilePage);
    selectionPages->addWidget(folderPage);
    outer->addWidget(selectionPages, 1);
    auto *actions = new QHBoxLayout;
    actions->addStretch();
    applyAction = new QPushButton(tr("Apply"), this);
    applyAction->setObjectName("DeviceDetailsApply");
    revertAction = new QPushButton(tr("Revert"), this);
    revertAction->setObjectName("DeviceDetailsRevert");
    actions->addWidget(applyAction); actions->addWidget(revertAction);
    outer->addLayout(actions);

    const auto markDirty = [this]() { if (!loading) setDirty(true); };
    for (QLineEdit *edit : {name, host, securityName, tags, folderNameEdit})
        connect(edit, &QLineEdit::textEdited, this, markDirty);
    connect(notes, &QPlainTextEdit::textChanged, this, markDirty);
    connect(protocol, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!loading) { activeProtocolExplicitlyChanged = true; setDirty(true); }
    });
    connect(credentialSource, &QComboBox::currentIndexChanged, this, [this](int) {
        communityCredential->setEnabled(credentialSource->currentIndex() == 1);
        if (!loading) setDirty(true);
    });
    connect(communityCredential, &QComboBox::currentIndexChanged, this,
            [markDirty](int) { markDirty(); });
    connect(usmCredential, &QComboBox::currentIndexChanged, this, [this, markDirty](int) {
        const int index = usmCredential->currentIndex();
        if (index >= 0 && usmCredential->currentData().toString() ==
                QStringLiteral("__new_usm_credential__")) {
            selectNewUsmCredential();
            return;
        }
        if (index >= 0 && usmService) {
            const QString id = usmCredential->currentData().toString();
            for (const UsmCredentialRecord &record : usmService->records())
                if (record.identity.credentialId == id) {
                    selectedUsmCredentialId = id;
                    securityName->setText(record.securityName);
                    const int level = UsmCredentialService::securityLevel(record);
                    securityLevel->setText(level == 2 ? tr("AuthPriv") :
                        (level == 1 ? tr("AuthNoPriv") : tr("noAuth/noPriv")));
                    authProtocol->setText(protocolName(record.authProtocol));
                    privacyProtocol->setText(privacyName(record.privacyProtocol));
                    break;
                }
        }
        if (!loading) usmCredentialExplicitlyChanged = true;
        markDirty();
    });
    connect(applyAction, &QPushButton::clicked, this, &DeviceDetailsEditor::apply);
    connect(revertAction, &QPushButton::clicked, this, &DeviceDetailsEditor::revert);
    connect(editMibs, &QPushButton::clicked, this, [this]() {
        if (!workingProfile.profileId.isEmpty())
            emit editPreferredMibsRequested(workingProfile.profileId);
    });
    connect(advancedAction, &QPushButton::clicked,
            this, &DeviceDetailsEditor::showAdvancedSettings);
    connect(settingsEditAction, &QPushButton::clicked, this, [this]() {
        if (!workingProfile.profileId.isEmpty())
            emit editConnectionSettingsRequested(workingProfile.profileId);
    });
}

bool DeviceDetailsEditor::isDirty() const { return dirty; }
QString DeviceDetailsEditor::currentProfileId() const { return workingProfile.profileId; }
QString DeviceDetailsEditor::currentFolderId() const { return folderId; }
QString DeviceDetailsEditor::contextSummary() const
{
    if (workingProfile.profileId.isEmpty()) return {};
    const int active = workingMetadata.hasActiveProtocol ?
        workingMetadata.activeProtocol : legacyProtocol;
    QString version = active == 2 ? QStringLiteral("SNMPv3") :
        (active == 1 ? QStringLiteral("SNMPv2c") : QStringLiteral("SNMPv1"));
    if (active == 2) {
        const QString level = workingProfile.seclevel == 2 ? QStringLiteral("AuthPriv") :
            (workingProfile.seclevel == 1 ? QStringLiteral("AuthNoPriv") : QStringLiteral("noAuth/noPriv"));
        version += QStringLiteral(" %1").arg(level);
    }
    return QStringLiteral("%1:%2  |  %3")
        .arg(workingProfile.address, workingProfile.port, version);
}
QComboBox *DeviceDetailsEditor::protocolEditor() const { return protocol; }
QStackedWidget *DeviceDetailsEditor::protocolPages() const { return protocolStack; }
QTabWidget *DeviceDetailsEditor::configurationTabs() const { return protocolTabs; }
QLineEdit *DeviceDetailsEditor::nameEditor() const { return name; }
QLineEdit *DeviceDetailsEditor::hostEditor() const { return host; }
QLineEdit *DeviceDetailsEditor::tagsEditor() const { return tags; }
QPushButton *DeviceDetailsEditor::applyButton() const { return applyAction; }
QPushButton *DeviceDetailsEditor::revertButton() const { return revertAction; }
QComboBox *DeviceDetailsEditor::usmCredentialEditor() const { return usmCredential; }

void DeviceDetailsEditor::showProfile(const QString &id)
{
    if (!profileService) return;
    const AgentProfileRecord *record = profileService->findById(id);
    if (!record) { clearSelection(); return; }
    workingProfile = *record;
    workingMetadata = metadataService ? metadataService->metadataForProfile(id)
                                      : ProfileMetadataRecord{id, {}, {}, {}};
    folderId.clear();
    QSettings settings;
    legacyProtocol = PreferencesSettings::load(settings).selectedProtocol;
    loadProfileControls();
    selectionPages->setCurrentWidget(profilePage);
    heading->setText(tr("CONNECTION DETAILS"));
    applyAction->show(); revertAction->show();
    setDirty(false);
}

void DeviceDetailsEditor::loadProfileControls()
{
    loading = true;
    name->setText(workingProfile.name);
    host->setText(workingProfile.address);
    protocol->setCurrentIndex(ConnectionRequestSettings::activeProtocol(
        workingProfile, workingMetadata, legacyProtocol));
    activeProtocolExplicitlyChanged = false;
    usmCredentialExplicitlyChanged = false;
    securityName->setText(workingProfile.secname);
    securityLevel->setText(workingProfile.seclevel == 2 ? tr("AuthPriv") :
        (workingProfile.seclevel == 1 ? tr("AuthNoPriv") : tr("noAuth/noPriv")));
    tags->setText(workingMetadata.tags.join(QStringLiteral(", ")));
    notes->setPlainText(workingMetadata.notes);
    preferredMibs->setText(tr("%n selected", nullptr, workingMetadata.preferredMibs.size()));
    readCommunityStatus->setText(workingProfile.readcomm.isEmpty() ? tr("Not configured") : tr("Configured inline"));
    writeCommunityStatus->setText(workingProfile.writecomm.isEmpty() ? tr("Not configured") : tr("Configured inline"));
    refreshCredentialChoices();
    refreshProtocolPage();
    loading = false;
}

void DeviceDetailsEditor::refreshCredentialChoices()
{
    communityCredential->clear();
    if (communityService) {
        for (const CommunityCredentialRecord &record : communityService->records()) {
            communityCredential->addItem(record.displayName, record.identity.credentialId);
        }
        const QString binding = communityService->binding(workingProfile.profileId);
        const int index = communityCredential->findData(binding);
        credentialSource->setCurrentIndex(index >= 0 ? 1 : 0);
        communityCredential->setCurrentIndex(index);
        communityCredential->setEnabled(index >= 0);
        if (index >= 0) {
            readCommunityStatus->setText(tr("Provided by reusable credential"));
            writeCommunityStatus->setText(tr("Provided by reusable credential"));
        }
    }
    usmCredential->clear();
    if (usmService) {
        for (const UsmCredentialRecord &record : usmService->records())
            usmCredential->addItem(QStringLiteral("%1 — %2").arg(
                record.displayName, record.securityName), record.identity.credentialId);
        int index = workingMetadata.usmCredentialId.isEmpty() ? -1 :
            usmCredential->findData(workingMetadata.usmCredentialId);
        if (index < 0 && workingMetadata.usmCredentialId.isEmpty())
            for (int i = 0; i < usmService->records().size(); ++i)
                if (usmService->records()[i].securityName == workingProfile.secname &&
                    usmService->isSecurityNameUnambiguous(workingProfile.secname))
                    index = i;
        usmCredential->insertSeparator(usmCredential->count());
        usmCredential->addItem(tr("<New...>"),
                               QStringLiteral("__new_usm_credential__"));
        usmCredential->setCurrentIndex(index);
        selectedUsmCredentialId = index >= 0 ?
            usmCredential->itemData(index).toString() : QString();
        if (index < 0) {
            securityName->setText(tr("Selection required"));
            securityLevel->setText(tr("Selection required"));
            authProtocol->setText(tr("Selection required"));
            privacyProtocol->setText(tr("Selection required"));
        }
    }
}

void DeviceDetailsEditor::selectNewUsmCredential()
{
    const QString previousId = selectedUsmCredentialId;
    QStringList before;
    if (usmService)
        for (const UsmCredentialRecord &record : usmService->records())
            before.append(record.identity.credentialId);
    emit manageUsmCredentialsRequested();
    refreshCredentialChoices();
    QString createdId;
    if (usmService)
        for (const UsmCredentialRecord &record : usmService->records())
            if (!before.contains(record.identity.credentialId)) {
                createdId = record.identity.credentialId;
                break;
            }
    usmCredential->setCurrentIndex(usmCredential->findData(
        createdId.isEmpty() ? previousId : createdId));
    if (!createdId.isEmpty()) {
        usmCredentialExplicitlyChanged = true;
        setDirty(true);
    }
}

void DeviceDetailsEditor::refreshProtocolPage()
{
    // Configuration tabs are intentionally independent of active protocol.
}

void DeviceDetailsEditor::showFolder(const QString &id, const QString &value,
                                     int deviceCount)
{
    workingProfile = {};
    folderId = id; folderName = value; folderDevices = deviceCount;
    loading = true;
    folderNameEdit->setText(value);
    folderCount->setText(QString::number(deviceCount));
    loading = false;
    selectionPages->setCurrentWidget(folderPage);
    heading->setText(tr("FOLDER DETAILS"));
    applyAction->show(); revertAction->show();
    setDirty(false);
}

void DeviceDetailsEditor::clearSelection()
{
    workingProfile = {}; workingMetadata = {}; folderId.clear();
    selectionPages->setCurrentWidget(emptyPage);
    heading->setText(tr("CONNECTION DETAILS"));
    applyAction->hide(); revertAction->hide();
    setDirty(false);
}

bool DeviceDetailsEditor::apply()
{
    if (!folderId.isEmpty()) {
        const QString value = folderNameEdit->text().trimmed();
        if (value.isEmpty()) return false;
        emit folderRenameRequested(folderId, value);
        folderName = value;
        setDirty(false);
        return true;
    }
    if (workingProfile.profileId.isEmpty() || !profileService) return false;
    DiagnosticLogger::log("Connections", QStringLiteral("profile validation begin id=%1")
                          .arg(workingProfile.profileId));
    AgentProfileRecord updated = workingProfile;
    updated.name = name->text().trimmed();
    updated.address = host->text().trimmed();
    if (protocol->currentIndex() == 0) updated.v1 = true;
    if (protocol->currentIndex() == 1) updated.v2 = true;
    if (protocol->currentIndex() == 2) updated.v3 = true;
    const UsmCredentialRecord *selectedUsm = usmService ?
        usmService->find(usmCredential->currentData().toString()) : nullptr;
    if (selectedUsm) {
        updated.secname = selectedUsm->securityName;
        updated.seclevel = UsmCredentialService::securityLevel(*selectedUsm);
    }
    if (updated.name.isEmpty() || updated.address.isEmpty() || updated.port.isEmpty() ||
        (!updated.v1 && !updated.v2 && !updated.v3) ||
        (protocol->currentIndex() == 2 &&
         (!selectedUsm || selectedUsm->securityName.trimmed().isEmpty())))
        { DiagnosticLogger::log("Connections", QStringLiteral(
              "profile validation failed id=%1").arg(workingProfile.profileId)); return false; }
    DiagnosticLogger::log("Connections", QStringLiteral("profile validation end id=%1")
                          .arg(workingProfile.profileId));
    ProfileMetadataRecord metadata = workingMetadata;
    if (activeProtocolExplicitlyChanged) {
        metadata.hasActiveProtocol = true;
        metadata.activeProtocol = protocol->currentIndex();
    }
    if (usmCredentialExplicitlyChanged) {
        metadata.usmCredentialId = usmCredential->currentData().toString();
        if (const UsmCredentialRecord *record = usmService ?
                usmService->find(metadata.usmCredentialId) : nullptr)
            updated.secname = record->securityName;
    }
    QSettings settings;
    const AgentProfileRecord effective = ConnectionRequestSettings::effectiveProfile(
        updated, metadata, PreferencesSettings::load(settings));
    updated.timeout = effective.timeout;
    updated.retries = effective.retries;
    updated.nonrepeaters = effective.nonrepeaters;
    updated.maxrepetitions = effective.maxrepetitions;
    setDirty(false);
    DiagnosticLogger::log("Connections", QStringLiteral(
        "AgentProfileService save begin id=%1").arg(updated.profileId));
    if (!profileService->update(updated)) {
        DiagnosticLogger::log("Error", QStringLiteral(
            "AgentProfileService save failed id=%1").arg(updated.profileId));
        setDirty(true); return false;
    }
    DiagnosticLogger::log("Connections", QStringLiteral(
        "AgentProfileService save end id=%1").arg(updated.profileId));
    if (communityService) {
        DiagnosticLogger::log("Credentials", QStringLiteral(
            "community credential resolution begin profile=%1 source=%2")
            .arg(updated.profileId).arg(credentialSource->currentIndex() == 1 ?
                 QStringLiteral("reusable") : QStringLiteral("inline")));
        if (credentialSource->currentIndex() == 1 && communityCredential->currentIndex() >= 0)
            communityService->bind(updated.profileId, communityCredential->currentData().toString());
        else
            communityService->unbind(updated.profileId);
        DiagnosticLogger::log("Credentials", QStringLiteral(
            "community credential resolution end profile=%1").arg(updated.profileId));
    }
    metadata.notes = notes->toPlainText();
    metadata.tags = tags->text().split(',', Qt::SkipEmptyParts);
    DiagnosticLogger::log("Connections", QStringLiteral("metadata save begin id=%1")
                          .arg(updated.profileId));
    if (metadataService && !metadataService->update(metadata)) {
        DiagnosticLogger::log("Error", QStringLiteral("metadata save failed id=%1")
                              .arg(updated.profileId));
        setDirty(true); return false;
    }
    DiagnosticLogger::log("Connections", QStringLiteral("metadata save end id=%1")
                          .arg(updated.profileId));
    workingProfile = updated;
    workingMetadata = metadataService ? metadataService->metadataForProfile(updated.profileId) : metadata;
    setDirty(false);
    emit profileApplied(updated.profileId);
    return true;
}

void DeviceDetailsEditor::showAdvancedSettings()
{
    if (workingProfile.profileId.isEmpty()) return;
    QSettings settings;
    const PreferencesSettings defaults = PreferencesSettings::load(settings);
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Advanced Connection Settings"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *legacy = new QLabel(&dialog);
    legacy->setText(workingMetadata.hasRequestSettingsMode ? QString() :
        tr("Current behavior: Legacy per-profile settings"));
    layout->addWidget(legacy);
    auto *mode = new QComboBox(&dialog);
    mode->addItem(tr("Legacy per-profile settings"), 0);
    mode->addItem(tr("Use global defaults"), 1);
    mode->addItem(tr("Override for this connection"), 2);
    mode->setCurrentIndex(workingMetadata.hasRequestSettingsMode ?
        qBound(1, workingMetadata.requestSettingsMode, 2) : 0);
    layout->addWidget(mode);
    auto *form = new QFormLayout;
    auto *portEdit = new QLineEdit(workingProfile.port, &dialog);
    portEdit->setObjectName("AdvancedSnmpPort");
    form->addRow(tr("SNMP Port"), portEdit);
    auto makeSpin = [&dialog](int maximum) {
        auto *spin = new QSpinBox(&dialog); spin->setRange(0, maximum); return spin;
    };
    auto *timeout = makeSpin(3600); auto *retries = makeSpin(100);
    auto *nonRepeaters = makeSpin(65535); auto *maxRepetitions = makeSpin(65535);
    timeout->setValue(workingMetadata.overrideTimeout);
    retries->setValue(workingMetadata.overrideRetries);
    nonRepeaters->setValue(workingMetadata.overrideBulkNonRepeaters);
    maxRepetitions->setValue(workingMetadata.overrideBulkMaxRepetitions);
    form->addRow(tr("Timeout"), timeout); form->addRow(tr("Retries"), retries);
    form->addRow(tr("GETBULK non-repeaters"), nonRepeaters);
    form->addRow(tr("GETBULK max repetitions"), maxRepetitions);
    layout->addLayout(form);
    auto *restore = new QPushButton(tr("Restore Global Defaults"), &dialog);
    layout->addWidget(restore);
    auto updateEnabled = [=](int index) {
        const bool enabled = index == 2;
        for (QSpinBox *spin : {timeout, retries, nonRepeaters, maxRepetitions})
            spin->setEnabled(enabled);
    };
    connect(mode, &QComboBox::currentIndexChanged, &dialog, updateEnabled);
    connect(restore, &QPushButton::clicked, &dialog, [=]() {
        mode->setCurrentIndex(1);
        timeout->setValue(defaults.requestTimeout);
        retries->setValue(defaults.requestRetries);
        nonRepeaters->setValue(defaults.bulkNonRepeaters);
        maxRepetitions->setValue(defaults.bulkMaxRepetitions);
    });
    updateEnabled(mode->currentIndex());
    auto *buttons = new QHBoxLayout; buttons->addStretch();
    auto *ok = new QPushButton(tr("OK"), &dialog);
    auto *cancel = new QPushButton(tr("Cancel"), &dialog);
    buttons->addWidget(ok); buttons->addWidget(cancel); layout->addLayout(buttons);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    workingProfile.port = portEdit->text().trimmed();
    workingMetadata.hasRequestSettingsMode = mode->currentIndex() != 0;
    workingMetadata.requestSettingsMode = mode->currentIndex();
    workingMetadata.overrideTimeout = timeout->value();
    workingMetadata.overrideRetries = retries->value();
    workingMetadata.overrideBulkNonRepeaters = nonRepeaters->value();
    workingMetadata.overrideBulkMaxRepetitions = maxRepetitions->value();
    setDirty(true);
}

void DeviceDetailsEditor::revert()
{
    if (!folderId.isEmpty()) {
        loading = true; folderNameEdit->setText(folderName); loading = false;
        setDirty(false);
    } else if (!workingProfile.profileId.isEmpty()) {
        showProfile(workingProfile.profileId);
    }
}

void DeviceDetailsEditor::setDirty(bool value)
{
    if (dirty == value) return;
    dirty = value;
    applyAction->setEnabled(value);
    revertAction->setEnabled(value);
    emit dirtyChanged(value);
}
