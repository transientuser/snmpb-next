#ifndef AGENTPROFILEREPOSITORY_H
#define AGENTPROFILEREPOSITORY_H

#include <qlist.h>
#include <qstring.h>

struct AgentProfileRecord
{
    QString name;
    bool v1;
    bool v2;
    bool v3;
    QString address;
    QString port;
    int retries;
    int timeout;
    QString readcomm;
    QString writecomm;
    int maxrepetitions;
    int nonrepeaters;
    QString secname;
    int seclevel;
    QString contextname;
    QString contextengineid;
};

class AgentProfileRepository
{
public:
    explicit AgentProfileRepository(const QString& filename);

    QList<AgentProfileRecord> Load() const;
    void Save(const QList<AgentProfileRecord>& profiles) const;
    QList<AgentProfileRecord> LoadOrCreateDefaults() const;

    static AgentProfileRecord DefaultProfile(const QString& name,
                                             const QString& address);
    static QList<AgentProfileRecord> DefaultProfiles();

private:
    QString filename;
};

#endif /* AGENTPROFILEREPOSITORY_H */
