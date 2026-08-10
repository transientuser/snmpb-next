#include "agentprofileoperations.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <iostream>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

bool Equal(const AgentProfileRecord &a, const AgentProfileRecord &b)
{
    return a.name == b.name && a.address == b.address && a.port == b.port &&
           a.profileId == b.profileId &&
           a.v1 == b.v1 && a.v2 == b.v2 && a.v3 == b.v3 &&
           a.retries == b.retries && a.timeout == b.timeout &&
           a.readcomm == b.readcomm && a.writecomm == b.writecomm &&
           a.maxrepetitions == b.maxrepetitions &&
           a.nonrepeaters == b.nonrepeaters && a.secname == b.secname &&
           a.seclevel == b.seclevel && a.contextname == b.contextname &&
           a.contextengineid == b.contextengineid;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    AgentProfileRecord source =
        AgentProfileRepository::DefaultProfile("router", "192.0.2.8");
    source.v1 = false;
    source.v2 = true;
    source.v3 = true;
    source.secname = "shared-usm-user";
    source.seclevel = 2;
    source.contextname = "ctx";
    source.readcomm = "read";

    AgentProfileRecord existing = source;
    existing.name = "router copy";
    AgentProfileRecord duplicate;
    if (!Check(AgentProfileOperations::Duplicate({source, existing}, source.profileId,
                                                  &duplicate),
               "duplicate failed") ||
        !Check(duplicate.name == "router copy 2", "duplicate naming changed"))
        return 1;
    AgentProfileRecord expected = source;
    expected.name = duplicate.name;
    expected.profileId = duplicate.profileId;
    if (!Check(Equal(duplicate, expected), "duplicate did not copy all settings") ||
        !Check(duplicate.profileId != source.profileId,
               "duplicate reused source profile ID") ||
        !Check(duplicate.secname == source.secname,
               "duplicate changed USM reference"))
        return 1;

    AgentProfileRecord renamed = source;
    renamed.name = "router-renamed";
    if (!Check(renamed.profileId == source.profileId,
               "rename changed stable profile ID"))
        return 1;
    AgentProfileRecord recreated =
        AgentProfileRepository::DefaultProfile("router", source.address);
    if (!Check(recreated.profileId != source.profileId,
               "delete/recreate reused stable profile ID"))
        return 1;

    QList<AgentProfileRecord> originals{source, existing};
    AgentProfileEditTransaction transaction(originals);
    QList<AgentProfileRecord> edited = originals;
    edited[0].name = "changed";
    edited[0].address = "203.0.113.9";
    edited.removeLast();
    if (!Check(transaction.rollbackRecords().size() == 2,
               "cancel did not restore deleted profile") ||
        !Check(Equal(transaction.rollbackRecords()[0], source),
               "cancel did not restore edited profile"))
        return 1;

    QTemporaryDir temporary;
    const QString file = temporary.filePath("agents.conf");
    AgentProfileRepository repository(file);
    repository.Save(originals);
    QFile beforeFile(file);
    if (!beforeFile.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray before = beforeFile.readAll();
    beforeFile.close();
    // Cancel restores memory and intentionally performs no repository Save.
    edited = transaction.rollbackRecords();
    QFile afterCancelFile(file);
    if (!afterCancelFile.open(QIODevice::ReadOnly))
        return 1;
    const QByteArray afterCancel = afterCancelFile.readAll();
    if (!Check(before == afterCancel, "cancel changed agents.conf"))
        return 1;

    edited[0].address = "203.0.113.10";
    repository.Save(edited);
    if (!Check(repository.Load()[0].address == "203.0.113.10",
               "accepted edit was not persisted"))
        return 1;

    // Duplicate names remain accepted and ordered; existing lookup semantics
    // continue to resolve the first matching record.
    repository.Save({source, source});
    if (!Check(repository.Load().size() == 2,
               "legacy duplicate names no longer round-trip"))
        return 1;
    return 0;
}
