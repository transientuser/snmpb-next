#include "graphservice.h"
#include "graphasyncrunner.h"

#include <QCoreApplication>
#include <QFile>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

static bool check(bool condition, const char *message)
{ if (!condition) std::cerr << "FAIL: " << message << '\n'; return condition; }

static AgentProfileRecord profile(const QString &id, const QString &name)
{ AgentProfileRecord p{}; p.profileId=id; p.name=name; p.v2=true; p.address="192.0.2.1"; p.port="161"; return p; }

static SnmpRequestConfig config(QString address="192.0.2.1")
{ SnmpRequestConfig c; c.version=SnmpRequestVersion::V2c; c.address=address; c.port="161"; c.readCommunity="captured"; return c; }

static SnmpTransportResult valueResult(int value)
{ SnmpTransportResult r; r.status=SnmpOperationStatus::Success; Vb vb(Oid("1.3.6.1.2.1.1.3.0")); vb.set_value(value); r.pdu += vb; return r; }

static bool persistence()
{
    QTemporaryDir dir; const QString file=dir.filePath("graphs.conf");
    { QSettings s(file,QSettings::IniFormat); s.beginWriteArray("graphs"); s.setArrayIndex(0);
      s.setValue("name","Legacy"); s.setValue("pollinterval",7); s.beginGroup("0");
      s.setValue("name","uptime"); s.setValue("agent","router"); s.setValue("proto",1);
      s.setValue("oid","1.3.6.1.2.1.1.3.0 (sysUpTime)"); s.endGroup(); s.endArray(); }
    const QByteArray before=[](const QString &p){ QFile f(p); f.open(QIODevice::ReadOnly); return f.readAll(); }(file);
    GraphRepository repo(file); auto graphs=repo.load({profile("stable","router")});
    bool ok=check(graphs.size()==1,"legacy graph loads") && check(graphs[0].series.size()==1,"legacy series loads")
      && check(graphs[0].series[0].profileId=="stable","unique legacy name migrates in memory")
      && check(graphs[0].series[0].numericOid=="1.3.6.1.2.1.1.3.0","OID canonicalized");
    QFile verify(file); verify.open(QIODevice::ReadOnly); ok &= check(verify.readAll()==before,"load does not rewrite");
    auto ambiguous=repo.load({profile("a","router"),profile("b","router")});
    ok &= check(ambiguous[0].series[0].health==GraphReferenceHealth::AmbiguousLegacyProfile,"ambiguous name retained");
    auto missing=repo.load({}); ok &= check(missing[0].series[0].health==GraphReferenceHealth::MissingProfile,"missing profile diagnosed");
    repo.save(graphs); auto stable=repo.load({profile("stable","renamed")});
    ok &= check(stable[0].series[0].profileId=="stable","stable identity survives rename");
    GraphService service(repo,{profile("stable","renamed")});
    GraphDefinition added; added.name="New"; added.pollIntervalSeconds=1; GraphSeriesDefinition series;
    series.seriesId="series"; series.profileId="stable"; series.numericOid="1.3.6.1"; added.series.append(series);
    ok &= check(service.create(added),"create") && check(service.graphs().size()==2,"create persisted");
    const QString copy=service.duplicate(service.graphs().last().graphId); ok &= check(!copy.isEmpty(),"duplicate");
    ok &= check(service.remove(copy),"delete");
    return ok;
}

static bool domainAndSampling()
{
    bool ok=true; GraphSeriesDefinition d; d.seriesId="s"; GraphSeriesState state(d,2);
    for(int i=0;i<3;++i){ GraphSample s; s.value=i; s.status=GraphSampleStatus::Valid; state.append(s); }
    ok &= check(state.samples().size()==2 && state.samples().first().value==1,"history bounded and ordered");
    const SnmpRequestConfig captured=config();
    QList<GraphSampleSeriesPlan> plans{GraphSampleSeriesPlan("one","1.3.6.1.2.1.1.3.0",SnmpRequestContext(captured,SnmpRequestOperation::Get))};
    ScriptedSnmpTransport transport(captured); transport.append(valueResult(42)); QList<ISnmpTransport*> transports{&transport};
    SnmpCancellationToken token; auto batch=GraphSamplingOperation(plans).execute(transports,token,QDateTime::fromSecsSinceEpoch(10));
    ok &= check(batch.samples.size()==1 && batch.samples[0].second.status==GraphSampleStatus::Valid && batch.samples[0].second.value==42,"numeric sample");
    ok &= check(transport.requests().size()==1 && transport.requests()[0].operation==SnmpTransportOperation::Get,"GET request recorded");
    SnmpRequestConfig changed=config("203.0.113.9"); Q_UNUSED(changed);
    ok &= check(transport.config().address=="192.0.2.1" && transport.config().readCommunity=="captured","request config captured");
    ScriptedSnmpTransport timeout(captured); SnmpTransportResult tr; tr.status=SnmpOperationStatus::Timeout; timeout.append(tr);
    transports={&timeout}; batch=GraphSamplingOperation(plans).execute(transports,token); ok &= check(batch.samples[0].second.status==GraphSampleStatus::Timeout,"timeout status");
    token.cancel(); batch=GraphSamplingOperation(plans).execute(transports,token); ok &= check(batch.samples[0].second.status==GraphSampleStatus::Cancelled,"cooperative cancellation");
    Vb text(Oid("1.2.3")); text.set_value("abc"); auto unsupported=GraphValueConverter::fromVarbind(text,QDateTime::currentDateTimeUtc());
    ok &= check(unsupported.status==GraphSampleStatus::UnsupportedValue,"non numeric unsupported");
    return ok;
}

static bool asynchronousLifecycle()
{
    const auto captured=config();
    QList<GraphSampleSeriesPlan> plans{GraphSampleSeriesPlan("one","1.3.6.1.2.1.1.3.0",SnmpRequestContext(captured,SnmpRequestOperation::Get))};
    auto transport=std::make_shared<ScriptedSnmpTransport>(captured); transport->append(valueResult(9));
    QList<std::shared_ptr<ISnmpTransport>> transports{transport};
    GraphAsyncRunner runner; QEventLoop loop; GraphSampleBatch received;
    QObject::connect(&runner,&GraphAsyncRunner::completed,&loop,[&](const GraphSampleBatch &batch){received=batch; loop.quit();});
    bool ok=check(runner.start(plans,transports),"async start"); loop.exec();
    ok &= check(received.samples.size()==1 && received.samples[0].second.value==9,"async value result"); QCoreApplication::processEvents();
    auto second=std::make_shared<ScriptedSnmpTransport>(captured); second->append(valueResult(10)); transports={second};
    ok &= check(runner.start(plans,transports),"async restart"); loop.exec();
    ok &= check(received.samples[0].second.value==10,"async restart result"); runner.stop();
    return ok;
}

int main(int argc,char **argv)
{ QCoreApplication app(argc,argv); return persistence() && domainAndSampling() && asynchronousLifecycle() ? 0 : 1; }
