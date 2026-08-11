#include "graphmanager.h"

#include "agentrequestselection.h"
#include "agentprofileservice.h"
#include "communitycredentialservice.h"
#include "graphplotpresenter.h"
#include "graphlabelresolver.h"
#include "snmpb.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <qwt_plot.h>

GraphManager::GraphManager(Snmpb *application)
    : app(application), service(GraphRepository(app->GetGraphsConfigFile()),
                                app->AgentProfiles()->profiles()), plot(new QwtPlot)
{
    auto *page = new QWidget(app->MainUI()->GraphTab);
    auto *layout = new QVBoxLayout(page);
    auto *controls = new QHBoxLayout;
    startButton = new QPushButton(tr("Start"), page);
    stopButton = new QPushButton(tr("Stop"), page);
    clearButton = new QPushButton(tr("Clear"), page);
    statusLabel = new QLabel(tr("Stopped"), page);
    controls->addWidget(startButton); controls->addWidget(stopButton);
    controls->addWidget(clearButton); controls->addWidget(statusLabel, 1);
    layout->addLayout(controls); layout->addWidget(plot, 1);
    app->MainUI()->GraphTab->addTab(page, tr("Live"));
    presenter = std::make_unique<GraphPlotPresenter>(plot);
    stopButton->setEnabled(false);

    connect(app->MainUI()->GraphList, &QListWidget::currentItemChanged, this, &GraphManager::selectGraph);
    connect(app->MainUI()->GraphAdd, &QPushButton::clicked, this, &GraphManager::addGraph);
    connect(app->MainUI()->GraphDelete, &QPushButton::clicked, this, &GraphManager::deleteGraph);
    connect(app->MainUI()->PlotAdd, &QPushButton::clicked, this, &GraphManager::addSeries);
    connect(app->MainUI()->PlotDelete, &QPushButton::clicked, this, &GraphManager::deleteSeries);
    connect(app->MainUI()->GraphName, &QLineEdit::editingFinished, this, &GraphManager::applyDetails);
    connect(app->MainUI()->GraphPollInterval, &QSpinBox::editingFinished, this, &GraphManager::applyDetails);
    connect(startButton, &QPushButton::clicked, this, &GraphManager::start);
    connect(stopButton, &QPushButton::clicked, this, &GraphManager::stop);
    connect(clearButton, &QPushButton::clicked, this, &GraphManager::clear);
    connect(&timer, &QTimer::timeout, this, &GraphManager::poll);
    connect(&runner, &GraphAsyncRunner::completed, this, &GraphManager::sampled);
    refreshList();
}

