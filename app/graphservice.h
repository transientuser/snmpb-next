#ifndef GRAPHSERVICE_H
#define GRAPHSERVICE_H

#include "graphrepository.h"

class GraphService
{
public:
    GraphService(const GraphRepository &repository, const QList<AgentProfileRecord> &profiles);
    bool reload();
    const QList<GraphDefinition> &graphs() const;
    bool create(GraphDefinition graph);
    bool update(const GraphDefinition &graph);
    bool remove(const QString &graphId);
    QString duplicate(const QString &graphId);
    bool validate(const GraphDefinition &graph, QString *error = nullptr) const;
private:
    void persist();
    GraphRepository storage;
    QList<AgentProfileRecord> availableProfiles;
    QList<GraphDefinition> definitions;
};

#endif
