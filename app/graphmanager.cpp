#include "graphmanager.h"
#include "mibenvironmentregistry.h"

#include "agentrequestselection.h"
#include "agentprofileservice.h"
#include "communitycredentialservice.h"
#include "graphplotpresenter.h"
#include "graphdefinitioneditor.h"
#include "graphlabelresolver.h"
#include "snmpb.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QMessageBox>
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
    editButton = new QPushButton(tr("Edit..."), app->MainUI()->Graph);
    duplicateButton = new QPushButton(tr("Duplicate"), app->MainUI()->groupBox);
    statusLabel = new QLabel(tr("Stopped"), page);
    controls->addWidget(startButton); controls->addWidget(stopButton);
    controls->addWidget(clearButton); controls->addWidget(statusLabel, 1);
    layout->addLayout(controls); layout->addWidget(plot, 1);
    app->MainUI()->GraphTab->addTab(page, tr("Live"));
    presenter = std::make_unique<GraphPlotPresenter>(plot);
    stopButton->setEnabled(false);
    if (auto *graphLayout = app->MainUI()->Graph->layout()) graphLayout->addWidget(editButton);
    if (auto *listLayout = qobject_cast<QGridLayout *>(app->MainUI()->groupBox->layout()))
        listLayout->addWidget(duplicateButton, 2, 0, 1, 2);
    app->MainUI()->GraphName->setReadOnly(true);
    app->MainUI()->GraphPollInterval->setReadOnly(true);
    app->MainUI()->PlotAdd->hide(); app->MainUI()->PlotDelete->hide();
    app->MainUI()->PlotL->setText(tr("Series"));
    app->MainUI()->GraphAdd->setText(tr("New..."));
    app->MainUI()->GraphDelete->setText(tr("Delete"));

    connect(app->MainUI()->GraphList, &QListWidget::currentItemChanged, this, &GraphManager::selectGraph);
    connect(app->MainUI()->GraphAdd, &QPushButton::clicked, this, &GraphManager::addGraph);
    connect(app->MainUI()->GraphDelete, &QPushButton::clicked, this, &GraphManager::deleteGraph);
    connect(editButton, &QPushButton::clicked, this, &GraphManager::editGraph);
    connect(duplicateButton, &QPushButton::clicked, this, &GraphManager::duplicateGraph);
    connect(app->MainUI()->GraphList, &QListWidget::itemDoubleClicked, this, [this] { editGraph(); });
    connect(startButton, &QPushButton::clicked, this, &GraphManager::start);
    connect(stopButton, &QPushButton::clicked, this, &GraphManager::stop);
    connect(clearButton, &QPushButton::clicked, this, &GraphManager::clear);
    connect(&timer, &QTimer::timeout, this, &GraphManager::poll);
    connect(&runner, &GraphAsyncRunner::completed, this, &GraphManager::sampled);
    refreshList(); refreshEditor();
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
    app->MainUI()->GraphDelete->setEnabled(enabled);
    editButton->setEnabled(enabled);
    duplicateButton->setEnabled(enabled);
    startButton->setEnabled(enabled && !pollingState.isRunning() && !graph->series.isEmpty());
    clearButton->setEnabled(enabled && !graph->series.isEmpty());
    if (!graph) { presenter->clear(); setStatus(tr("Select a graph")); return; }
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
    GraphDefinitionEditor editor(graph,app->AgentProfiles()->profiles(),app->MainUI()->GraphsTab);
    if(editor.exec()==QDialog::Accepted && service.create(editor.definition())) refreshList(graph.graphId);
}
void GraphManager::editGraph()
{
    const auto *graph=current(); if(!graph)return;
    stop();
    GraphDefinitionEditor editor(*graph,app->AgentProfiles()->profiles(),app->MainUI()->GraphsTab);
    if(editor.exec()!=QDialog::Accepted)return;
    const GraphDefinition draft=editor.definition();
    if(service.update(draft)){currentGraphId=draft.graphId;refreshList(draft.graphId);refreshEditor();}
}
void GraphManager::duplicateGraph()
{
    if(currentGraphId.isEmpty())return;
    const QString copyId=service.duplicate(currentGraphId);
    if(!copyId.isEmpty())refreshList(copyId);
}
void GraphManager::deleteGraph()
{
    if(currentGraphId.isEmpty())return;
    stop();
    if(QMessageBox::question(app->MainUI()->GraphsTab,tr("Delete Graph"),tr("Delete the selected graph?"))!=QMessageBox::Yes)return;
    if(service.remove(currentGraphId)){currentGraphId.clear();refreshList();}
}
void GraphManager::start()
{
    if (!current() || !pollingState.start()) return;
    startButton->setEnabled(false); stopButton->setEnabled(true); setStatus(tr("Running")); poll();
}
void GraphManager::stop()
{ timer.stop(); pollingState.stop(); runner.stop(); const auto *graph=current(); startButton->setEnabled(graph&&!graph->series.isEmpty()); stopButton->setEnabled(false); setStatus(tr("Stopped")); }
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
        plans.append(GraphSampleSeriesPlan(series.seriesId,series.numericOid,SnmpRequestContext(config,SnmpRequestOperation::Get,MibEnvironmentRegistry::active())));
        transports.append(std::make_shared<SnmpPlusTransport>(config));
    }
    if(plans.isEmpty()){pollingState.completeCycle();pollingState.stop();startButton->setEnabled(graph&&!graph->series.isEmpty());stopButton->setEnabled(false);setStatus(pendingErrors.isEmpty()?tr("No series"):tr("No runnable series: ")+pendingErrors.join(QStringLiteral("; ")));return;}
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
