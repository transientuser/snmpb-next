#ifndef GRAPHMODEL_H
#define GRAPHMODEL_H

#include "snmprequestcontext.h"
#include "snmptableoperation.h"

#include <QDateTime>
#include <QList>
#include <QString>

enum class GraphReferenceHealth { Resolved, MissingProfile, AmbiguousLegacyProfile, InvalidOid };
enum class GraphSampleStatus { Valid, Timeout, SnmpError, TransportError, UnsupportedValue, MissingValue, Cancelled };

struct GraphSeriesDefinition
{
    QString seriesId;
    QString profileId;
    QString legacyProfileName;
    int protocol = 0;
    QString numericOid;
    QString label;
    QString units;
    int color = 0;
    int width = 0;
    int style = 0;
    GraphReferenceHealth health = GraphReferenceHealth::Resolved;
};

struct GraphDefinition
{
    QString graphId;
    QString name;
    int pollIntervalSeconds = 5;
    int maximumSamples = 30;
    QList<GraphSeriesDefinition> series;
};

struct GraphSample
{
    QDateTime timestamp;
    double value = 0.0;
    GraphSampleStatus status = GraphSampleStatus::MissingValue;
    QString detail;
};

class GraphSeriesState
{
public:
    explicit GraphSeriesState(const GraphSeriesDefinition &definition = {}, int maximumSamples = 30);
    void append(const GraphSample &sample);
    void clear();
    const GraphSeriesDefinition &definition() const;
    const QList<GraphSample> &samples() const;
private:
    GraphSeriesDefinition seriesDefinition;
    int limit;
    QList<GraphSample> history;
};

struct GraphSampleSeriesPlan
{
    QString seriesId;
    QString numericOid;
    SnmpRequestContext context;
    GraphSampleSeriesPlan(const QString &id, const QString &oid, const SnmpRequestContext &requestContext);
};

struct GraphSampleBatch
{
    QDateTime timestamp;
    QList<QPair<QString, GraphSample>> samples;
};

Q_DECLARE_METATYPE(GraphSampleBatch)

class GraphValueConverter
{
public:
    static GraphSample fromVarbind(const Vb &varbind, const QDateTime &timestamp);
};

class GraphSamplingOperation
{
public:
    explicit GraphSamplingOperation(const QList<GraphSampleSeriesPlan> &series);
    GraphSampleBatch execute(const QList<ISnmpTransport *> &transports,
                             const SnmpCancellationToken &cancellation,
                             const QDateTime &timestamp = QDateTime::currentDateTimeUtc()) const;
private:
    QList<GraphSampleSeriesPlan> plans;
};

#endif
