#include "graphservice.h"
#include "graphasyncrunner.h"
#include "graphplotpresenter.h"
#include "graphpollingstate.h"
#include "graphlabelresolver.h"
#include "graphdefinitioneditor.h"

#include <QApplication>
#include <QFile>
#include <QEventLoop>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
#include <qwt_plot.h>
#include "smi.h"

static bool check(bool condition, const char *message)
{ if (!condition) std::cerr << "FAIL: " << message << '\n'; return condition; }

static AgentProfileRecord profile(const QString &id, const QString &name)
{ AgentProfileRecord p{}; p.profileId=id; p.name=name; p.v2=true; p.address="192.0.2.1"; p.port="161"; return p; }

static SnmpRequestConfig config(QString address="192.0.2.1")
{ SnmpRequestConfig c; c.version=SnmpRequestVersion::V2c; c.address=address; c.port="161"; c.readCommunity="captured"; return c; }

static SnmpTransportResult valueResult(int value)
{ SnmpTransportResult r; r.status=SnmpOperationStatus::Success; Vb vb(Oid("1.3.6.1.2.1.1.3.0")); vb.set_value(value); r.pdu += vb; return r; }

template<typename T> static Vb numericVb(const T &value)
{ Vb vb(Oid("1.3.6.1.2.1.1.3.0")); vb.set_value(value); return vb; }

static SnmpTransportResult vbResult(const Vb &vb)
{ SnmpTransportResult r; r.status=SnmpOperationStatus::Success; r.pdu+=vb; return r; }

static bool persistence()
{
    QTemporaryDir dir; const QString file=dir.filePath("graphs.conf");
    { QSettings s(file,QSettings::IniFormat); s.beginWriteArray("graphs"); s.setArrayIndex(0);
      s.setValue("name","Legacy"); s.setValue("pollinterval",7); s.beginGroup("0");
      s.setValue("name","uptime"); s.setValue("agent","router"); s.setValue("proto",1);
      s.setValue("oid","1.3.6.1.2.1.1.3.0 (sysUpTime)"); s.endGroup(); s.endArray(); }
    const QByteArray before=[](const QString &p){ QFile f(p); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }(file);
    GraphRepository repo(file); auto graphs=repo.load({profile("stable","router")});
    bool ok=check(graphs.size()==1,"legacy graph loads") && check(graphs[0].series.size()==1,"legacy series loads")
      && check(graphs[0].series[0].profileId=="stable","unique legacy name migrates in memory")
      && check(graphs[0].series[0].protocol==1,"series protocol persists")
      && check(graphs[0].series[0].numericOid=="1.3.6.1.2.1.1.3.0","OID canonicalized");
    QFile verify(file); ok &= check(verify.open(QIODevice::ReadOnly),"open graph file for verification");
    ok &= check(verify.readAll()==before,"load does not rewrite");
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
    ok &= check(plans[0].context.config().version==SnmpRequestVersion::V2c
      && plans[0].context.config().address=="192.0.2.1"
      && plans[0].context.config().readCommunity=="captured","protocol and request config captured");
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
    const QDateTime now=QDateTime::currentDateTimeUtc();
    const auto integer=GraphValueConverter::fromVarbind(numericVb(SnmpInt32(-123)),now);
    const auto gauge=GraphValueConverter::fromVarbind(numericVb(Gauge32(100000)),now);
    const auto counter32=GraphValueConverter::fromVarbind(numericVb(Counter32(4000000000UL)),now);
    const auto counter64=GraphValueConverter::fromVarbind(numericVb(Counter64(pp_uint64(4294967419ULL))),now);
    const auto ticks=GraphValueConverter::fromVarbind(numericVb(TimeTicks(5000000UL)),now);
    ok &= check(integer.status==GraphSampleStatus::Valid&&integer.value==-123,"INTEGER typed conversion")
      && check(gauge.status==GraphSampleStatus::Valid&&gauge.value==100000,"Gauge32 typed conversion")
      && check(counter32.status==GraphSampleStatus::Valid&&counter32.value==4000000000.0,"Counter32 typed conversion")
      && check(counter64.status==GraphSampleStatus::Valid&&counter64.value==4294967419.0,"Counter64 typed conversion")
      && check(ticks.status==GraphSampleStatus::Valid&&ticks.value==5000000.0,"large TimeTicks typed conversion");
    GraphSeriesState tickState(d,3); tickState.append(ticks); ok &= check(tickState.samples().first().value==5000000.0,"TimeTicks retained");
    ok &= check(GraphRepository::canonicalOid("1.3.6.1.2.1.1.3.0 (sysUpTime)")=="1.3.6.1.2.1.1.3.0","scalar instance preserved");
    return ok;
}

