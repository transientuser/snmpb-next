#ifndef GRAPHPLOTPRESENTER_H
#define GRAPHPLOTPRESENTER_H

#include "graphmodel.h"

#include <QHash>
#include <QPen>

class QwtPlot;
class QwtPlotCurve;

class GraphPlotPresenter
{
public:
    explicit GraphPlotPresenter(QwtPlot *plot);
    ~GraphPlotPresenter();
    void setTitle(const QString &title);
    void refresh(const QList<GraphSeriesState> &series);
    void clear();
    int curveCount() const;
    bool hasCurve(const QString &seriesId) const;
    QVector<QPointF> displayedPoints(const QString &seriesId) const;
    double yUpperBound() const;
    QString displayedYAxisLabel(double value) const;
    static QString formatNumericAxisValue(double value);
    static QPen penForLegacyStyle(int color, int width, int style);
private:
    QwtPlot *plotWidget;
    QHash<QString, QwtPlotCurve *> curves;
    QHash<QString, QVector<QPointF>> points;
};

#endif
