#ifndef GRAPHASYNCRUNNER_H
#define GRAPHASYNCRUNNER_H

#include "graphmodel.h"

#include <QObject>
#include <QThread>
#include <memory>

class GraphAsyncRunner : public QObject
{
    Q_OBJECT
public:
    explicit GraphAsyncRunner(QObject *parent = nullptr);
    ~GraphAsyncRunner() override;
    bool start(const QList<GraphSampleSeriesPlan> &plans,
               const QList<std::shared_ptr<ISnmpTransport>> &transports,
               const QDateTime &timestamp = QDateTime::currentDateTimeUtc());
    void stop();
    void wait();
    bool isRunning() const;
signals:
    void completed(const GraphSampleBatch &batch);
private:
    QThread *worker = nullptr;
    std::shared_ptr<SnmpCancellationToken> cancellation;
};

#endif