static bool mixedSeriesSampling()
{
    const auto captured=config();
    QList<GraphSampleSeriesPlan> plans{
      GraphSampleSeriesPlan("first","1.3.6.1.2.1.1.3.0",SnmpRequestContext(captured,SnmpRequestOperation::Get)),
      GraphSampleSeriesPlan("second","1.3.6.1.2.1.1.5.0",SnmpRequestContext(captured,SnmpRequestOperation::Get))};
    auto run=[&](SnmpTransportResult first,SnmpTransportResult second){
      ScriptedSnmpTransport a(captured),b(captured);a.append(first);b.append(second);QList<ISnmpTransport*> transports{&a,&b};SnmpCancellationToken token;
      return GraphSamplingOperation(plans).execute(transports,token);};
    SnmpTransportResult timeout;timeout.status=SnmpOperationStatus::Timeout;
    const auto success=vbResult(numericVb(TimeTicks(123456UL)));
    bool ok=true; auto batch=run(success,success);ok&=check(batch.samples[0].second.status==GraphSampleStatus::Valid&&batch.samples[1].second.status==GraphSampleStatus::Valid,"both series succeed");
    batch=run(success,timeout);ok&=check(batch.samples[0].second.status==GraphSampleStatus::Valid&&batch.samples[1].second.status==GraphSampleStatus::Timeout,"first succeeds second fails");
    batch=run(timeout,success);ok&=check(batch.samples[0].second.status==GraphSampleStatus::Timeout&&batch.samples[1].second.status==GraphSampleStatus::Valid,"first fails second succeeds");
    batch=run(timeout,timeout);ok&=check(batch.samples[0].second.status==GraphSampleStatus::Timeout&&batch.samples[1].second.status==GraphSampleStatus::Timeout,"both series fail");
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

static bool presentation()
{
    QwtPlot plot; GraphPlotPresenter presenter(&plot);
    GraphSeriesDefinition first; first.seriesId="first"; first.numericOid="1.2.3"; first.label="Symbolic";
    first.color=1; first.width=2; first.style=int(Qt::DashLine);
    GraphSeriesState firstState(first,3);
    GraphSample a; a.timestamp=QDateTime::fromSecsSinceEpoch(10); a.status=GraphSampleStatus::Valid; a.value=4;
    GraphSample invalid=a; invalid.timestamp=QDateTime::fromSecsSinceEpoch(11); invalid.status=GraphSampleStatus::Timeout;
    firstState.append(a); firstState.append(invalid);
    GraphSeriesDefinition second; second.seriesId="second"; second.numericOid="1.2.4";
    GraphSeriesState secondState(second,3); GraphSample b=a; b.value=8; secondState.append(b);
    presenter.refresh({firstState,secondState}); const auto points=presenter.displayedPoints("first");
    bool ok=check(presenter.curveCount()==2,"presenter multiple curves")
      && check(presenter.hasCurve("first"),"curve mapped by series id")
      && check(points.size()==2 && points[0].x()<points[1].x(),"timestamp ordering")
      && check(qIsNaN(points[1].y()),"invalid sample is a gap");
    const QPen pen=GraphPlotPresenter::penForLegacyStyle(1,2,int(Qt::DashLine));
    ok &= check(pen.color()==Qt::red && pen.width()==3 && pen.style()==Qt::DashLine,"legacy style mapping");
    const QPen fallback=GraphPlotPresenter::penForLegacyStyle(999,999,999);
    ok &= check(fallback.color()==Qt::blue && fallback.width()==1 && fallback.style()==Qt::SolidLine,"invalid style fallback");
    presenter.refresh({firstState}); ok &= check(presenter.curveCount()==1 && !presenter.hasCurve("second"),"series removal");
    presenter.clear(); ok &= check(presenter.displayedPoints("first").isEmpty(),"clear presentation");
    GraphSeriesState scaleState(first,10); for(double value:{100.0,1000.0,100000.0,5000000.0}){GraphSample sample=a;sample.value=value;sample.timestamp=sample.timestamp.addSecs(scaleState.samples().size());scaleState.append(sample);}
    presenter.refresh({scaleState}); ok &= check(presenter.yUpperBound()>=5000000.0,"Y axis autoscales above large TimeTicks");
    const QString largeA=GraphPlotPresenter::formatNumericAxisValue(2540540000.0);
    const QString largeB=GraphPlotPresenter::formatNumericAxisValue(2540540100.0);
    ok &= check(largeA!=largeB,"large nearby Y-axis values have distinct labels")
      && check(presenter.displayedYAxisLabel(2540540000.0)!=presenter.displayedYAxisLabel(2540540100.0),"presenter installs precise Y-axis scale draw")
      && check(GraphPlotPresenter::formatNumericAxisValue(42.5)=="42.5","ordinary numeric Y-axis label remains concise");
    return ok;
}

static bool pollingAndTransactionalEditing()
{
    GraphPollingState state; bool ok=check(state.start(),"polling start")
      && check(!state.start(),"duplicate start rejected")
      && check(state.beginCycle(),"first cycle")
      && check(!state.beginCycle(),"overlap rejected");
    state.stop(); state.completeCycle(); ok &= check(!state.beginCycle(),"stop prevents later cycle");
    ok &= check(state.start() && state.beginCycle(),"polling restart"); state.completeCycle(); state.stop();

    QTemporaryDir dir; GraphRepository repository(dir.filePath("transaction.conf"));
    GraphService service(repository,{}); GraphDefinition graph; graph.graphId="g"; graph.name="Original";
    ok &= check(service.create(graph),"transaction fixture");
    GraphDefinition working=service.graphs().first(); working.name="Cancelled";
    ok &= check(service.graphs().first().name=="Original","cancel working copy isolation");
    working.name="Applied"; ok &= check(service.update(working) && service.graphs().first().name=="Applied","working copy apply");

    GraphDefinition editable=service.graphs().first(); editable.pollIntervalSeconds=17; editable.maximumSamples=240;
    GraphSeriesDefinition styled; styled.seriesId="series-stable"; styled.profileId="profile-stable";
    styled.protocol=1; styled.numericOid="1.3.6.1.2.1.1.3.0"; styled.color=4; styled.width=5; styled.style=int(Qt::DashDotLine);
    editable.series={styled};
    GraphDefinitionEditor editor(editable,{profile("profile-stable","router")});
    const GraphDefinition editorCopy=editor.definition();
    ok &= check(editorCopy.pollIntervalSeconds==17&&editorCopy.maximumSamples==240,"editor exposes polling and history")
      && check(editorCopy.series.first().profileId=="profile-stable"&&editorCopy.series.first().numericOid=="1.3.6.1.2.1.1.3.0","editor retains stable profile and numeric OID")
      && check(editorCopy.series.first().color==4&&editorCopy.series.first().width==5&&editorCopy.series.first().style==int(Qt::DashDotLine),"editor retains series style")
      && check(service.graphs().first().name=="Applied","editor working copy does not mutate service before OK");
    ok &= check(service.update(editorCopy)&&service.graphs().first().maximumSamples==240,"accepted editor definition commits through service");
    return ok;
}

static bool mibLabels()
{
    smiInit("snmpb-graph-test");
    const QByteArray path=QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf").toLocal8Bit();
    smiSetPath(path.constData()); smiLoadModule("SNMPv2-MIB");
    const QString symbolic=GraphLabelResolver::displayLabel("1.3.6.1.2.1.1.3");
    const QString fallback=GraphLabelResolver::displayLabel("1.3.6.1.4.1.999999.1");
    const bool ok=check(symbolic.contains("sysUpTime"),"symbolic MIB label")
      && check(fallback=="1.3.6.1.4.1.999999.1","numeric MIB fallback");
    smiExit(); return ok;
}

int main(int argc,char **argv)
{ QApplication app(argc,argv); return persistence() && domainAndSampling()
    && mixedSeriesSampling() && asynchronousLifecycle() && presentation()
    && pollingAndTransactionalEditing() && mibLabels() ? 0 : 1; }
