#include "agentrequestselection.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {
int failures = 0;

void check(bool condition, const char *description)
{
    if (!condition)
    {
        QTextStream(stderr) << "FAIL: " << description << Qt::endl;
        ++failures;
    }
}

AgentProfileRecord profile(const QString &name, const QString &address,
                           bool v1, bool v2, bool v3)
{
    AgentProfileRecord record{};
    record.name = name;
    record.address = address;
    record.port = "161";
    record.v1 = v1;
    record.v2 = v2;
    record.v3 = v3;
    record.retries = 2;
    record.timeout = 5;
    record.readcomm = "public";
    record.writecomm = "private";
    record.maxrepetitions = 10;
    record.nonrepeaters = 0;
    record.secname = "user";
    record.seclevel = 1;
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QList<AgentProfileRecord> profiles = {
        profile("v1-only", "192.0.2.1", true, false, false),
        profile("v2-only", "192.0.2.2", false, true, false),
        profile("v3-ipv6", "2001:db8::3", false, false, true)
    };

    AgentRequestSelection selection;
    check(AgentSelectionResolver::Resolve(profiles, "v1-only", 0,
                                          &selection) ==
              AgentSelectionError::None,
          "valid SNMPv1 selection resolves");
    SnmpRequestConfig config;
    check(selection.requestConfig(&config) &&
          config.version == SnmpRequestVersion::V1 &&
          selection.profile.name == "v1-only",
          "SNMPv1 profile lookup and configuration are deterministic");

    check(AgentSelectionResolver::Resolve(profiles, "v2-only", 1,
                                          &selection) ==
              AgentSelectionError::None &&
          selection.requestConfig(&config) &&
          config.version == SnmpRequestVersion::V2c &&
          config.endpoint() == "192.0.2.2/161",
          "valid SNMPv2c IPv4 selection resolves");

    check(AgentSelectionResolver::Resolve(profiles, "v3-ipv6", 2,
                                          &selection) ==
              AgentSelectionError::None &&
          selection.requestConfig(&config) &&
          config.version == SnmpRequestVersion::V3 &&
          config.endpoint() == "2001:db8::3/161",
          "valid SNMPv3 IPv6 selection resolves");

    check(AgentSelectionResolver::Resolve(profiles, "v1-only", 1,
                                          &selection) ==
              AgentSelectionError::UnsupportedProtocol,
          "unsupported protocol is rejected using profile flags");
    check(AgentSelectionResolver::Resolve(profiles, "v2-only", 0,
                                          &selection) ==
              AgentSelectionError::UnsupportedProtocol &&
          AgentSelectionResolver::Resolve(profiles, "v2-only", 2,
                                          &selection) ==
              AgentSelectionError::UnsupportedProtocol,
          "all protocol support flags participate in validation");
    check(AgentSelectionResolver::Resolve(profiles, "missing", 0,
                                          &selection) ==
              AgentSelectionError::ProfileNotFound &&
          AgentSelectionResolver::Resolve({}, "v1-only", 0,
                                          &selection) ==
              AgentSelectionError::ProfileNotFound,
          "invalid profile name and empty profile list are rejected");
    check(AgentSelectionResolver::Resolve(profiles, "v1-only", -1,
                                          &selection) ==
              AgentSelectionError::InvalidProtocol &&
          AgentSelectionResolver::Resolve(profiles, "v1-only", 3,
                                          &selection) ==
              AgentSelectionError::InvalidProtocol,
          "invalid protocol identifiers are rejected");

    if (failures == 0)
        QTextStream(stdout) << "All Agent selection tests passed." << Qt::endl;
    return failures == 0 ? 0 : 1;
}
