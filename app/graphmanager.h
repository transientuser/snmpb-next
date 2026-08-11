#ifndef GRAPHMANAGER_H
#define GRAPHMANAGER_H

#include "graphasyncrunner.h"
#include "graphservice.h"
#include "graphpollingstate.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <memory>

class GraphPlotPresenter;
class QwtPlot;
class QPushButton;
class QLabel;
class Snmpb;

class GraphManager : public QObject
{
    Q_OBJECT
public:
    explicit GraphManager(Snmpb *application);
    ~GraphManager() override;
private slots:
    void selectGraph();
    void addGraph();
    void deleteGraph();
    void addSeries();
    void deleteSeries();
    void applyDetails();
    void start();
    void stop();
    void clear();
    void poll();
    void sampled(const GraphSampleBatch &batch);
private:
    const GraphDefinition *current() const;
    void refreshList(const QString &selectId = {});
    void refreshEditor();
    void setStatus(const QString &text);
    Snmpb *app;
    GraphService service;
    QwtPlot *plot;
    std::unique_ptr<GraphPlotPresenter> presenter;
    QPushButton *startButton;
    QPushButton *stopButton;
    QPushButton *clearButton;
    QLabel *statusLabel;
    QTimer timer;
    GraphAsyncRunner runner;
    QHash<QString, GraphSeriesState> state;
    QString currentGraphId;
    GraphPollingState pollingState;
    QStringList pendingErrors;
};

#endif
