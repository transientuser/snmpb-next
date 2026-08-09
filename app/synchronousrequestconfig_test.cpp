#include "agentrequestselection.h"
#include "snmprequestconfigadapter.h"

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
                           int retries, int timeout, int maxRepetitions,
                           int nonRepeaters)
{
    AgentProfileRecord record{};
    record.name = name;
    record.v1 = true;
    record.v2 = true;
    record.v3 = true;
    record.address = address;
    record.port = "10161";
    record.retries = retries;
    record.timeout = timeout;
    record.readcomm = "sync-read";
    record.writecomm = "sync-write";
    record.maxrepetitions = maxRepetitions;
    record.nonrepeaters = nonRepeaters;
    record.secname = "sync-user";
    record.seclevel = 2;
    record.contextname = "sync-context";
    record.contextengineid = "sync-engine";
    return record;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QList<AgentProfileRecord> profiles = {
        profile("original", "192.0.2.40", 4, 23, 30, 5),
        profile("replacement", "2001:db8::50", 1, 2, 7, 0)
    };

    AgentRequestSelection selection;
    check(AgentSelectionResolver::Resolve(profiles, "original", 1,
                                          &selection) ==
              AgentSelectionError::None,
          "synchronous selection resolves once");
    SnmpRequestConfig captured;
    check(selection.requestConfig(&captured),
          "synchronous request configuration is produced");

    // Simulate mutable profile/UI state changing while the operation runs.
    profiles[0] = profiles[1];
    AgentRequestSelection replacement;
    check(AgentSelectionResolver::Resolve(profiles, "replacement", 2,
                                          &replacement) ==
              AgentSelectionError::None,
          "replacement selection resolves independently");

    check(captured.version == SnmpRequestVersion::V2c &&
          captured.endpoint() == "192.0.2.40/10161",
          "captured protocol and IPv4 endpoint remain unchanged");
    check(captured.retries == 4 && captured.timeout == 23,
          "captured retries and timeout remain unchanged");
    check(captured.maxRepetitions == 30 && captured.nonRepeaters == 5,
          "captured bulk values remain unchanged");

    UdpAddress ipv4(captured.endpoint().toLatin1().data());
    CTarget v2Target(ipv4);
    ApplySnmpRequestConfig(captured, v2Target);
    check(ipv4.valid() && ipv4.get_port() == 10161 &&
          v2Target.get_version() == version2c &&
          v2Target.get_retry() == 4 && v2Target.get_timeout() == 2300,
          "captured SNMPv2c target configuration is retained");

    for (int protocol : {0, 2})
    {
        AgentRequestSelection versionSelection;
        check(AgentSelectionResolver::Resolve(
                  {profile("versioned", "2001:db8::60", 3, 11, 12, 1)},
                  "versioned", protocol, &versionSelection) ==
                  AgentSelectionError::None,
              "versioned synchronous selection resolves");
        SnmpRequestConfig config;
        versionSelection.requestConfig(&config);
        UdpAddress ipv6(config.endpoint().toLatin1().data());
        check(ipv6.valid() && ipv6.get_port() == 10161,
              "captured IPv6 endpoint constructs without network traffic");
        if (protocol == 0)
        {
            CTarget target(ipv6);
            ApplySnmpRequestConfig(config, target);
            check(target.get_version() == version1,
                  "captured SNMPv1 target configured");
        }
        else
        {
            UTarget target(ipv6);
            ApplySnmpRequestConfig(config, target);
            check(target.get_version() == version3,
                  "captured SNMPv3 target configured");
        }
    }

    if (failures == 0)
        QTextStream(stdout) << "All synchronous request configuration tests passed."
                            << Qt::endl;
    return failures == 0 ? 0 : 1;
}
