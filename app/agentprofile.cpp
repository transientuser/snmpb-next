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
#include "agentprofile.h"
#include "agentprofileoperations.h"
#include "agentprofileservice.h"
#include "profilemetadataservice.h"
#include "mibmodule.h"
#include "usmprofile.h"
#include "usmcredentialservice.h"
#include "communitycredentialservice.h"
#include <qhash.h>
#include <qmessagebox.h>
#include <QSignalBlocker>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QVBoxLayout>

AgentProfileManager::AgentProfileManager(Snmpb *snmpb,
                                         AgentProfileService *profileService,
                                         ProfileMetadataService *profileMetadata)
    : service(profileService), metadataService(profileMetadata)
{
    s = snmpb;

    ap.setupUi(&apw);
    auto *metadataPage = new QWidget(ap.ProfileProps);
    auto *metadataLayout = new QVBoxLayout(metadataPage);
    auto *metadataTitle = new QLabel(tr("Profile Information"), metadataPage);
    QFont titleFont = metadataTitle->font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    metadataTitle->setFont(titleFont);
    metadataLayout->addWidget(metadataTitle);
    auto *metadataForm = new QFormLayout;
    tagsEdit = new QLineEdit(metadataPage);
    tagsEdit->setPlaceholderText(tr("datacenter, core, customer"));
    notesEdit = new QTextEdit(metadataPage);
    metadataForm->addRow(tr("Tags"), tagsEdit);
    metadataForm->addRow(tr("Notes"), notesEdit);
    mibsEdit = new QListWidget(metadataPage);
    metadataForm->addRow(tr("Preferred MIB modules"), mibsEdit);
    metadataLayout->addLayout(metadataForm);
    ap.ProfileProps->addWidget(metadataPage);
    credentialStatus = new QLabel(ap.SecName->parentWidget());
    credentialStatus->setObjectName("CredentialStatus");
    credentialStatus->setWordWrap(true);
    if (ap.SecName->parentWidget()->layout())
        ap.SecName->parentWidget()->layout()->addWidget(credentialStatus);
    communitySource = new QComboBox(ap.ReadComm->parentWidget());
    communityStatus = new QLabel(ap.ReadComm->parentWidget());
    communityStatus->setWordWrap(true);
    if (ap.ReadComm->parentWidget()->layout())
    {
        ap.ReadComm->parentWidget()->layout()->addWidget(
            new QLabel(tr("Credential source"), ap.ReadComm->parentWidget()));
        ap.ReadComm->parentWidget()->layout()->addWidget(communitySource);
        ap.ReadComm->parentWidget()->layout()->addWidget(communityStatus);
    }
    connect(communitySource, &QComboBox::currentIndexChanged,
            this, &AgentProfileManager::SetCommunityBinding);

    // Set some properties for the Agent Profile TreeView
    ap.ProfileTree->header()->hide();
    ap.ProfileTree->setSortingEnabled( false );
    ap.ProfileTree->header()->setSortIndicatorShown( false );
    ap.ProfileTree->setLineWidth( 2 );
    ap.ProfileTree->setAllColumnsShowFocus( false );
    ap.ProfileTree->setFrameShape(QFrame::WinPanel);
    ap.ProfileTree->setFrameShadow(QFrame::Plain);
    ap.ProfileTree->setRootIsDecorated( true );

    // Create context menu actions
    ap.ProfileTree->setContextMenuPolicy (Qt::CustomContextMenu);
    connect( ap.ProfileTree, 
             SIGNAL( customContextMenuRequested ( const QPoint & ) ),
             this, SLOT( ContextMenu ( const QPoint & ) ) );
    addAct = new QAction(tr("&New agent profile"), this);
    connect(addAct, SIGNAL(triggered()), this, SLOT(Add()));
    deleteAct = new QAction(tr("&Delete agent profile"), this);
    connect(deleteAct, SIGNAL(triggered()), this, SLOT(Delete()));
    duplicateAct = new QAction(tr("D&uplicate agent profile"), this);
    connect(duplicateAct, SIGNAL(triggered()), this, SLOT(DuplicateCurrent()));

    connect( ap.ProfileTree, 
             SIGNAL( currentItemChanged( QTreeWidgetItem *, QTreeWidgetItem * ) ),
             this, SLOT( SelectedAgentProfile( QTreeWidgetItem *, QTreeWidgetItem * ) ) );
    connect( ap.ProfileTree, 
             SIGNAL( itemChanged( QTreeWidgetItem *, int ) ),
             this, SLOT( AgentProfileNameChange( QTreeWidgetItem *, int ) ) );
    connect( ap.V1, SIGNAL( toggled(bool) ),
             this, SLOT( ProtocolV1Support(bool) ) );
    connect( ap.V2, SIGNAL( toggled(bool) ),
             this, SLOT( ProtocolV2Support(bool) ) );
    connect( ap.V3, SIGNAL( toggled(bool) ),
             this, SLOT( ProtocolV3Support(bool) ) );
    connect( ap.ProfileName, SIGNAL( editingFinished() ), 
             this, SLOT ( SetProfileName() ) );
    connect( ap.Address, SIGNAL( editingFinished() ), 
             this, SLOT ( SetAddress() ) );
    connect( ap.Port, SIGNAL( editingFinished() ), 
             this, SLOT ( ApplyPort() ) );
    connect( ap.Retries, SIGNAL( valueChanged( int ) ),
             this, SLOT ( SetRetries() ) );
    connect( ap.Timeout, SIGNAL( valueChanged( int ) ), 
             this, SLOT ( SetTimeout() ) );
    connect( ap.ReadComm, SIGNAL( editingFinished() ), 
             this, SLOT ( SetReadComm() ) );
    connect( ap.WriteComm, SIGNAL( editingFinished() ), 
             this, SLOT ( SetWriteComm() ) );
    connect( ap.MaxRepetitions, SIGNAL( valueChanged( int ) ), 
             this, SLOT ( SetMaxRepetitions() ) );
    connect( ap.NonRepeaters, SIGNAL( valueChanged( int ) ), 
             this, SLOT ( SetNonRepeaters() ) );
    connect( ap.SecName, SIGNAL( currentIndexChanged( int ) ), 
             this, SLOT ( SetSecName() ) );
    connect( ap.SecLevel, SIGNAL( currentIndexChanged( int ) ), 
             this, SLOT ( SetSecLevel() ) );
    connect( ap.ContextName, SIGNAL( editingFinished() ), 
             this, SLOT ( SetContextName() ) );
    connect( ap.ContextEngineID, SIGNAL( editingFinished() ), 
             this, SLOT ( SetContextEngineID() ) );
    connect(notesEdit, &QTextEdit::textChanged,
            this, &AgentProfileManager::SetNotes);
    connect(tagsEdit, &QLineEdit::textChanged,
            this, &AgentProfileManager::SetTags);
    connect(mibsEdit, &QListWidget::itemChanged,
            this, &AgentProfileManager::SetPreferredMibs);

    currentprofile = NULL; 

    connect(service, &AgentProfileService::profilesChanged,
            this, &AgentProfileManager::AgentProfileListChanged);
    connect(service, &AgentProfileService::profileRenamed,
            this, &AgentProfileManager::AgentProfileRenamed);
    connect(service, &AgentProfileService::profileDuplicated,
            this, &AgentProfileManager::AgentProfileDuplicated);

    // Loop & load all stored agent profiles
    ReadConfigFile();

    if (agents.size() != 0)
       ap.ProfileTree->setCurrentItem(ap.ProfileTree->topLevelItem(0));
}

