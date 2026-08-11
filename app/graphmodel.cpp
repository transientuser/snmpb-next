#include "graphmodel.h"

GraphSeriesState::GraphSeriesState(const GraphSeriesDefinition &definition, int maximumSamples)
    : seriesDefinition(definition), limit(qMax(1, maximumSamples)) {}

void GraphSeriesState::append(const GraphSample &sample)
{
    history.append(sample);
    while (history.size() > limit) history.removeFirst();
}
void GraphSeriesState::clear() { history.clear(); }
const GraphSeriesDefinition &GraphSeriesState::definition() const { return seriesDefinition; }
const QList<GraphSample> &GraphSeriesState::samples() const { return history; }

GraphSampleSeriesPlan::GraphSampleSeriesPlan(const QString &id, const QString &oid,
                                             const SnmpRequestContext &requestContext)
    : seriesId(id), numericOid(oid), context(requestContext) {}

GraphSample GraphValueConverter::fromVarbind(const Vb &vb, const QDateTime &timestamp)
{
    GraphSample sample;
    sample.timestamp = timestamp;
    const int syntax = vb.get_syntax();
    switch (syntax)
    {
    case sNMP_SYNTAX_INT32:
    {
        long value=0;
        if(vb.get_value(value)==SNMP_CLASS_SUCCESS){sample.value=double(value);sample.status=GraphSampleStatus::Valid;}
        else sample.status=GraphSampleStatus::UnsupportedValue;
        return sample;
    }
    case sNMP_SYNTAX_GAUGE32:
    case sNMP_SYNTAX_CNTR32:
    case sNMP_SYNTAX_TIMETICKS:
    {
        unsigned long value=0;
        if(vb.get_value(value)==SNMP_CLASS_SUCCESS){sample.value=double(value);sample.status=GraphSampleStatus::Valid;}
        else sample.status=GraphSampleStatus::UnsupportedValue;
        return sample;
    }
    case sNMP_SYNTAX_CNTR64:
    {
        pp_uint64 value=0;
        if(vb.get_value(value)==SNMP_CLASS_SUCCESS){sample.value=double(value);sample.status=GraphSampleStatus::Valid;}
        else sample.status=GraphSampleStatus::UnsupportedValue;
        return sample;
    }
    case sNMP_SYNTAX_NOSUCHOBJECT:
    case sNMP_SYNTAX_NOSUCHINSTANCE:
    case sNMP_SYNTAX_ENDOFMIBVIEW:
        sample.status = GraphSampleStatus::MissingValue;
        sample.detail = QString::fromLatin1(vb.get_printable_value());
        return sample;
    default:
        sample.status = GraphSampleStatus::UnsupportedValue;
        sample.detail = QString::fromLatin1(vb.get_printable_value());
        return sample;
    }
}

QString GraphValueConverter::statusText(GraphSampleStatus status)
{
    switch(status){
    case GraphSampleStatus::Valid:return QStringLiteral("valid");
    case GraphSampleStatus::Timeout:return QStringLiteral("timeout");
    case GraphSampleStatus::SnmpError:return QStringLiteral("SNMP error");
    case GraphSampleStatus::TransportError:return QStringLiteral("transport error");
    case GraphSampleStatus::UnsupportedValue:return QStringLiteral("unsupported value type");
    case GraphSampleStatus::MissingValue:return QStringLiteral("missing value");
    case GraphSampleStatus::Cancelled:return QStringLiteral("cancelled");}
    return QStringLiteral("unknown error");
}

GraphSamplingOperation::GraphSamplingOperation(const QList<GraphSampleSeriesPlan> &series) : plans(series) {}

GraphSampleBatch GraphSamplingOperation::execute(const QList<ISnmpTransport *> &transports,
                                                  const SnmpCancellationToken &cancellation,
                                                  const QDateTime &timestamp) const
{
    GraphSampleBatch batch;
    batch.timestamp = timestamp;
    for (int i = 0; i < plans.size(); ++i)
    {
        GraphSample sample;
        sample.timestamp = timestamp;
        if (cancellation.isCancelled()) sample.status = GraphSampleStatus::Cancelled;
        else if (i >= transports.size() || !transports[i]) sample.status = GraphSampleStatus::TransportError;
        else
        {
            SnmpTransportRequest request;
            request.operation = SnmpTransportOperation::Get;
            request.pdu += Vb(Oid(plans[i].numericOid.toLatin1().constData()));
            const SnmpTransportResult result = transports[i]->execute(request);
            if (result.status == SnmpOperationStatus::Success && result.pdu.get_vb_count() > 0)
            {
                Vb vb;
                result.pdu.get_vb(vb, 0);
                sample = GraphValueConverter::fromVarbind(vb, timestamp);
            }
            else if (result.status == SnmpOperationStatus::Timeout) sample.status = GraphSampleStatus::Timeout;
            else if (result.status == SnmpOperationStatus::SnmpError) { sample.status = GraphSampleStatus::SnmpError; sample.detail=QString::number(result.snmpErrorStatus); }
            else if (result.status == SnmpOperationStatus::Cancelled) sample.status = GraphSampleStatus::Cancelled;
            else { sample.status = GraphSampleStatus::TransportError; sample.detail=QString::number(result.transportStatus); }
        }
        batch.samples.append(qMakePair(plans[i].seriesId, sample));
    }
    return batch;
}
