#include "snmpinstanceasyncrunner.h"

namespace {
class InstanceWorker : public QObject
{
    Q_OBJECT
public:
    InstanceWorker(const SnmpRequestContext &context, const Oid &root,
                   std::unique_ptr<ISnmpTransport> transport,
                   std::shared_ptr<SnmpCancellationToken> cancellation)
        : operation(context, root), transport(std::move(transport)),
          cancellation(std::move(cancellation)) {}
public slots:
    void run() { emit finished(operation.execute(*transport, *cancellation)); }
signals:
    void finished(const SnmpInstanceResult &result);
private:
    SnmpInstanceOperation operation;
    std::unique_ptr<ISnmpTransport> transport;
    std::shared_ptr<SnmpCancellationToken> cancellation;
};
}

SnmpInstanceAsyncRunner::SnmpInstanceAsyncRunner(QObject *parent) : QObject(parent)
{ qRegisterMetaType<SnmpInstanceResult>(); }
SnmpInstanceAsyncRunner::~SnmpInstanceAsyncRunner()
{
    cancel(); wait();
    if (thread) { delete thread; thread = nullptr; }
}
bool SnmpInstanceAsyncRunner::start(const SnmpRequestContext &context,
                                    const Oid &root,
                                    std::unique_ptr<ISnmpTransport> transport)
{
    if (isRunning() || !transport) return false;
    cancellation = std::make_shared<SnmpCancellationToken>();
    thread = new QThread;
    auto *worker = new InstanceWorker(context, root, std::move(transport), cancellation);
    worker->moveToThread(thread);
    connect(thread, &QThread::started, worker, &InstanceWorker::run);
    connect(worker, &InstanceWorker::finished, this,
            [this](const SnmpInstanceResult &result) { emit completed(result); });
    connect(worker, &InstanceWorker::finished, thread, &QThread::quit);
    connect(worker, &InstanceWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this]() {
        thread->deleteLater(); thread = nullptr; cancellation.reset();
    });
    thread->start(); return true;
}
void SnmpInstanceAsyncRunner::cancel() { if (cancellation) cancellation->cancel(); }
bool SnmpInstanceAsyncRunner::isRunning() const { return thread && thread->isRunning(); }
bool SnmpInstanceAsyncRunner::wait(unsigned long ms) { return !thread || thread->wait(ms); }

#include "snmpinstanceasyncrunner.moc"
