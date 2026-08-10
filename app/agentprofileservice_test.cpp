#include "agentprofileservice.h"

#include <QCoreApplication>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");
    const QString file = dir.filePath("agents.conf");
    AgentProfileService service(file);
    const int defaults = service.profiles().size();

    AgentProfileRecord draft = AgentProfileRepository::DefaultProfile("same", "192.0.2.1");
    draft.profileId.clear();
    const QString first = service.create(draft);
    const QString second = service.create(draft);
    ok &= check(!first.isEmpty() && first != second, "create assigns distinct IDs");
    ok &= check(service.findFirstByName("same") != nullptr &&
                service.uniqueIdForName("same").isEmpty(), "duplicate names are supported but ambiguous");

    AgentProfileRecord changed = *service.findById(first);
    AgentProfileRecord cancelled = changed;
    cancelled.address = "198.51.100.99";
    ok &= check(service.findById(first)->address != cancelled.address,
                "cancelled working copy leaves canonical record untouched");
    AgentProfileRecord cancelledNew = draft;
    cancelledNew.name = "cancelled-new";
    ok &= check(!service.findFirstByName(cancelledNew.name),
                "cancelled new-profile draft creates no record");
    changed.name = "renamed";
    changed.timeout = 17;
    ok &= check(service.update(changed) && service.findById(first)->timeout == 17,
                "update targets stable ID");
    ok &= check(service.findById(first)->profileId == first, "rename preserves ID");
    AgentProfileRecord sameNameSecond = *service.findById(second);
    sameNameSecond.timeout = 29;
    ok &= check(service.update(sameNameSecond) &&
                service.findById(second)->timeout == 29 &&
                service.findById(first)->timeout == 17,
                "same-name edit targets exactly one ID");

    const QString copy = service.duplicate(first);
    ok &= check(!copy.isEmpty() && copy != first && service.findById(copy),
                "duplicate gets new ID");
    const QString discoveredA = service.createFromTemplate(
        first, "same-discovered", "192.0.2.40", "161", true, true, false);
    const QString discoveredB = service.createFromTemplate(
        first, "same-discovered", "192.0.2.41", "161", true, false, false);
    ok &= check(!discoveredA.isEmpty() && !discoveredB.isEmpty() &&
                discoveredA != discoveredB,
                "Discovery same-name profiles retain distinct IDs");
    ok &= check(service.remove(second) && !service.findById(second), "delete by ID");

    AgentProfileService reloaded(file);
    ok &= check(reloaded.profiles().size() == defaults + 4 &&
                reloaded.findById(first) && reloaded.findById(copy),
                "repository persistence");
    return ok ? 0 : 1;
}
