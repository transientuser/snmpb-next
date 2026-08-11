#ifndef GRAPHDEFINITIONEDITOR_H
#define GRAPHDEFINITIONEDITOR_H

#include "agentprofilerepository.h"
#include "graphmodel.h"

#include <QDialog>

class QLineEdit;
class QListWidget;
class QSpinBox;

class GraphDefinitionEditor : public QDialog
{
    Q_OBJECT
public:
    GraphDefinitionEditor(const GraphDefinition &definition,
                          const QList<AgentProfileRecord> &profiles,
                          QWidget *parent = nullptr);
    GraphDefinition definition() const;
    void setWorkingDefinition(const GraphDefinition &definition);

private slots:
    void addSeries();
    void editSeries();
    void deleteSeries();
    void updateSeriesActions();

private:
    bool editSeriesDefinition(GraphSeriesDefinition *series);
    void refreshSeries(const QString &selectedId = {});
    GraphDefinition working;
    QList<AgentProfileRecord> availableProfiles;
    QLineEdit *nameEdit;
    QSpinBox *pollInterval;
    QSpinBox *historySize;
    QListWidget *seriesList;
    QPushButton *editSeriesButton;
    QPushButton *deleteSeriesButton;
};

#endif
