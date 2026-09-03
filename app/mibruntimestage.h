#ifndef MIBRUNTIMESTAGE_H
#define MIBRUNTIMESTAGE_H

#include "mibeffectiveplan.h"

struct MibRuntimeStageResult {
    bool success = false;
    bool reused = false;
    QString error;
    QString directory;
    MibProfileRuntimeConfiguration configuration;
    MibRuntimePathConfiguration paths;
};

class MibRuntimeStage final
{
public:
    static constexpr int SchemaVersion = 1;
    static MibRuntimeStageResult prepare(
        const MibProfileRuntimeConfiguration &configuration,
        const QString &cacheRoot = {});
};

#endif
