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
#ifndef AGENTPROFILE_H
#define AGENTPROFILE_H

#include "snmpb.h"
#include "agentprofilerepository.h"
#include "profilemetadatarepository.h"
#include "ui_agentprofile.h"
#include <qdialog.h>
#include <qtreewidget.h>
#include <qlist.h>
#include <qhash.h>

class AgentProfile: public QObject
{
    Q_OBJECT
    
public:
    AgentProfile(Ui_AgentProfile *uiap, const QString *n = NULL);
    AgentProfile(Ui_AgentProfile *uiap, const AgentProfileRecord& record);
    ~AgentProfile();

    int IsPartOfAgentProfile(QTreeWidgetItem *item);
    int SelectAgentProfile(QTreeWidgetItem * item);
    QTreeWidgetItem *GetGeneralWidgetItem(void);
    bool IsMetadataItem(QTreeWidgetItem *item) const;
    void ProtocolV1Support(bool checked);
    void ProtocolV2Support(bool checked);
    void ProtocolV3Support(bool checked);

    void GetSupportedProtocol(bool *v1, bool *v2, bool *v3);
    void SetSupportedProtocol(bool v1, bool v2, bool v3);
    void ApplySupportedProtocol(void);

    void SetName(QString n);
    QString GetName(void);
    void SetProfileName(void);

    void SetAddress(void);
    void SetAddress(QString a) {record.address = a;}
    QString GetAddress(void);
    void ApplyPort(void);
    QString GetPort(void);
    void SetTarget(QString a, QString p);

    void SetRetries(void);
    int GetRetries(void);
    void SetTimeout(void);
    int GetTimeout(void);
    void SetRetriesTimeout(int r, int t);
    
    void SetReadComm(void);
    QString GetReadComm(void);
    void SetWriteComm(void);
    QString GetWriteComm(void);
    void SetComms(QString r, QString w);
    
    void SetMaxRepetitions(void);
    int GetMaxRepetitions(void);
    void SetNonRepeaters(void);
    int GetNonRepeaters(void);
    void SetBulk(int mr, int nr);

    void SetSecName(void);
    QString GetSecName(void);
    void SetSecLevel(void);
    int GetSecLevel(void);
    void SetUser(QString u, int l);

    void SetContextName(void);
    QString GetContextName(void);
    void SetContextEngineID(void);
    QString GetContextEngineID(void);
    void SetContext(QString n, QString id);

    const AgentProfileRecord& GetRecord(void) const;

protected:
    Ui_AgentProfile *ap;

    QTreeWidgetItem *general;
    QTreeWidgetItem *v1v2c;
    QTreeWidgetItem *bulk;
    QTreeWidgetItem *v3;
    QTreeWidgetItem *metadata;

    AgentProfileRecord record;
};

class AgentProfileManager: public QObject
{
    Q_OBJECT

public:
    AgentProfileManager(Snmpb *snmpb, class AgentProfileService *service,
                        class ProfileMetadataService *metadataService);
    void Execute(bool reload = true);
    QStringList GetAgentsList(void);
    QList<AgentProfileRecord> GetAgentProfileRecords(void) const;
    const AgentProfileRecord *GetAgentProfileRecord(const QString &profileId) const;
    const AgentProfileRecord *GetAgentProfileRecordByName(const QString &name) const;
    void SetSelectedAgent(QString a);
    AgentProfile *GetAgentProfile(QString a);
    AgentProfile *GetAgentProfileById(const QString &profileId);
    void SetSelectedAgentById(const QString &profileId);
    void EditProfile(const QString &profileId);
    void NewProfile(void);
    bool DeleteProfile(const QString &profileId);
    QString DuplicateProfile(const QString &profileId);
    void PersistProfiles(void);
    void Add(QString name, QString address, QString port,
             bool isv1, bool isv2c, bool isv3, QString clonefrom);

signals:
    void AgentProfileListChanged(void);
    void AgentProfileRenamed(const QString &profileId, const QString &oldName,
                             const QString &newName);
    void AgentProfileDuplicated(const QString &sourceId, const QString &newId);
    void NewProfileCompleted(const QString &profileId);

protected:
    QAction *addAct;
    QAction *deleteAct;
    QAction *duplicateAct;

protected slots:
    void ProtocolV1Support(bool checked);
    void ProtocolV2Support(bool checked);
    void ProtocolV3Support(bool checked);
    void SetProfileName(void);
    void SetAddress(void);
    void ApplyPort(void);
    void SetRetries(void);
    void SetTimeout(void);
    void SetReadComm(void);
    void SetWriteComm(void);
    void SetMaxRepetitions(void);
    void SetNonRepeaters(void);
    void SetSecName(void);
    void SetSecLevel(void);
    void SetContextName(void);
    void SetContextEngineID(void);
    void SetNotes(void);
    void SetTags(void);
    void SetPreferredMibs(void);
    void SelectedAgentProfile( QTreeWidgetItem * item, QTreeWidgetItem * old);
    void AgentProfileNameChange(QTreeWidgetItem * item, int column);
    void Add(void);
    void Delete(void);
    void DuplicateCurrent(void);
    void ContextMenu ( const QPoint & );

private:
    void ReadConfigFile(void);
    void WriteConfigFile(void);
    void ReplaceRecords(const QList<AgentProfileRecord> &records);
    QList<AgentProfileRecord> EditorRecords(void) const;

private:
    Snmpb *s;
    AgentProfileService *service;
    ProfileMetadataService *metadataService;
    Ui_AgentProfile ap;
    QDialog apw;
    AgentProfile* currentprofile;
    QList<AgentProfile *> agents;
    QHash<QString, ProfileMetadataRecord> workingMetadata;
    class QTextEdit *notesEdit;
    class QLineEdit *tagsEdit;
    class QListWidget *mibsEdit;
};

#endif /* AGENTPROFILE_H */