void AgentProfileManager::ReadConfigFile (void)
{
    const QList<AgentProfileRecord> profiles = service->profiles();
    for (const AgentProfileRecord& profile : profiles)
        agents.append(new AgentProfile(&ap, profile));
}

void AgentProfileManager::WriteConfigFile (void)
{
    const QList<AgentProfileRecord> edited = EditorRecords();
    QStringList editedIds;
    for (const AgentProfileRecord &record : edited)
    {
        editedIds.append(record.profileId);
        if (service->findById(record.profileId))
            service->update(record);
        else
            service->create(record);
    }
    const QList<AgentProfileRecord> original = service->profiles();
    for (const AgentProfileRecord &record : original)
        if (!editedIds.contains(record.profileId))
            service->remove(record.profileId);
}

QList<AgentProfileRecord> AgentProfileManager::EditorRecords(void) const
{
    QList<AgentProfileRecord> records;
    for (AgentProfile *agent : agents)
        records.append(agent->GetRecord());
    return records;
}

void AgentProfileManager::ReplaceRecords(
    const QList<AgentProfileRecord> &records)
{
    const QSignalBlocker blocker(ap.ProfileTree);
    currentprofile = NULL;
    qDeleteAll(agents);
    agents.clear();
    for (const AgentProfileRecord &record : records)
        agents.append(new AgentProfile(&ap, record));
    if (!agents.isEmpty())
        ap.ProfileTree->setCurrentItem(agents.first()->GetGeneralWidgetItem());
}

