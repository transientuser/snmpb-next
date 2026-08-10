#include "preferredmibresolver.h"

#include "profilemetadatarepository.h"

PreferredMibResolution PreferredMibResolver::resolve(
    const QStringList &preferred, const QStringList &available,
    const QStringList &loaded)
{
    PreferredMibResolution result;
    for (const QString &name : ProfileMetadataRepository::normalizeMibs(preferred))
    {
        if (loaded.contains(name)) result.alreadyLoaded.append(name);
        else if (available.contains(name)) result.toLoad.append(name);
        else result.unavailable.append(name);
    }
    return result;
}
