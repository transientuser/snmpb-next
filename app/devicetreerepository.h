#ifndef DEVICETREEREPOSITORY_H
#define DEVICETREEREPOSITORY_H

#include "devicetree.h"

class DeviceTreeRepository
{
public:
    explicit DeviceTreeRepository(const QString &filename);
    DeviceTreeState Load() const;
    bool Save(const DeviceTreeState &state) const;
    QString fileName() const;

private:
    QString filename;
};

#endif
