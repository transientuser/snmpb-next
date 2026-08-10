#include "discoveryscanplan.h"

void ScriptedDiscoveryProbeExecutor::append(const DiscoveryProbeResult &result)
{ script.append(result); }

DiscoveryProbeResult ScriptedDiscoveryProbeExecutor::execute(
    const DiscoveryProbePlan &probe, int)
{
    observed.append(probe);
    if (script.isEmpty()) return {DiscoveryCompletion::TransportFailure,
                                  SnmpOperationStatus::TransportFailure, {}, 0};
    return script.takeFirst();
}

const QList<DiscoveryProbePlan> &ScriptedDiscoveryProbeExecutor::requests() const
{ return observed; }

DiscoveryOperation::DiscoveryOperation(const DiscoveryScanPlan &plan) : scanPlan(plan) {}

DiscoveryResult DiscoveryOperation::execute(
    IDiscoveryProbeExecutor &executor,
    const SnmpCancellationToken &cancellation) const
{
    DiscoveryResult result;
    result.completion = DiscoveryCompletion::Complete;
    for (const DiscoveryProbePlan &probe : scanPlan.probes)
    {
        if (cancellation.isCancelled())
        {
            result.completion = DiscoveryCompletion::Cancelled;
            return result;
        }
        const DiscoveryProbeResult probeResult = executor.execute(
            probe, scanPlan.waitTimeSeconds);
        result.candidates.append(probeResult.candidates);
        result.completedWaitIntervals += probeResult.waitIntervals;
        if (probeResult.completion == DiscoveryCompletion::Cancelled)
        {
            result.completion = DiscoveryCompletion::Cancelled;
            return result;
        }
        if (probeResult.completion == DiscoveryCompletion::TransportFailure)
            result.completion = DiscoveryCompletion::TransportFailure;
    }
    return result;
}
