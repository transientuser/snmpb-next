#ifndef DISCOVERYDESTINATION_H
#define DISCOVERYDESTINATION_H

#include "devicetreerepository.h"

#include <QObject>
#include <QSettings>

struct DeviceFolderChoice
{
    QString folderId;
    QString displayPath;
};

class DiscoveryDestinationSettings
{
public:
    static QString load(QSettings &settings);
    static void save(QSettings &settings, const QString &folderId);
    static QString resolve(const QString &savedFolderId,
                           const QList<DeviceFolderChoice> &folders);
};

class DeviceTreePlacementService : public QObject
{
    Q_OBJECT
public:
    explicit DeviceTreePlacementService(const QString &fileName,
                                        QObject *parent = nullptr);
    QList<DeviceFolderChoice> folderChoices() const;
    bool placeProfile(const QString &profileId, const QString &folderId);

signals:
    void placementChanged(const QString &profileId);

private:
    DeviceTreeRepository repository;
};

#endif
