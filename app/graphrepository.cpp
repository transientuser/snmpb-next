#include "graphrepository.h"

#include "snmp_pp/oid.h"
#include <QSettings>
#include <QUuid>

GraphRepository::GraphRepository(const QString &filename) : path(filename) {}
QString GraphRepository::createId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }

QString GraphRepository::canonicalOid(const QString &text)
{
    QString candidate = text.trimmed();
    const int space = candidate.indexOf(' ');
    if (space > 0) candidate = candidate.left(space);
    Oid oid(candidate.toLatin1().constData());
    return oid.valid() ? QString::fromLatin1(oid.get_printable()) : QString();
}

QList<GraphDefinition> GraphRepository::load(const QList<AgentProfileRecord> &profiles) const
{
    QSettings settings(path, QSettings::IniFormat);
    QList<GraphDefinition> result;
    const int count = settings.beginReadArray("graphs");
    for (int i = 0; i < count; ++i)
    {
        settings.setArrayIndex(i);
        GraphDefinition graph;
        graph.graphId = settings.value("graphId").toString();
        if (graph.graphId.isEmpty()) graph.graphId = createId();
        graph.name = settings.value("name").toString();
        graph.pollIntervalSeconds = settings.value("pollinterval", 5).toInt();
        graph.maximumSamples = settings.value("maximumSamples", 30).toInt();
        int seriesCount = 0;
        const QStringList groups = settings.childGroups();
        for (const QString &group : groups) if (group.toInt() || group == "0") ++seriesCount;
        for (int j = 0; j < seriesCount; ++j)
        {
            settings.beginGroup(QString::number(j));
            GraphSeriesDefinition series;
            series.seriesId = settings.value("seriesId").toString();
            if (series.seriesId.isEmpty()) series.seriesId = createId();
            series.label = settings.value("name").toString();
            series.profileId = settings.value("profileId").toString();
            series.legacyProfileName = settings.value("agent").toString();
            series.protocol = settings.value("proto", 0).toInt();
            series.numericOid = canonicalOid(settings.value("oid").toString());
            series.color = settings.value("color", 0).toInt();
            series.width = settings.value("width", 0).toInt();
            series.style = settings.value("shape", 0).toInt();
            if (series.numericOid.isEmpty()) series.health = GraphReferenceHealth::InvalidOid;
            if (series.profileId.isEmpty())
            {
                QList<QString> matches;
                for (const auto &profile : profiles)
                    if (profile.name == series.legacyProfileName) matches.append(profile.profileId);
                if (matches.size() == 1) series.profileId = matches.first();
                else series.health = matches.isEmpty() ? GraphReferenceHealth::MissingProfile
                                                       : GraphReferenceHealth::AmbiguousLegacyProfile;
            }
            settings.endGroup();
            graph.series.append(series);
        }
        result.append(graph);
    }
    settings.endArray();
    return result;
}

void GraphRepository::save(const QList<GraphDefinition> &graphs) const
{
    QSettings settings(path, QSettings::IniFormat);
    settings.clear();
    settings.setValue("schemaVersion", 2);
    settings.beginWriteArray("graphs");
    for (int i = 0; i < graphs.size(); ++i)
    {
        settings.setArrayIndex(i);
        const auto &graph = graphs[i];
        settings.setValue("graphId", graph.graphId);
        settings.setValue("name", graph.name);
        settings.setValue("pollinterval", graph.pollIntervalSeconds);
        settings.setValue("maximumSamples", graph.maximumSamples);
        for (int j = 0; j < graph.series.size(); ++j)
        {
            settings.beginGroup(QString::number(j));
            const auto &series = graph.series[j];
            settings.setValue("seriesId", series.seriesId);
            settings.setValue("name", series.label);
            settings.setValue("profileId", series.profileId);
            settings.setValue("agent", series.legacyProfileName);
            settings.setValue("proto", series.protocol);
            settings.setValue("oid", series.numericOid);
            settings.setValue("color", series.color);
            settings.setValue("width", series.width);
            settings.setValue("shape", series.style);
            settings.endGroup();
        }
    }
    settings.endArray();
    settings.sync();
}
