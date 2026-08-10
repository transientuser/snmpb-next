#ifndef AGENTPROFILESERVICE_H
#define AGENTPROFILESERVICE_H

#include "agentprofilerepository.h"

#include <QObject>

class AgentProfileService : public QObject
{
    Q_OBJECT

public:
    explicit AgentProfileService(const QString &fileName,
                                 QObject *parent = nullptr);

    const QList<AgentProfileRecord> &profiles() const;
    const AgentProfileRecord *findById(const QString &profileId) const;
    const AgentProfileRecord *findFirstByName(const QString &name) const;
    QString uniqueIdForName(const QString &name) const;

    QString create(const AgentProfileRecord &draft);
    bool update(const AgentProfileRecord &record);
    bool remove(const QString &profileId);
    QString duplicate(const QString &profileId);
    void reload();

signals:
    void profileCreated(const QString &profileId);
    void profileUpdated(const QString &profileId);
    void profileRenamed(const QString &profileId, const QString &oldName,
                        const QString &newName);
    void profileDeleted(const QString &profileId);
    void profileDuplicated(const QString &sourceId, const QString &newId);
    void profilesReloaded();
    void profilesChanged();

private:
    void save();

    AgentProfileRepository repository;
    QList<AgentProfileRecord> records;
};

#endif