void AgentProfileManager::Execute (bool reload)
{
    QString selectedId;
    if (currentprofile)
        selectedId = currentprofile->GetRecord().profileId;
    if (reload)
    {
        ReplaceRecords(service->profiles());
        if (!selectedId.isEmpty())
            SetSelectedAgentById(selectedId);
    }
    workingMetadata.clear();
    workingCommunityBindings.clear();
    for (const AgentProfileRecord &record : EditorRecords())
    {
        workingMetadata.insert(record.profileId,
                               metadataService->metadataForProfile(record.profileId));
        workingCommunityBindings.insert(
            record.profileId, s->CommunityCredentials()->binding(record.profileId));
    }
    // Fill-in loaded user names
    QString cpn;
    if (currentprofile)
        cpn = currentprofile->GetSecName();
    {
        const QSignalBlocker blocker(ap.SecName);
        ap.SecName->clear();
        ap.SecName->addItems(s->UPManagerObj()->GetUsersList());
        if (currentprofile)
        {
            int idx = ap.SecName->findText(cpn);
            if (idx < 0 && !cpn.isEmpty())
            {
                ap.SecName->addItem(cpn);
                idx = ap.SecName->count() - 1;
            }
            ap.SecName->setCurrentIndex(idx);
        }
    }
    UpdateCredentialStatus();

    if(apw.exec() == QDialog::Accepted)
    {
        WriteConfigFile();
        QStringList retainedIds;
        for (const AgentProfileRecord &record : service->profiles())
        {
            retainedIds.append(record.profileId);
            ProfileMetadataRecord metadata = workingMetadata.value(record.profileId);
            metadata.profileId = record.profileId;
            metadataService->update(metadata);
        }
        metadataService->reconcile(retainedIds, true);
        for (const AgentProfileRecord &record : service->profiles())
        {
            const QString credentialId = workingCommunityBindings.value(record.profileId);
            if (credentialId.isEmpty()) s->CommunityCredentials()->unbind(record.profileId);
            else s->CommunityCredentials()->bind(record.profileId, credentialId);
        }
        s->CommunityCredentials()->reconcileProfiles(retainedIds);
        ReplaceRecords(service->profiles());
    }
    else
        ReplaceRecords(service->profiles());
}

void AgentProfileManager::SetSelectedAgent(QString a)
{
    QTreeWidgetItem *item;
    for (int i = 0; i < ap.ProfileTree->topLevelItemCount(); i++)
    {
        item = ap.ProfileTree->topLevelItem(i);
        if (item->text(0) == a)
        {
            ap.ProfileTree->setCurrentItem(item);
            break;
        }
    }
}

AgentProfile *AgentProfileManager::GetAgentProfile(QString a)
{
    for (int i = 0; i < agents.size(); i++)
    {
        if (agents[i]->GetName() == a)
            return agents[i];
    }
    return NULL;
}

void AgentProfileManager::SetCommunityBinding(int index)
{
    if (!currentprofile || index < 0) return;
    workingCommunityBindings.insert(currentprofile->GetRecord().profileId,
                                    communitySource->currentData().toString());
    const QString id = communitySource->currentData().toString();
    if (id.isEmpty()) communityStatus->setText(tr("Using inline communities"));
    else if (s->CommunityCredentials()->find(id))
        communityStatus->setText(tr("Reusable credential available"));
    else communityStatus->setText(tr("Reusable credential missing"));
}

void AgentProfileManager::RefreshCredentialChoices(void)
{
    const QString selected = ap.SecName->currentText();
    const QSignalBlocker blocker(ap.SecName);
    ap.SecName->clear();
    if (s->UPManagerObj()) ap.SecName->addItems(s->UPManagerObj()->GetUsersList());
    int index = ap.SecName->findText(selected);
    if (index < 0 && !selected.isEmpty())
    {
        ap.SecName->addItem(selected);
        index = ap.SecName->count() - 1;
    }
    ap.SecName->setCurrentIndex(index);
    UpdateCredentialStatus();
}

