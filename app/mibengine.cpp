#include "mibengine.h"
#include <algorithm>
#include <mutex>
namespace { thread_local int engineDepth=0; }

class MibEngine::Private
{
public:
    std::recursive_mutex mutex;
    std::atomic<int> active{0};
    std::atomic<int> maximum{0};
    std::atomic<quint64> operationId{1};
};

MibEngine::MibEngine():d(std::make_unique<Private>()){}
MibEngine::~MibEngine()=default;
MibEngine &MibEngine::instance(){static MibEngine engine;return engine;}
MibEngine::Operation::Operation(MibEngine *owner):engine(owner)
{
    engine->d->mutex.lock();
    ++engineDepth;
    const int current=engineDepth==1?engine->d->active.fetch_add(1)+1:engine->d->active.load();
    int observed=engine->d->maximum.load();
    while(current>observed&&!engine->d->maximum.compare_exchange_weak(observed,current)){}
}
MibEngine::Operation::Operation(Operation &&other)noexcept:engine(other.engine){other.engine=nullptr;}
MibEngine::Operation::~Operation(){if(engine){if(engineDepth==1)engine->d->active.fetch_sub(1);--engineDepth;engine->d->mutex.unlock();}}
MibEngine::Operation MibEngine::beginOperation(const QString &name){Q_UNUSED(name);return Operation(this);}
int MibEngine::maximumConcurrentOperations()const{return d->maximum.load();}
void MibEngine::resetConcurrencyMetrics(){d->maximum.store(0);}
