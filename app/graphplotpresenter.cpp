#include "graphplotpresenter.h"

#include <qwt_date_scale_draw.h>
#include <qwt_date_scale_engine.h>
#include <qwt_legend.h>
#include <qwt_plot.h>
#include <qwt_plot_curve.h>
#include <qwt_plot_grid.h>
#include <qwt_scale_draw.h>
#include <qwt_symbol.h>
#include <qwt_text.h>

#include <limits>

namespace
{
class GraphNumericScaleDraw final : public QwtScaleDraw
{
public:
    QwtText label(double value) const override
    {
        return GraphPlotPresenter::formatNumericAxisValue(value);
    }
};
}

GraphPlotPresenter::GraphPlotPresenter(QwtPlot *plot) : plotWidget(plot)
{
    plotWidget->insertLegend(new QwtLegend(), QwtPlot::BottomLegend);
    auto *grid = new QwtPlotGrid();
    grid->setMajorPen(QPen(QColor(180, 180, 180), 0, Qt::DotLine));
    grid->attach(plotWidget);
    plotWidget->setAxisScaleEngine(QwtAxis::XBottom, new QwtDateScaleEngine(Qt::UTC));
    plotWidget->setAxisScaleDraw(QwtAxis::XBottom, new QwtDateScaleDraw(Qt::UTC));
    plotWidget->setAxisTitle(QwtAxis::XBottom, QObject::tr("Time"));
    plotWidget->setAxisScaleDraw(QwtAxis::YLeft, new GraphNumericScaleDraw());
    plotWidget->setAxisTitle(QwtAxis::YLeft, QObject::tr("Value"));
}

GraphPlotPresenter::~GraphPlotPresenter()
{
    for (auto *curve : curves) delete curve;
}

void GraphPlotPresenter::setTitle(const QString &title) { plotWidget->setTitle(title); }

QString GraphPlotPresenter::formatNumericAxisValue(double value)
{
    return QString::number(value, 'g', 12);
}

QPen GraphPlotPresenter::penForLegacyStyle(int color, int width, int style)
{
    static const QColor colors[] = {Qt::blue, Qt::red, Qt::darkGreen, Qt::magenta,
                                    Qt::cyan, Qt::darkYellow, Qt::black, Qt::gray};
    const QColor selected = color >= 0 && color < int(std::size(colors)) ? colors[color] : colors[0];
    const int selectedWidth = width >= 0 && width <= 10 ? qMax(1, width + 1) : 1;
    const Qt::PenStyle selectedStyle = style >= int(Qt::SolidLine) && style <= int(Qt::DashDotDotLine)
        ? Qt::PenStyle(style) : Qt::SolidLine;
    return QPen(selected, selectedWidth, selectedStyle);
}

void GraphPlotPresenter::refresh(const QList<GraphSeriesState> &states)
{
    QSet<QString> retained;
    for (const auto &state : states)
    {
        const auto &definition = state.definition();
        retained.insert(definition.seriesId);
        auto *curve = curves.value(definition.seriesId, nullptr);
        if (!curve)
        {
            curve = new QwtPlotCurve();
            curve->setSymbol(new QwtSymbol(QwtSymbol::Ellipse,QBrush(),QPen(Qt::black),QSize(5,5)));
            curve->attach(plotWidget);
            curves.insert(definition.seriesId, curve);
        }
        curve->setTitle(definition.label.isEmpty() ? definition.numericOid : definition.label);
        curve->setPen(penForLegacyStyle(definition.color, definition.width, definition.style));
        QVector<QPointF> values;
        for (const GraphSample &sample : state.samples())
        {
            const double y = sample.status == GraphSampleStatus::Valid
                ? sample.value : std::numeric_limits<double>::quiet_NaN();
            values.append(QPointF(sample.timestamp.toMSecsSinceEpoch(), y));
        }
        points.insert(definition.seriesId, values);
        curve->setSamples(values);
    }
    for (auto it = curves.begin(); it != curves.end();)
    {
        if (!retained.contains(it.key()))
        {
            delete it.value(); points.remove(it.key()); it = curves.erase(it);
        }
        else ++it;
    }
    plotWidget->setAxisAutoScale(QwtAxis::XBottom);
    plotWidget->setAxisAutoScale(QwtAxis::YLeft);
    plotWidget->replot();
}

void GraphPlotPresenter::clear()
{
    for (auto *curve : curves) curve->setSamples(QVector<QPointF>());
    points.clear(); plotWidget->replot();
}
int GraphPlotPresenter::curveCount() const { return curves.size(); }
bool GraphPlotPresenter::hasCurve(const QString &id) const { return curves.contains(id); }
QVector<QPointF> GraphPlotPresenter::displayedPoints(const QString &id) const { return points.value(id); }
double GraphPlotPresenter::yUpperBound() const { return plotWidget->axisScaleDiv(QwtAxis::YLeft).upperBound(); }
QString GraphPlotPresenter::displayedYAxisLabel(double value) const
{
    return plotWidget->axisScaleDraw(QwtAxis::YLeft)->label(value).text();
}