void AgentProfileManager::UpdateCredentialStatus(void)
{
    if (!credentialStatus) return;
    if (!currentprofile || !s->UsmCredentials())
    {
        credentialStatus->clear();
        return;
    }
    const UsmReferenceResult result =
        s->UsmCredentials()->validate(currentprofile->GetRecord());
    QString text;
    switch (result.status)
    {
        case UsmReferenceStatus::Valid: text = tr("Credential status: Available"); break;
        case UsmReferenceStatus::Missing: text = tr("Credential status: Credential not found"); break;
        case UsmReferenceStatus::Ambiguous: text = tr("Credential status: Ambiguous security name"); break;
        case UsmReferenceStatus::IncompatibleSecurityLevel:
            text = tr("Credential status: Security level is not supported by this credential"); break;
        case UsmReferenceStatus::Empty: text = tr("Credential status: No security name selected"); break;
        default: break;
    }
    credentialStatus->setText(text);
}

AgentProfile *AgentProfileManager::GetAgentProfileById(const QString &profileId)
{
    for (AgentProfile *agent : agents)
        if (agent->GetRecord().profileId == profileId)
            return agent;
    return NULL;
}

void AgentProfileManager::SetSelectedAgentById(const QString &profileId)
{
    AgentProfile *agent = GetAgentProfileById(profileId);
    if (agent)
        ap.ProfileTree->setCurrentItem(agent->GetGeneralWidgetItem());
}

void AgentProfileManager::EditProfile(const QString &profileId)
{
    ReplaceRecords(service->profiles());
    SetSelectedAgentById(profileId);
    Execute(false);
}

QString AgentProfileManager::DuplicateProfile(const QString &profileId)
{
    return service->duplicate(profileId);
}

void AgentProfileManager::NewProfile(void)
{
    QStringList before;
    for (const AgentProfileRecord &record : service->profiles())
        before.append(record.profileId);
    ReplaceRecords(service->profiles());
    Add();
    Execute(false);
    for (const AgentProfileRecord &record : service->profiles())
        if (!before.contains(record.profileId))
        {
            emit NewProfileCompleted(record.profileId);
            break;
        }
}

bool AgentProfileManager::DeleteProfile(const QString &profileId)
{
    return service->remove(profileId);
}

QList<AgentProfileRecord> AgentProfileManager::GetAgentProfileRecords(void) const
{
    return service->profiles();
}

const AgentProfileRecord *AgentProfileManager::GetAgentProfileRecord(
    const QString &profileId) const
{
    return service->findById(profileId);
}

const AgentProfileRecord *AgentProfileManager::GetAgentProfileRecordByName(
    const QString &name) const
{
    return service->findFirstByName(name);
}

void AgentProfileManager::PersistProfiles(void)
{
    // Profile persistence is owned by AgentProfileService. This compatibility
    // slot remains for the device-tree organization signal.
}

void AgentProfileManager::ProtocolV1Support(bool checked)
{
    if (currentprofile)
        currentprofile->ProtocolV1Support(checked);
}

void AgentProfileManager::ProtocolV2Support(bool checked)
{
    if (currentprofile)
        currentprofile->ProtocolV2Support(checked);
}

void AgentProfileManager::ProtocolV3Support(bool checked)
{
    if (currentprofile)
        currentprofile->ProtocolV3Support(checked);
}

void AgentProfileManager::SetProfileName(void)
{
    if (currentprofile)
        currentprofile->SetProfileName();
}

void AgentProfileManager::SetAddress(void)
{
    if (currentprofile)
        currentprofile->SetAddress();
}

void AgentProfileManager::ApplyPort(void)
{
    if (currentprofile)
        currentprofile->ApplyPort();
}

void AgentProfileManager::SetRetries(void)
{
    if (currentprofile)
        currentprofile->SetRetries();
}

void AgentProfileManager::SetTimeout(void)
{
    if (currentprofile)
        currentprofile->SetTimeout();
}

void AgentProfileManager::SetReadComm(void)
{
    if (currentprofile)
        currentprofile->SetReadComm();
}

void AgentProfileManager::SetWriteComm(void)
{
    if (currentprofile)
        currentprofile->SetWriteComm();
}

void AgentProfileManager::SetMaxRepetitions(void)
{
    if (currentprofile)
        currentprofile->SetMaxRepetitions();
}

void AgentProfileManager::SetNonRepeaters(void)
{
    if (currentprofile)
        currentprofile->SetNonRepeaters();
}

void AgentProfileManager::SetSecName(void)
{
    if (currentprofile)
        currentprofile->SetSecName();
    UpdateCredentialStatus();
}

void AgentProfileManager::SetSecLevel(void)
{
    if (currentprofile)
        currentprofile->SetSecLevel();
    UpdateCredentialStatus();
}

