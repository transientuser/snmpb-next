#include "devicepane.h"

#include <QApplication>
#include <QFile>
#include <QTemporaryDir>
#include <QTreeView>

#include <iostream>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

QModelIndex Find(DeviceTreeModel *model, const QString &text,
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
    QModelIndex profileIndex = Find(pane.model(), "core-01");
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
    return 0;
}
