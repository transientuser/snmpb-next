#include "agentprofilerepository.h"

#include <qcoreapplication.h>
#include <qfile.h>
#include <qsettings.h>
#include <qtemporarydir.h>

#include <iostream>

namespace
{
bool Check(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << std::endl;
    return condition;
}

bool Equal(const AgentProfileRecord& actual,
           const AgentProfileRecord& expected)
{
    return actual.name == expected.name &&
           actual.profileId == expected.profileId &&
           actual.v1 == expected.v1 &&
           actual.v2 == expected.v2 &&
           actual.v3 == expected.v3 &&
           actual.address == expected.address &&
           actual.port == expected.port &&
           actual.retries == expected.retries &&
           actual.timeout == expected.timeout &&
           actual.readcomm == expected.readcomm &&
           actual.writecomm == expected.writecomm &&
           actual.maxrepetitions == expected.maxrepetitions &&
           actual.nonrepeaters == expected.nonrepeaters &&
           actual.secname == expected.secname &&
           actual.seclevel == expected.seclevel &&
           actual.contextname == expected.contextname &&
           actual.contextengineid == expected.contextengineid;
}

QStringList ExpectedKeys(int count)
{
    static const QStringList fields = {
        "address", "contextengineid", "contextname", "id", "maxrepetitions",
        "name", "nonrepeaters", "port", "readcomm", "retries",
        "seclevel", "secname", "timeout", "v1", "v2", "v3",
        "writecomm"
    };
    QStringList keys;
    for (int i = 1; i <= count; ++i)
        for (const QString& field : fields)
            keys.append(QString("agents/%1/%2").arg(i).arg(field));
    keys.append("agents/size");
    keys.sort();
    return keys;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    QTemporaryDir temporaryDir;
    if (!Check(temporaryDir.isValid(), "Could not create temporary directory"))
        return 1;

    const QString defaultsFile = temporaryDir.filePath("defaults.conf");
    AgentProfileRepository defaultsRepository(defaultsFile);
    const QList<AgentProfileRecord> defaults =
        defaultsRepository.LoadOrCreateDefaults();
    if (!Check(defaults.size() == 2, "Expected two default profiles") ||
        !Check(defaults[0].name == "localhost", "IPv4 default name changed") ||
        !Check(!defaults[0].profileId.isEmpty() &&
               defaults[0].profileId != defaults[1].profileId,
               "Default profile IDs missing or duplicated") ||
        !Check(defaults[0].address == "127.0.0.1", "IPv4 default address changed") ||
        !Check(defaults[1].name == "localhostipv6", "IPv6 default name changed") ||
        !Check(defaults[1].address == "::1", "IPv6 default address changed") ||
        !Check(defaults[0].v1 && !defaults[0].v2 && !defaults[0].v3,
               "Default protocol flags changed") ||
        !Check(defaults[0].port == "161" && defaults[0].retries == 1 &&
               defaults[0].timeout == 3, "Default transport values changed") ||
        !Check(defaults[0].readcomm == "public" &&
               defaults[0].writecomm == "private", "Default communities changed") ||
        !Check(defaults[0].maxrepetitions == 10 &&
               defaults[0].nonrepeaters == 0, "Default bulk values changed"))
        return 1;

    AgentProfileRecord complete =
        AgentProfileRepository::DefaultProfile("router west/primary", "192.0.2.10");
    complete.v1 = false;
    complete.v2 = true;
    complete.v3 = true;
    complete.port = "10161";
    complete.retries = 7;
    complete.timeout = 19;
    complete.readcomm = "read community";
    complete.writecomm = "write community";
    complete.maxrepetitions = 42;
    complete.nonrepeaters = 3;
    complete.secname = "security-user";
    complete.seclevel = 2;
    complete.contextname = "context name";
    complete.contextengineid = "80001f8880e9630000d61ff449";

    AgentProfileRecord second =
        AgentProfileRepository::DefaultProfile("second", "2001:db8::20");
    const QString roundTripFile = temporaryDir.filePath("roundtrip.conf");
    AgentProfileRepository roundTripRepository(roundTripFile);
    roundTripRepository.Save({complete, second});
    const QList<AgentProfileRecord> reloaded = roundTripRepository.Load();
    if (!Check(reloaded.size() == 2, "Multiple profile count changed") ||
        !Check(Equal(reloaded[0], complete), "Complete profile did not round-trip") ||
        !Check(Equal(reloaded[1], second), "Profile ordering changed"))
        return 1;

    QSettings schema(roundTripFile, QSettings::IniFormat);
    QStringList actualKeys = schema.allKeys();
    actualKeys.sort();
    if (!Check(actualKeys == ExpectedKeys(2), "Saved agents.conf schema changed"))
        return 1;

    const QString legacyFile = temporaryDir.filePath("legacy.conf");
    QFile legacy(legacyFile);
    if (!Check(legacy.open(QIODevice::WriteOnly | QIODevice::Text),
               "Could not create legacy fixture"))
        return 1;
    legacy.write(
        "[agents]\n"
        "1\\name=legacy-router\n"
        "1\\v1=false\n"
        "1\\v2=true\n"
        "1\\v3=true\n"
        "1\\address=198.51.100.44\n"
        "1\\port=2161\n"
        "1\\retries=4\n"
        "1\\timeout=12\n"
        "1\\readcomm=legacy-read\n"
        "1\\writecomm=legacy-write\n"
        "1\\maxrepetitions=25\n"
        "1\\nonrepeaters=2\n"
        "1\\secname=legacy-user\n"
        "1\\seclevel=2\n"
        "1\\contextname=legacy-context\n"
        "1\\contextengineid=8000000001020304\n"
        "size=1\n");
    legacy.close();

    AgentProfileRepository legacyRepository(legacyFile);
    QList<AgentProfileRecord> legacyProfiles = legacyRepository.Load();
    if (!Check(legacyProfiles.size() == 1, "Legacy profile was not loaded") ||
        !Check(!legacyProfiles[0].profileId.isEmpty(),
               "Legacy profile did not receive an ID") ||
        !Check(legacyProfiles[0].name == "legacy-router", "Legacy name changed") ||
        !Check(!legacyProfiles[0].v1 && legacyProfiles[0].v2 &&
               legacyProfiles[0].v3, "Legacy protocols changed") ||
        !Check(legacyProfiles[0].readcomm == "legacy-read" &&
               legacyProfiles[0].writecomm == "legacy-write",
               "Legacy communities changed") ||
        !Check(legacyProfiles[0].secname == "legacy-user" &&
               legacyProfiles[0].seclevel == 2 &&
               legacyProfiles[0].contextname == "legacy-context" &&
               legacyProfiles[0].contextengineid == "8000000001020304",
               "Legacy SNMPv3 fields changed") ||
        !Check(legacyProfiles[0].retries == 4 &&
               legacyProfiles[0].timeout == 12 &&
               legacyProfiles[0].maxrepetitions == 25 &&
               legacyProfiles[0].nonrepeaters == 2,
               "Legacy timing or bulk values changed"))
        return 1;

    const QString migratedId = legacyProfiles[0].profileId;

    legacyRepository.Save(legacyProfiles);
    QSettings savedLegacy(legacyFile, QSettings::IniFormat);
    QStringList savedLegacyKeys = savedLegacy.allKeys();
    savedLegacyKeys.sort();
    if (!Check(savedLegacyKeys == ExpectedKeys(1),
               "Legacy save changed the agents.conf schema"))
        return 1;
    if (!Check(legacyRepository.Load()[0].profileId == migratedId,
               "Migrated profile ID was not stable"))
        return 1;

    const QString duplicateLegacyFile = temporaryDir.filePath("duplicates.conf");
    QFile duplicateLegacy(duplicateLegacyFile);
    if (!duplicateLegacy.open(QIODevice::WriteOnly | QIODevice::Text))
        return 1;
    duplicateLegacy.write(
        "[agents]\n"
        "1\\name=same\n1\\address=192.0.2.1\n"
        "2\\name=same\n2\\address=192.0.2.2\nsize=2\n");
    duplicateLegacy.close();
    AgentProfileRepository duplicateRepository(duplicateLegacyFile);
    const QList<AgentProfileRecord> duplicates = duplicateRepository.Load();
    if (!Check(duplicates.size() == 2, "Duplicate-name profiles were merged") ||
        !Check(duplicates[0].profileId != duplicates[1].profileId,
               "Duplicate-name profiles received the same ID"))
        return 1;

    return 0;
}