void AgentProfileManager::SetContextName(void)
{
    if (currentprofile)
        currentprofile->SetContextName();
}

void AgentProfileManager::SetContextEngineID(void)
{
    if (currentprofile)
        currentprofile->SetContextEngineID();
}

void AgentProfileManager::SetNotes(void)
{
    if (currentprofile)
        workingMetadata[currentprofile->GetRecord().profileId].notes =
            notesEdit->toPlainText();
}

void AgentProfileManager::SetTags(void)
{
    if (currentprofile)
        workingMetadata[currentprofile->GetRecord().profileId].tags =
            ProfileMetadataRepository::normalizeTags(
                tagsEdit->text().split(',', Qt::KeepEmptyParts));
}

void AgentProfileManager::SetPreferredMibs(void)
{
    if (!currentprofile) return;
    QStringList selected;
    for (int i = 0; i < mibsEdit->count(); ++i)
        if (mibsEdit->item(i)->checkState() == Qt::Checked)
            selected.append(mibsEdit->item(i)->text());
    workingMetadata[currentprofile->GetRecord().profileId].preferredMibs =
        ProfileMetadataRepository::normalizeMibs(selected);
}

void AgentProfileManager::ContextMenu ( const QPoint &pos )
{    
    QMenu menu(tr("Actions"), ap.ProfileTree);

    menu.addAction(addAct);
    menu.addAction(duplicateAct);
    menu.addAction(deleteAct);

    menu.exec(ap.ProfileTree->mapToGlobal(pos));
}

void AgentProfileManager::Add(void)
{
    AgentProfile * newagent = new AgentProfile(&ap);
    // Set default values
    newagent->SetSupportedProtocol(true, false, false); // SNMPV1 only
    newagent->SetTarget("127.0.0.1", "161");
    newagent->SetRetriesTimeout(1, 3);
    newagent->SetComms("public", "private");
    newagent->SetBulk(10, 0);
    newagent->SetUser("", 0);
    newagent->SetContext("", "");

    agents.append(newagent);
    ProfileMetadataRecord metadata;
    metadata.profileId = newagent->GetRecord().profileId;
    workingMetadata.insert(metadata.profileId, metadata);

    // Select the new item and change the focus to change its name ...
    ap.ProfileTree->setCurrentItem(newagent->GetGeneralWidgetItem());
    ap.ProfileName->setFocus(Qt::OtherFocusReason);  
    ap.ProfileName->selectAll();  
}

void AgentProfileManager::Add(QString name, QString address, QString port,
                              bool isv1, bool isv2c, bool isv3, QString clonefrom)
{
    const AgentProfileRecord *clone = service->findById(clonefrom);
    if (!clone)
        clone = service->findFirstByName(clonefrom); // legacy name adapter
    if (!clone || service->findFirstByName(name))
        return;
    AgentProfileRecord created = *clone;
    created.profileId.clear();
    created.name = name;
    created.address = address;
    created.port = port;
    created.v1 = isv1;
    created.v2 = isv2c;
    created.v3 = isv3;
    const QString createdId = service->create(created);
    if (!createdId.isEmpty())
        s->CommunityCredentials()->copyBinding(clone->profileId, createdId);
}

void AgentProfileManager::Delete(void)
{
    QTreeWidgetItem *p = NULL;

    for (int i = 0; i < agents.count(); i++) 
    {
        if (currentprofile && (agents[i] == currentprofile))
        {
            // Delete the profile (removes from the list)
            delete agents.takeAt(i);
            currentprofile = NULL;
            // Readjust the currentprofile pointer with the new current widget item
            if ((p = ap.ProfileTree->currentItem()) != NULL)
            {
                for (int i = 0; i < agents.count(); i++) 
                {
                    if (agents[i]->IsPartOfAgentProfile(p))
                    {
                        currentprofile = agents[i];
                        break;
                    }
                }
            }
            break;
        }
    }
}

