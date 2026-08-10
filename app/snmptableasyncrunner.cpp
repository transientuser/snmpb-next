#include "snmptableasyncrunner.h"

namespace
{
class TableWorker : public QObject
{
    Q_OBJECT
public:
    TableWorker(const SnmpRequestContext &context, const SnmpTablePlan &plan,
                std::unique_ptr<ISnmpTransport> transport,
                std::shared_ptr<SnmpCancellationToken> cancellation)
        : operation(context, plan), transport(std::move(transport)),
          cancellation(std::move(cancellation)) {}
public slots:
    void run() { emit finished(operation.execute(*transport, *cancellation)); }
signals:
    void finished(const SnmpTableResult &result);
private:
    SnmpTableOperation operation;
    std::unique_ptr<ISnmpTransport> transport;
    std::shared_ptr<SnmpCancellationToken> cancellation;
};
}

SnmpTableAsyncRunner::SnmpTableAsyncRunner(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<SnmpTableResult>();
}

SnmpTableAsyncRunner::~SnmpTableAsyncRunner()
{
    cancel();
    wait();
    if (thread)
    {
        delete thread;
        thread = nullptr;
    }
}

bool SnmpTableAsyncRunner::start(const SnmpRequestContext &context,
                                 const SnmpTablePlan &plan,
                                 std::unique_ptr<ISnmpTransport> transport)
{
    if (isRunning() || !transport)
        return false;
    cancellation = std::make_shared<SnmpCancellationToken>();
    thread = new QThread;
    auto *worker = new TableWorker(context, plan, std::move(transport), cancellation);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &TableWorker::run);
    connect(worker, &TableWorker::finished, this,
            [this](const SnmpTableResult &result) { emit completed(result); });
    connect(worker, &TableWorker::finished, thread, &QThread::quit);
    connect(worker, &TableWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this]() {
        thread->deleteLater();
        thread = nullptr;
        cancellation.reset();
    });
    thread->start();
    return true;
}

void SnmpTableAsyncRunner::cancel()
{
    if (cancellation)
        cancellation->cancel();
}

bool SnmpTableAsyncRunner::isRunning() const
{
    return thread && thread->isRunning();
}

bool SnmpTableAsyncRunner::wait(unsigned long milliseconds)
{
    return !thread || thread->wait(milliseconds);
}

#include "snmptableasyncrunner.moc"
