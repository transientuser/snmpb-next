#include "discoveryscanplan.h"
#include <QCoreApplication>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{ if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
SnmpRequestConfig config(const QString &address, SnmpRequestVersion version)
{
    SnmpRequestConfig c; c.address = address; c.port = "161"; c.version = version;
    c.readCommunity = "captured-community"; c.securityName = "captured-user";
    c.retries = 2; c.timeout = 4; return c;
}
DiscoveryCandidate candidate(const QString &endpoint)
{ DiscoveryCandidate c; c.endpoint = endpoint; c.name = "device"; c.protocol = "V2c"; return c; }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    DiscoveryScanPlan plan;
    plan.templateProfileId = "template-stable-id";
    plan.destinationFolderId = "folder-a";
    plan.waitTimeSeconds = 3;
    plan.templateProfile.profileId = plan.templateProfileId;
    plan.templateProfile.readcomm = "inline-captured";
    plan.probes = {{"192.0.2.1/161", 1, config("192.0.2.1", SnmpRequestVersion::V2c), false},
                   {"192.0.2.2/161", 1, config("192.0.2.2", SnmpRequestVersion::V2c), false},
                   {"192.0.2.3/161", 1, config("192.0.2.3", SnmpRequestVersion::V3), true}};
    DiscoveryOperation operation(plan);
    plan.destinationFolderId = "folder-changed";
    plan.templateProfile.readcomm = "changed-inline";
    plan.probes[0].requestConfig.readCommunity = "changed-community";

    ScriptedDiscoveryProbeExecutor executor;
    executor.append({DiscoveryCompletion::Complete, SnmpOperationStatus::Success,
                     {candidate("192.0.2.1/161")}, 3});
    executor.append({DiscoveryCompletion::Complete, SnmpOperationStatus::Timeout, {}, 3});
    executor.append({DiscoveryCompletion::Complete, SnmpOperationStatus::SnmpError,
                     {candidate("192.0.2.3/161")}, 3});
    SnmpCancellationToken token;
    const DiscoveryResult result = operation.execute(executor, token);
    bool ok = check(result.completion == DiscoveryCompletion::Complete &&
                    result.candidates.size() == 2 && executor.requests().size() == 3,
                    "multiple probes continue after timeout/error") &&
              check(executor.requests()[0].startEndpoint == "192.0.2.1/161" &&
                    executor.requests()[1].startEndpoint == "192.0.2.2/161",
                    "probe request ordering") &&
              check(executor.requests()[0].requestConfig.readCommunity ==
                        "captured-community" &&
                    executor.requests()[0].requestConfig.timeout == 4,
                    "credentials/config captured once");

    DiscoveryScanPlan cancelPlan = plan;
    ScriptedDiscoveryProbeExecutor cancelledExecutor;
    cancelledExecutor.append({DiscoveryCompletion::Cancelled,
                              SnmpOperationStatus::Cancelled, {}, 0});
    const DiscoveryResult cancelled = DiscoveryOperation(cancelPlan).execute(
        cancelledExecutor, token);
    ok &= check(cancelled.completion == DiscoveryCompletion::Cancelled &&
                cancelledExecutor.requests().size() == 1,
                "cancellation stops new probes");
    SnmpCancellationToken preCancelled; preCancelled.cancel();
    ScriptedDiscoveryProbeExecutor unused;
    ok &= check(DiscoveryOperation(cancelPlan).execute(unused, preCancelled).completion ==
                    DiscoveryCompletion::Cancelled && unused.requests().isEmpty(),
                "pre-cancelled scan sends no requests");
    return ok ? 0 : 1;
}
