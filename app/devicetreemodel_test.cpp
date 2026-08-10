#include "devicetreemodel.h"

#include <QCoreApplication>
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

AgentProfileRecord Profile(const QString &name, const QString &address)
{
    return AgentProfileRepository::DefaultProfile(name, address);
}

QModelIndex Find(const DeviceTreeModel &model, const QString &text,
                 const QModelIndex &parent = {})
{
    for (int row = 0; row < model.rowCount(parent); ++row)
    {
        QModelIndex child = model.index(row, 0, parent);
        if (child.data().toString() == text)
            return child;
        QModelIndex nested = Find(model, text, child);
        if (nested.isValid())
            return nested;
    }
    return {};
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!Check(temporary.isValid(), "temporary directory failed"))
        return 1;
    const QString file = temporary.filePath("device-tree.conf");
    DeviceTreeRepository repository(file);
    AgentProfileRecord coreProfile = Profile("core-01", "192.0.2.1");
    AgentProfileRecord labProfile = Profile("lab-01", "192.0.2.2");
    AgentProfileRecord newProfile = Profile("new", "2001:db8::1");
    DeviceTreeState state;
    state.folders = {{"dc", "", "Datacenter", 0},
                     {"lab", "dc", "Lab", 0}};
    state.placements = {{"core-node", "dc", coreProfile.profileId, "", 0},
                        {"lab-node", "lab", labProfile.profileId, "", 0}};
    repository.Save(state);

    DeviceTreeModel model(file, {coreProfile, labProfile, newProfile});
    QModelIndex dc = Find(model, "Datacenter");
    QModelIndex lab = Find(model, "Lab");
    QModelIndex core = Find(model, "core-01");
    QModelIndex unfiled = Find(model, "Unfiled");
    QModelIndex newlyAdded = Find(model, "new");
    if (!Check(dc.isValid() && lab.isValid() && core.isValid(),
               "hierarchy missing") ||
        !Check(model.parent(lab) == dc, "folder parent incorrect") ||
        !Check(model.parent(core) == dc, "profile parent incorrect") ||
        !Check(model.isFolder(dc) && model.isProfile(core), "node type incorrect") ||
        !Check(model.isUnfiled(unfiled) && model.parent(newlyAdded) == unfiled,
               "Unfiled hierarchy incorrect") ||
        !Check(core.data(Qt::ToolTipRole).toString().contains("192.0.2.1"),
               "profile tooltip missing address"))
        return 1;

    model.setCredentialHealth({{coreProfile.profileId,
                                "Reusable credential available"}});
    core = Find(model, "core-01");
    if (!Check(core.data(DeviceTreeModel::CredentialHealthRole).toString() ==
                   "Reusable credential available",
               "credential health role missing") ||
        !Check(core.data(Qt::ToolTipRole).toString().contains(
                   "Credential: Reusable credential available"),
               "credential health tooltip missing") ||
        !Check(core.data(DeviceTreeModel::SearchTextRole).toString().contains(
                   "Reusable credential available"),
               "credential health search text missing") ||
        !Check(!core.data(Qt::ToolTipRole).toString().contains("secret-marker") &&
                   !core.data(DeviceTreeModel::SearchTextRole).toString().contains(
                       "secret-marker"),
               "credential health exposed a secret marker"))
        return 1;

    QModelIndex branch = model.createFolder("Branch", dc);
    dc = Find(model, "Datacenter");
    branch = Find(model, "Branch");
    if (!Check(branch.isValid() && model.parent(branch) == dc,
               "folder creation failed") ||
        !Check(model.setData(branch, "Branch Office"), "folder rename failed"))
        return 1;
    branch = Find(model, "Branch Office");
    if (!Check(model.moveProfile(newProfile.profileId, branch), "profile move failed"))
        return 1;
    branch = Find(model, "Branch Office");
    newlyAdded = Find(model, "new");
    if (!Check(model.parent(newlyAdded) == branch, "moved profile parent incorrect") ||
        !Check(!Find(model, "Unfiled").isValid(), "empty Unfiled remained"))
        return 1;

    coreProfile.name = "core-renamed";
    AgentProfileRecord duplicateProfile = coreProfile;
    duplicateProfile.profileId = AgentProfileRepository::CreateProfileId();
    duplicateProfile.name = "core-renamed copy";
    model.renameProfile(coreProfile.profileId, coreProfile.name);
    model.setProfiles({coreProfile, labProfile, newProfile, duplicateProfile});
    model.placeDuplicate(coreProfile.profileId, duplicateProfile.profileId);
    QModelIndex renamed = Find(model, "core-renamed");
    QModelIndex duplicated = Find(model, "core-renamed copy");
    if (!Check(renamed.isValid() && duplicated.isValid() &&
               model.parent(renamed) == model.parent(duplicated),
               "rename or same-folder duplicate placement failed"))
        return 1;

    branch = Find(model, "Branch Office");
    if (!Check(model.deleteFolder(branch), "folder deletion failed"))
        return 1;
    unfiled = Find(model, "Unfiled");
    if (!Check(Find(model, "new").isValid() && unfiled.isValid(),
               "deleted folder lost profile"))
        return 1;

    AgentProfileRecord externalProfile = Profile("external", "198.51.100.4");
    model.setProfiles({coreProfile, externalProfile});
    if (!Check(!Find(model, "lab-01").isValid(), "deleted profile remained") ||
        !Check(Find(model, "external").isValid(), "external profile not reconciled"))
        return 1;

    DeviceTreeModel reloaded(file, {coreProfile, externalProfile});
    if (!Check(Find(reloaded, "Datacenter").isValid(), "model persistence failed"))
        return 1;

    AgentProfileRecord sameA = Profile("same", "192.0.2.10");
    AgentProfileRecord sameB = Profile("same", "192.0.2.11");
    DeviceTreeModel duplicates(temporary.filePath("same-names.conf"),
                               {sameA, sameB});
    QModelIndex sameFirst = Find(duplicates, "same");
    if (!Check(sameFirst.isValid(), "first duplicate-name profile missing") ||
        !Check(duplicates.rowCount(sameFirst.parent()) == 2,
               "same-name profiles were merged") ||
        !Check(duplicates.index(0, 0, sameFirst.parent()).data(
                   DeviceTreeModel::ProfileIdRole).toString() !=
               duplicates.index(1, 0, sameFirst.parent()).data(
                   DeviceTreeModel::ProfileIdRole).toString(),
               "same-name profile nodes lack distinct identities"))
        return 1;
    return 0;
}
