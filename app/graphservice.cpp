#include "graphservice.h"

GraphService::GraphService(const GraphRepository &repository, const QList<AgentProfileRecord> &profiles)
    : storage(repository), availableProfiles(profiles) { reload(); }
bool GraphService::reload() { definitions = storage.load(availableProfiles); return true; }
const QList<GraphDefinition> &GraphService::graphs() const { return definitions; }
bool GraphService::validate(const GraphDefinition &graph, QString *error) const
{
    if (graph.name.trimmed().isEmpty()) { if (error) *error = "Graph name is required"; return false; }
    if (graph.pollIntervalSeconds < 1 || graph.maximumSamples < 1)
    { if (error) *error = "Polling interval and history size must be positive"; return false; }
    for (const auto &series : graph.series)
        if (series.seriesId.isEmpty() || GraphRepository::canonicalOid(series.numericOid).isEmpty())
        { if (error) *error = "Each series requires an identity and numeric OID"; return false; }
    return true;
}
void GraphService::persist() { storage.save(definitions); }
bool GraphService::create(GraphDefinition graph)
{
    if (!validate(graph)) return false;
    if (graph.graphId.isEmpty()) graph.graphId = GraphRepository::createId();
    for (const auto &existing : definitions) if (existing.graphId == graph.graphId) return false;
    definitions.append(graph); persist(); return true;
}
bool GraphService::update(const GraphDefinition &graph)
{
    if (!validate(graph)) return false;
    for (auto &existing : definitions) if (existing.graphId == graph.graphId)
    { existing = graph; persist(); return true; }
    return false;
}
bool GraphService::remove(const QString &id)
{
    for (int i = 0; i < definitions.size(); ++i) if (definitions[i].graphId == id)
    { definitions.removeAt(i); persist(); return true; }
    return false;
}
QString GraphService::duplicate(const QString &id)
{
    for (auto graph : definitions) if (graph.graphId == id)
    {
        graph.graphId = GraphRepository::createId(); graph.name += " Copy";
        for (auto &series : graph.series) series.seriesId = GraphRepository::createId();
        definitions.append(graph); persist(); return graph.graphId;
    }
    return {};
}
