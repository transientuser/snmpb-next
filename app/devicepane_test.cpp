#include "devicepane.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTreeView>
#include <QLineEdit>
#include <QToolBar>

#include <iostream>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

QModelIndex Find(QAbstractItemModel *model, const QString &text,
                 const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row)
    {
        QModelIndex index = model->index(row, 0, parent);
        if (index.data().toString() == text)
            return index;
        QModelIndex nested = Find(model, text, index);
        if (nested.isValid())
            return nested;
    }
    return {};
}
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    const QString agents = temporary.filePath("agents.conf");
    QFile agentsFile(agents);
    if (!agentsFile.open(QIODevice::WriteOnly))
        return 1;
    agentsFile.write("unchanged");
    agentsFile.close();

    AgentProfileRecord profile =
        AgentProfileRepository::DefaultProfile("core-01", "192.0.2.1");
    DevicePane pane(temporary.filePath("device-tree.conf"), {profile});
    if (!Check(pane.model() != nullptr && pane.treeView() != nullptr,
               "pane construction failed"))
        return 1;
    QToolBar *toolbar = pane.findChild<QToolBar *>();
    bool hasNewProfile = false, hasNewFolder = false;
    for (QAction *action : toolbar ? toolbar->actions() : QList<QAction *>()) {
        hasNewProfile |= action->text() == "New Profile" && !action->toolTip().isEmpty();
        hasNewFolder |= action->text() == "New Folder" && !action->toolTip().isEmpty();
    }
    if (!Check(hasNewProfile && hasNewFolder,
               "device toolbar does not distinguish profile and folder creation"))
        return 1;
    QModelIndex profileIndex = Find(pane.treeView()->model(), "core-01");
    if (!Check(profileIndex.isValid(), "profile missing from pane"))
        return 1;
    QString selected;
    QObject::connect(&pane, &DevicePane::profileSelected,
                     [&selected](const QString &id) { selected = id; });
    pane.treeView()->clicked(profileIndex);
    if (!Check(selected == profile.profileId,
               "pane emitted wrong profile identity"))
        return 1;

    QModelIndex folder = pane.model()->createFolder("Datacenter");
    if (!Check(folder.isValid() &&
               pane.model()->moveProfile(profile.profileId, folder),
               "pane hierarchy setup failed") ||
        !Check(Find(pane.model(), "Datacenter").isValid(),
               "folder missing from pane"))
        return 1;

    QFile verify(agents);
    if (!verify.open(QIODevice::ReadOnly))
        return 1;
    if (!Check(verify.readAll() == "unchanged", "opening pane changed agents.conf"))
        return 1;

    AgentProfileRecord lab =
        AgentProfileRepository::DefaultProfile("lab-switch", "2001:db8::10");
    pane.setProfiles({profile, lab});
    pane.filterEdit()->setText("2001:db8::10");
    if (!Check(Find(pane.treeView()->model(), "lab-switch").isValid(),
               "address filter did not retain matching profile") ||
        !Check(!Find(pane.treeView()->model(), "core-01").isValid(),
               "address filter retained nonmatching profile"))
        return 1;
    pane.filterEdit()->setText("core-01");
    if (!Check(Find(pane.treeView()->model(), "Datacenter").isValid(),
               "filter did not retain matching parent folder"))
        return 1;
    pane.setMetadata({{profile.profileId, "Primary aggregation router", {"Backbone"},
                       {"IF-MIB"}},
                      {lab.profileId, "Temporary validation device", {"QA"},
                       {"SNMPv2-MIB"}}});
    pane.filterEdit()->setText("backbone");
    if (!Check(Find(pane.treeView()->model(), "core-01").isValid() &&
               Find(pane.treeView()->model(), "Datacenter").isValid(),
               "tag filter or parent visibility failed"))
        return 1;
    pane.filterEdit()->setText("validation device");
    if (!Check(Find(pane.treeView()->model(), "lab-switch").isValid() &&
               !Find(pane.treeView()->model(), "core-01").isValid(),
               "notes filter/nonmatch exclusion failed"))
        return 1;
    pane.filterEdit()->setText("IF-MIB");
    if (!Check(Find(pane.treeView()->model(), "core-01").isValid(),
               "preferred MIB filter failed"))
        return 1;
    pane.filterEdit()->clear();
    QModelIndex proxyFolder = Find(pane.treeView()->model(), "Datacenter");
    pane.treeView()->setCurrentIndex(proxyFolder);
    QString requestedFolder;
    QObject::connect(&pane, &DevicePane::newProfileRequested,
                     [&requestedFolder](const QString &id) { requestedFolder = id; });
    QMetaObject::invokeMethod(&pane, "newProfile");
    if (!Check(!requestedFolder.isEmpty(), "new profile did not retain folder target"))
        return 1;
    pane.setProfiles({profile, lab});
    pane.placeCreatedProfile(lab.profileId);
    bool labPlaced = false;
    for (const DeviceProfilePlacement &placement : pane.model()->state().placements)
        if (placement.profileId == lab.profileId && placement.parentId == requestedFolder)
            labPlaced = true;
    if (!Check(labPlaced, "created profile was not placed in selected folder"))
        return 1;
    return 0;
}