void AgentProfileManager::SelectedAgentProfile(QTreeWidgetItem * item, QTreeWidgetItem *)
{
    for (int i = 0; i < agents.count(); i++) 
    {
        if (agents[i]->IsPartOfAgentProfile(item))
        {
            currentprofile = agents[i];
            agents[i]->SelectAgentProfile(item);
            {
                const QSignalBlocker blocker(communitySource);
                communitySource->clear();
                communitySource->addItem(tr("Inline communities"), QString());
                for (const CommunityCredentialRecord &credential :
                     s->CommunityCredentials()->records())
                    communitySource->addItem(credential.displayName,
                                             credential.identity.credentialId);
                const QString bound = workingCommunityBindings.value(
                    agents[i]->GetRecord().profileId);
                int credentialIndex = communitySource->findData(bound);
                if (credentialIndex < 0 && !bound.isEmpty())
                {
                    communitySource->addItem(tr("Missing reusable credential"), bound);
                    credentialIndex = communitySource->count() - 1;
                }
                communitySource->setCurrentIndex(qMax(0, credentialIndex));
            }
            SetCommunityBinding(communitySource->currentIndex());
            if (agents[i]->IsMetadataItem(item))
            {
                const QSignalBlocker notesBlock(notesEdit);
                const QSignalBlocker tagsBlock(tagsEdit);
                const QSignalBlocker mibsBlock(mibsEdit);
                const ProfileMetadataRecord metadata = workingMetadata.value(
                    agents[i]->GetRecord().profileId);
                notesEdit->setPlainText(metadata.notes);
                tagsEdit->setText(metadata.tags.join(", "));
                QStringList names = s->MibModuleObj()->AvailableModuleNames();
                for (const QString &preferred : metadata.preferredMibs)
                    if (!names.contains(preferred)) names.append(preferred);
                names.sort(Qt::CaseInsensitive);
                mibsEdit->clear();
                for (const QString &name : names)
                {
                    auto *entry = new QListWidgetItem(name, mibsEdit);
                    entry->setFlags(entry->flags() | Qt::ItemIsUserCheckable);
                    entry->setCheckState(metadata.preferredMibs.contains(name) ?
                                         Qt::Checked : Qt::Unchecked);
                }
            }
            return;
        }
    }
}

void AgentProfileManager::AgentProfileNameChange(QTreeWidgetItem * item, int column)
{
    if (column != 0)
        return;

    for (int i = 0; i < agents.count(); i++) 
    {
        if (agents[i]->IsPartOfAgentProfile(item))
        {
            agents[i]->SetName(item->text(0));
            return;
        }
    }
}

QStringList AgentProfileManager::GetAgentsList(void)
{
    QStringList sl;

    for(int i = 0; i < agents.size(); i++)
        sl << agents[i]->GetName();

    return sl;
}

AgentProfile::AgentProfile(Ui_AgentProfile *uiap, const QString *n)
{
    ap = uiap;
    record.profileId = AgentProfileRepository::CreateProfileId();

    general = new QTreeWidgetItem(ap->ProfileTree);

    if (n)
    {
        general->setText(0, n->toLatin1().data());
        SetName(*n);
    }
    else
    {
        general->setText(0, "newagent");
        SetName("newagent");
    }

    v1v2c = new QTreeWidgetItem(general);
    v1v2c->setText(0, "Snmpv1/v2c");
    bulk = new QTreeWidgetItem(general);
    bulk->setText(0, "Get-Bulk");
    v3 = new QTreeWidgetItem(general);
    v3->setText(0, "SnmpV3");
    metadata = new QTreeWidgetItem(general);
    metadata->setText(0, "Information");
}

void AgentProfileManager::DuplicateCurrent(void)
{
    if (currentprofile)
    {
        AgentProfileRecord duplicate;
        if (AgentProfileOperations::Duplicate(EditorRecords(),
                                              currentprofile->GetRecord().profileId,
                                              &duplicate))
        {
            agents.append(new AgentProfile(&ap, duplicate));
            ProfileMetadataRecord copied = workingMetadata.value(
                currentprofile->GetRecord().profileId);
            copied.profileId = duplicate.profileId;
            workingMetadata.insert(duplicate.profileId, copied);
            workingCommunityBindings.insert(
                duplicate.profileId,
                workingCommunityBindings.value(currentprofile->GetRecord().profileId));
            ap.ProfileTree->setCurrentItem(agents.last()->GetGeneralWidgetItem());
        }
    }
}

AgentProfile::AgentProfile(Ui_AgentProfile *uiap,
                           const AgentProfileRecord& profile)
    : AgentProfile(uiap, &profile.name)
{
    record = profile;
    ApplySupportedProtocol();
}

AgentProfile::~AgentProfile()
{
    delete v1v2c;
    delete bulk;
    delete v3;
    delete metadata;
    delete general;
}