GraphManager::~GraphManager() { disconnect(&runner,nullptr,this,nullptr); stop(); runner.wait(); }
const GraphDefinition *GraphManager::current() const
{
    for (const auto &graph : service.graphs())
        if (graph.graphId == currentGraphId) return &graph;
    return nullptr;
}
void GraphManager::refreshList(const QString &selectId)
{
    auto *list=app->MainUI()->GraphList; list->clear();
    for (const auto &graph : service.graphs())
    { auto *item=new QListWidgetItem(graph.name,list); item->setData(Qt::UserRole,graph.graphId);
      if (graph.graphId==(selectId.isEmpty()?currentGraphId:selectId)) list->setCurrentItem(item); }
    if (!list->currentItem() && list->count()) list->setCurrentRow(0);
}
void GraphManager::selectGraph()
{
    stop(); auto *item=app->MainUI()->GraphList->currentItem();
    currentGraphId=item?item->data(Qt::UserRole).toString():QString(); refreshEditor();
}
void GraphManager::refreshEditor()
{
    const auto *graph=current(); const bool enabled=graph;
    app->MainUI()->Graph->setEnabled(enabled); app->MainUI()->PlotList->clear();
    if (!graph) return;
    app->MainUI()->GraphName->setText(graph->name);
    app->MainUI()->GraphPollInterval->setValue(graph->pollIntervalSeconds);
    presenter->setTitle(graph->name); state.clear();
    for (const auto &series : graph->series)
    { auto *item=new QListWidgetItem(series.label.isEmpty()?series.numericOid:series.label,app->MainUI()->PlotList);
      item->setData(Qt::UserRole,series.seriesId); state.insert(series.seriesId,GraphSeriesState(series,graph->maximumSamples)); }
    presenter->refresh(state.values());
}
void GraphManager::addGraph()
{
    GraphDefinition graph; graph.name=tr("New Graph"); graph.graphId=GraphRepository::createId();
    if (service.create(graph)) refreshList(graph.graphId);
}
void GraphManager::deleteGraph()
{ if (!currentGraphId.isEmpty() && service.remove(currentGraphId)) { currentGraphId.clear(); refreshList(); } }
void GraphManager::applyDetails()
{
    auto *graph=current(); if (!graph) return; GraphDefinition draft=*graph;
    draft.name=app->MainUI()->GraphName->text(); draft.pollIntervalSeconds=app->MainUI()->GraphPollInterval->value();
    if (service.update(draft)) { currentGraphId=draft.graphId; refreshList(draft.graphId); presenter->setTitle(draft.name); }
}
void GraphManager::addSeries()
{
    auto *graph=current(); if (!graph) return;
    QDialog dialog(app->MainUI()->GraphsTab); dialog.setWindowTitle(tr("Add Graph Series")); QFormLayout form(&dialog);
    QComboBox profiles, protocols; QLineEdit oid, label;
    for (const auto &profile : app->AgentProfiles()->profiles()) profiles.addItem(profile.name,profile.profileId);
    protocols.addItem("SNMPv1",0); protocols.addItem("SNMPv2c",1); protocols.addItem("SNMPv3",2);
    form.addRow(tr("Agent Profile"),&profiles); form.addRow(tr("Protocol"),&protocols);
    form.addRow(tr("Numeric OID"),&oid); form.addRow(tr("Label"),&label);
    QDialogButtonBox buttons(QDialogButtonBox::Ok|QDialogButtonBox::Cancel); form.addRow(&buttons);
    connect(&buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(&buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec()!=QDialog::Accepted) return;
    GraphDefinition draft=*graph; GraphSeriesDefinition series; series.seriesId=GraphRepository::createId();
    series.profileId=profiles.currentData().toString(); series.legacyProfileName=profiles.currentText();
    series.protocol=protocols.currentData().toInt(); series.numericOid=GraphRepository::canonicalOid(oid.text());
    series.label=label.text().isEmpty()?GraphLabelResolver::displayLabel(series.numericOid):label.text();
    draft.series.append(series); if (service.update(draft)) refreshEditor();
}
void GraphManager::deleteSeries()
{
    auto *graph=current(); auto *item=app->MainUI()->PlotList->currentItem(); if (!graph||!item) return;
    GraphDefinition draft=*graph; const QString id=item->data(Qt::UserRole).toString();
    for(int i=0;i<draft.series.size();++i) if(draft.series[i].seriesId==id){draft.series.removeAt(i);break;}
    if(service.update(draft)) refreshEditor();
}
void GraphManager::start()
{
    if (!current() || !pollingState.start()) return;
    startButton->setEnabled(false); stopButton->setEnabled(true); setStatus(tr("Running")); poll();
}
void GraphManager::stop()

{ timer.stop(); pollingState.stop(); runner.stop(); startButton->setEnabled(true); stopButton->setEnabled(false); setStatus(tr("Stopped")); }
void GraphManager::clear()
{ for(auto it=state.begin();it!=state.end();++it) it.value().clear(); presenter->refresh(state.values()); }
void GraphManager::poll()
{
    auto *graph=current(); if(!graph||!pollingState.beginCycle()) return;
    QList<GraphSampleSeriesPlan> plans; QList<std::shared_ptr<ISnmpTransport>> transports; pendingErrors.clear();
    for(const auto &series:graph->series)
    {
        AgentRequestSelection selection;
        if(AgentSelectionResolver::ResolveById(app->AgentProfiles()->profiles(),series.profileId,series.protocol,&selection)!=AgentSelectionError::None){pendingErrors.append((series.label.isEmpty()?series.numericOid:series.label)+tr(": unresolved profile or protocol"));continue;}
        if(series.protocol<2){selection.credentials=app->CommunityCredentials()->resolve(selection.profile).values;selection.hasResolvedCredentials=true;}
        SnmpRequestConfig config; if(!selection.requestConfig(&config)){pendingErrors.append((series.label.isEmpty()?series.numericOid:series.label)+tr(": invalid request configuration"));continue;}
        plans.append(GraphSampleSeriesPlan(series.seriesId,series.numericOid,SnmpRequestContext(config,SnmpRequestOperation::Get)));
        transports.append(std::make_shared<SnmpPlusTransport>(config));
    }
    if(plans.isEmpty()){pollingState.completeCycle();setStatus(pendingErrors.isEmpty()?tr("No series"):pendingErrors.join(QStringLiteral("; ")));return;}
    if(!runner.start(plans,transports)) pollingState.completeCycle();
}
void GraphManager::sampled(const GraphSampleBatch &batch)
{
    pollingState.completeCycle(); bool cancelled=false;
    for(const auto &entry:batch.samples){if(state.contains(entry.first))state[entry.first].append(entry.second);if(entry.second.status!=GraphSampleStatus::Valid){
        QString label=entry.first; if(state.contains(entry.first)){const auto &definition=state[entry.first].definition();label=definition.label.isEmpty()?definition.numericOid:definition.label;}
        QString reason=GraphValueConverter::statusText(entry.second.status); if(!entry.second.detail.isEmpty())reason+=QStringLiteral(" ")+entry.second.detail;
        pendingErrors.append(label+QStringLiteral(": ")+reason); cancelled|=entry.second.status==GraphSampleStatus::Cancelled;}}
    presenter->refresh(state.values());
    if(cancelled&&!pollingState.isRunning())setStatus(tr("Cancelled"));
    else if(pendingErrors.isEmpty())setStatus(tr("Running — latest poll successful"));
    else setStatus(tr("Running — some series have errors: ")+pendingErrors.join(QStringLiteral("; ")));
    if(pollingState.isRunning()&&current()) timer.start(current()->pollIntervalSeconds*1000);
}
void GraphManager::setStatus(const QString &text){statusLabel->setText(text);}
