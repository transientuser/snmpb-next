#ifndef DISCOVERYSCANPLAN_H
#define DISCOVERYSCANPLAN_H

#include "agentprofilerepository.h"
#include "snmprequestconfig.h"
#include <QList>
#include <QMetaType>
#include <functional>

#include "snmptableoperation.h"

struct DiscoveryProbePlan
{
    QString startEndpoint;
    unsigned long long addressCount = 0;
    SnmpRequestConfig requestConfig;
    bool useSnmpV3Probe = false;
};

struct DiscoveryScanPlan
{
    AgentProfileRecord templateProfile;
    QString templateProfileId;
    QString destinationFolderId;
    QList<DiscoveryProbePlan> probes;
    int waitTimeSeconds = 1;
    bool enableIpv4 = true;
    bool enableIpv6 = true;
};

struct DiscoveryCandidate
{
    QString name;
    QString endpoint;
    QString protocol;
    QString uptime;
    QString contact;
    QString location;
    QString description;
};

enum class DiscoveryCompletion { Complete, Cancelled, TransportFailure };

struct DiscoveryResult
{
    DiscoveryCompletion completion = DiscoveryCompletion::TransportFailure;
    QList<DiscoveryCandidate> candidates;
    int completedWaitIntervals = 0;
};

struct DiscoveryProbeResult
{
    DiscoveryCompletion completion = DiscoveryCompletion::Complete;
    SnmpOperationStatus status = SnmpOperationStatus::Complete;
    QList<DiscoveryCandidate> candidates;
    int waitIntervals = 0;
};

class IDiscoveryProbeExecutor
{
public:
    virtual ~IDiscoveryProbeExecutor() = default;
    virtual DiscoveryProbeResult execute(const DiscoveryProbePlan &probe,
                                         int waitTimeSeconds) = 0;
};

class ScriptedDiscoveryProbeExecutor final : public IDiscoveryProbeExecutor
{
public:
    void append(const DiscoveryProbeResult &result);
    DiscoveryProbeResult execute(const DiscoveryProbePlan &probe,
                                 int waitTimeSeconds) override;
    const QList<DiscoveryProbePlan> &requests() const;
private:
    QList<DiscoveryProbeResult> script;
    QList<DiscoveryProbePlan> observed;
};

class DiscoveryOperation
{
public:
    explicit DiscoveryOperation(const DiscoveryScanPlan &plan);
    DiscoveryResult execute(IDiscoveryProbeExecutor &executor,
                            const SnmpCancellationToken &cancellation) const;
private:
    DiscoveryScanPlan scanPlan;
};

Q_DECLARE_METATYPE(DiscoveryCandidate)
Q_DECLARE_METATYPE(DiscoveryResult)

#endif
