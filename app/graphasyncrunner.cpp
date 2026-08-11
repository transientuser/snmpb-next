#include "graphasyncrunner.h"

GraphAsyncRunner::GraphAsyncRunner(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<GraphSampleBatch>();
}

GraphAsyncRunner::~GraphAsyncRunner()
{
    stop();
    if (worker) worker->wait();
}

bool GraphAsyncRunner::start(const QList<GraphSampleSeriesPlan> &plans,
                             const QList<std::shared_ptr<ISnmpTransport>> &transports,
                             const QDateTime &timestamp)
{
    if (isRunning() || plans.size() != transports.size()) return false;
    cancellation = std::make_shared<SnmpCancellationToken>();
    const auto token = cancellation;
    worker = QThread::create([this, plans, transports, timestamp, token]() {
        QList<ISnmpTransport *> raw;
        for (const auto &transport : transports) raw.append(transport.get());
        emit completed(GraphSamplingOperation(plans).execute(raw, *token, timestamp));
    });
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QThread::finished, this, [this]() { worker = nullptr; cancellation.reset(); });
    worker->start();
    return true;
}

void GraphAsyncRunner::stop() { if (cancellation) cancellation->cancel(); }
void GraphAsyncRunner::wait() { if (worker) worker->wait(); }
bool GraphAsyncRunner::isRunning() const { return worker && worker->isRunning(); }
