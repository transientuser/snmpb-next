#ifndef DEVICETREE_H
#define DEVICETREE_H

#include <QList>
#include <QString>
#include <QStringList>
#include "agentprofilerepository.h"

struct DeviceFolderRecord
{
    QString id;
    QString parentId;
    QString name;
    int order = 0;
};

struct DeviceProfilePlacement
{
    QString id;
    QString parentId;
    QString profileId;
    QString legacyProfileName;
    int order = 0;
};

struct DeviceTreeState
{
    QList<DeviceFolderRecord> folders;
    QList<DeviceProfilePlacement> placements;
};

class DeviceTree
{
public:
    static DeviceTreeState Reconcile(const DeviceTreeState &state,
                                     const QList<AgentProfileRecord> &profiles);
    static QStringList UnfiledProfiles(const DeviceTreeState &state,
                                       const QList<AgentProfileRecord> &profiles);
    static bool DeleteFolder(DeviceTreeState *state, const QString &folderId);
    static bool MoveProfile(DeviceTreeState *state, const QString &profileId,
                            const QString &parentId, int order = -1);
};

#endif