void AgentProfile::ProtocolV1Support(bool checked)
{
    if (!checked && 
        (ap->V2->checkState() == Qt::Unchecked) && 
        (ap->V3->checkState() == Qt::Unchecked))
    {
        QMessageBox::critical(NULL,
                              tr("SnmpB error"),
                              tr("At least one protocol must be selected\n"),
                              QMessageBox::Ok);
        ap->V1->setCheckState(Qt::Checked); 
        return;
    }

    if (ap->V2->checkState() == Qt::Unchecked)
    {
        if (checked)
            v1v2c->setHidden(false);
        else
            v1v2c->setHidden(true);
    }

    record.v1 = checked;
}

void AgentProfile::ProtocolV2Support(bool checked)
{
    if (!checked && 
        (ap->V1->checkState() == Qt::Unchecked) && 
        (ap->V3->checkState() == Qt::Unchecked))
    {
        QMessageBox::critical(NULL,
                              tr("SnmpB error"),
                              tr("At least one protocol must be selected\n"),
                              QMessageBox::Ok);
        ap->V2->setCheckState(Qt::Checked);
        return;
    }

    if (ap->V1->checkState() == Qt::Unchecked)
    {
        if (checked)
            v1v2c->setHidden(false);
        else
            v1v2c->setHidden(true);
    }

    if (ap->V3->checkState() == Qt::Unchecked)
    {
        if (checked)
            bulk->setHidden(false);
        else
            bulk->setHidden(true);
    }

    record.v2 = checked;
}

void AgentProfile::ProtocolV3Support(bool checked)
{
    if (!checked && 
        (ap->V1->checkState() == Qt::Unchecked) && 
        (ap->V2->checkState() == Qt::Unchecked))
    {
        QMessageBox::critical(NULL,
                              tr("SnmpB error"),
                              tr("At least one protocol must be selected\n"),
                              QMessageBox::Ok);
        ap->V3->setCheckState(Qt::Checked); 
        return;
    }

    if (checked)
        v3->setHidden(false);
    else
        v3->setHidden(true);

    if (ap->V2->checkState() == Qt::Unchecked)
    {
        if (checked)
            bulk->setHidden(false);
        else
            bulk->setHidden(true);
    }

    record.v3 = checked;
}

int AgentProfile::SelectAgentProfile(QTreeWidgetItem * item)
{
    if (item == general)
    {
        ap->ProfileProps->setCurrentIndex(0);

        ApplySupportedProtocol();
        ap->ProfileName->setText(record.name);
        ap->Address->setText(record.address);
        ap->Port->setText(record.port);
        ap->Retries->setValue(record.retries);
        ap->Timeout->setValue(record.timeout);

        return 1;
    }
    else
    if (item == metadata)
    {
        ap->ProfileProps->setCurrentIndex(4);
        return 1;
    }
    else
    if (item == v1v2c)
    {
        ap->ProfileProps->setCurrentIndex(1);

        ap->ReadComm->setText(record.readcomm);
        ap->WriteComm->setText(record.writecomm);

        return 1;
    }
    else
    if (item == bulk)
    {
        ap->ProfileProps->setCurrentIndex(2);

        ap->MaxRepetitions->setValue(record.maxrepetitions);
        ap->NonRepeaters->setValue(record.nonrepeaters);

        return 1;
    }
    else
    if (item == v3)
    {
        ap->ProfileProps->setCurrentIndex(3);

        ap->SecName->setCurrentIndex(ap->SecName->findText(record.secname));
        ap->SecLevel->setCurrentIndex(record.seclevel);
        ap->ContextName->setText(record.contextname);
        ap->ContextEngineID->setText(record.contextengineid);

        return 1;
    }

    return 0;
}

void AgentProfile::ApplySupportedProtocol(void)
{
    // Order is important: first set the ones that are supported ...
    if (record.v1)
    {
        ap->V1->setCheckState(Qt::Checked); 
        ProtocolV1Support(record.v1);
    }
    if (record.v2)
    {
        ap->V2->setCheckState(Qt::Checked); 
        ProtocolV2Support(record.v2);
    }
    if (record.v3)
    {
        ap->V3->setCheckState(Qt::Checked); 
        ProtocolV3Support(record.v3);
    }
    // ... then the ones that are not. This avoids the message box protection
    if (!record.v1)
    {
        ap->V1->setCheckState(Qt::Unchecked);
        ProtocolV1Support(record.v1);
    }
    if (!record.v2)
    {
        ap->V2->setCheckState(Qt::Unchecked);
        ProtocolV2Support(record.v2);
    }
    if (!record.v3)
    {
        ap->V3->setCheckState(Qt::Unchecked);
        ProtocolV3Support(record.v3);
    }
}

