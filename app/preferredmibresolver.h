#ifndef PREFERREDMIBRESOLVER_H
#define PREFERREDMIBRESOLVER_H

#include <QStringList>

struct PreferredMibResolution
{
    QStringList alreadyLoaded;
    QStringList toLoad;
    QStringList unavailable;
};

class PreferredMibResolver
{
public:
    static PreferredMibResolution resolve(const QStringList &preferred,
                                          const QStringList &available,
                                          const QStringList &loaded);
};

#endif
