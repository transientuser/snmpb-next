#ifndef GRAPHREPOSITORY_H
#define GRAPHREPOSITORY_H

#include "agentprofilerepository.h"
#include "graphmodel.h"

class GraphRepository
{
public:
    explicit GraphRepository(const QString &filename);
    QList<GraphDefinition> load(const QList<AgentProfileRecord> &profiles = {}) const;
    void save(const QList<GraphDefinition> &graphs) const;
    static QString createId();
    static QString canonicalOid(const QString &legacyText);
private:
    QString path;
};

#endif
