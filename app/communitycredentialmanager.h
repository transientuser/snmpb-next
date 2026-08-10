#ifndef COMMUNITYCREDENTIALMANAGER_H
#define COMMUNITYCREDENTIALMANAGER_H

#include "credentialrecords.h"
#include <QDialog>

class CommunityCredentialService;
class AgentProfileService;
class QListWidget;
class QLineEdit;

class CommunityCredentialManager : public QDialog
{
    Q_OBJECT
public:
    CommunityCredentialManager(CommunityCredentialService *credentials,
                               AgentProfileService *profiles,
                               QWidget *parent = nullptr);
    void execute();
private slots:
    void selectRow(int row);
    void storeFields();
    void addCredential();
    void duplicateCredential();
    void deleteCredential();
private:
    void rebuildList(int selected = 0);
    CommunityCredentialService *service;
    AgentProfileService *profiles;
    QList<CommunityCredentialRecord> working;
    QListWidget *list;
    QLineEdit *name;
    QLineEdit *readCommunity;
    QLineEdit *writeCommunity;
    int current = -1;
};

#endif