int AgentProfile::IsPartOfAgentProfile(QTreeWidgetItem * item)
{
    if ((item == general) || (item == v1v2c) || (item == bulk) || (item == v3))
        return 1;
    else
        return 0;
}

QTreeWidgetItem *AgentProfile::GetGeneralWidgetItem(void)
{
    return general;
}

bool AgentProfile::IsMetadataItem(QTreeWidgetItem *item) const
{
    return item == metadata;
}

void AgentProfile::GetSupportedProtocol(bool *v1, bool *v2, bool *v3)
{
    if (v1) *v1 = record.v1;
    if (v2) *v2 = record.v2;
    if (v3) *v3 = record.v3;
}

void AgentProfile::SetSupportedProtocol(bool v1, bool v2, bool v3)
{
    record.v1 = v1;
    record.v2 = v2;
    record.v3 = v3;

    ApplySupportedProtocol();
}

void AgentProfile::SetName(QString n)
{
    record.name = n;
    ap->ProfileName->setText(record.name);
}

QString AgentProfile::GetName(void)
{
    return record.name;
}

void AgentProfile::SetProfileName(void)
{
    record.name = ap->ProfileName->text();
    general->setText(0, record.name);
}

void AgentProfile::SetAddress(void)
{
    record.address = ap->Address->text();
}

QString AgentProfile::GetAddress(void)
{
    return record.address;
}

void AgentProfile::ApplyPort(void)
{
    record.port = ap->Port->text();
}

QString AgentProfile::GetPort(void)
{
    return record.port;
}

void AgentProfile::SetTarget(QString a, QString p)
{
    record.address = a;
    record.port = p;
}

void AgentProfile::SetRetries(void)
{
    record.retries = ap->Retries->value();
}

int AgentProfile::GetRetries(void)
{
    return record.retries;
}

void AgentProfile::SetTimeout(void)
{
    record.timeout = ap->Timeout->value();
}

int AgentProfile::GetTimeout(void)
{
    return record.timeout;
}

void AgentProfile::SetRetriesTimeout(int r, int t)
{
    record.retries = r;
    record.timeout = t;
}

void AgentProfile::SetReadComm(void)
{
    record.readcomm = ap->ReadComm->text();
}

QString AgentProfile::GetReadComm(void)
{
    return record.readcomm;
}

void AgentProfile::SetWriteComm(void)
{
    record.writecomm = ap->WriteComm->text();
}

QString AgentProfile::GetWriteComm(void)
{
    return record.writecomm;
}

void AgentProfile::SetComms(QString r, QString w)
{
    record.readcomm = r;
    record.writecomm = w;
}

void AgentProfile::SetMaxRepetitions(void)
{
    record.maxrepetitions = ap->MaxRepetitions->value();
}

int AgentProfile::GetMaxRepetitions(void)
{
    return record.maxrepetitions;
}

void AgentProfile::SetNonRepeaters(void)
{
    record.nonrepeaters = ap->NonRepeaters->value();
}

int AgentProfile::GetNonRepeaters(void)
{
    return record.nonrepeaters;
}

void AgentProfile::SetBulk(int mr, int nr)
{
    record.maxrepetitions = mr;
    record.nonrepeaters = nr;
}

void AgentProfile::SetSecName(void)
{
    record.secname = ap->SecName->itemText(ap->SecName->currentIndex());
}

QString AgentProfile::GetSecName(void)
{
    return record.secname;
}

void AgentProfile::SetSecLevel(void)
{
    record.seclevel = ap->SecLevel->currentIndex();
}

int AgentProfile::GetSecLevel(void)
{
    return record.seclevel;
}

void AgentProfile::SetUser(QString u, int l)
{
    record.secname = u;
    record.seclevel = l;
}

void AgentProfile::SetContextName(void)
{
    record.contextname = ap->ContextName->text();
}

QString AgentProfile::GetContextName(void)
{
    return record.contextname;
}

void AgentProfile::SetContextEngineID(void)
{
    record.contextengineid = ap->ContextEngineID->text();
}

QString AgentProfile::GetContextEngineID(void)
{
    return record.contextengineid;
}

void AgentProfile::SetContext(QString n, QString id)
{
    record.contextname = n;
    record.contextengineid = id;
}

const AgentProfileRecord& AgentProfile::GetRecord(void) const
{
    return record;
}

