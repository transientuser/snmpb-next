#include "agentprofilerepository.h"

#include <qfile.h>
#include <qsettings.h>

AgentProfileRepository::AgentProfileRepository(const QString& filename)
    : filename(filename)
{
}

QList<AgentProfileRecord> AgentProfileRepository::Load() const
{
    QSettings settings(filename, QSettings::IniFormat);
    QList<AgentProfileRecord> profiles;
    int size = settings.beginReadArray("agents");
    for (int i = 0; i < size; i++)
    {
        settings.setArrayIndex(i);
        AgentProfileRecord profile;
        profile.name = settings.value("name").toString();
        profile.v1 = settings.value("v1").toBool();
        profile.v2 = settings.value("v2").toBool();
        profile.v3 = settings.value("v3").toBool();
        profile.address = settings.value("address").toString();
        profile.port = settings.value("port").toString();
        profile.retries = settings.value("retries").toInt();
        profile.timeout = settings.value("timeout").toInt();
        profile.readcomm = settings.value("readcomm").toString();
        profile.writecomm = settings.value("writecomm").toString();
        profile.maxrepetitions = settings.value("maxrepetitions").toInt();
        profile.nonrepeaters = settings.value("nonrepeaters").toInt();
        profile.secname = settings.value("secname").toString();
        profile.seclevel = settings.value("seclevel").toInt();
        profile.contextname = settings.value("contextname").toString();
        profile.contextengineid = settings.value("contextengineid").toString();
        profiles.append(profile);
    }
    settings.endArray();
    return profiles;
}

void AgentProfileRepository::Save(const QList<AgentProfileRecord>& profiles) const
{
    QSettings settings(filename, QSettings::IniFormat);
    settings.beginWriteArray("agents");
    settings.remove("");
    for (int i = 0; i < profiles.size(); i++)
    {
        const AgentProfileRecord& profile = profiles[i];
        settings.setArrayIndex(i);
        settings.setValue("name", profile.name);
        settings.setValue("v1", profile.v1);
        settings.setValue("v2", profile.v2);
        settings.setValue("v3", profile.v3);
        settings.setValue("address", profile.address);
        settings.setValue("port", profile.port);
        settings.setValue("retries", profile.retries);
        settings.setValue("timeout", profile.timeout);
        settings.setValue("readcomm", profile.readcomm);
        settings.setValue("writecomm", profile.writecomm);
        settings.setValue("maxrepetitions", profile.maxrepetitions);
        settings.setValue("nonrepeaters", profile.nonrepeaters);
        settings.setValue("secname", profile.secname);
        settings.setValue("seclevel", profile.seclevel);
        settings.setValue("contextname", profile.contextname);
        settings.setValue("contextengineid", profile.contextengineid);
    }
    settings.endArray();
    settings.sync();
}

QList<AgentProfileRecord> AgentProfileRepository::LoadOrCreateDefaults() const
{
    if (!QFile::exists(filename))
        Save(DefaultProfiles());
    return Load();
}

AgentProfileRecord AgentProfileRepository::DefaultProfile(
    const QString& name, const QString& address)
{
    AgentProfileRecord profile;
    profile.name = name;
    profile.v1 = true;
    profile.v2 = false;
    profile.v3 = false;
    profile.address = address;
    profile.port = "161";
    profile.retries = 1;
    profile.timeout = 3;
    profile.readcomm = "public";
    profile.writecomm = "private";
    profile.maxrepetitions = 10;
    profile.nonrepeaters = 0;
    profile.secname = "";
    profile.seclevel = 0;
    profile.contextname = "";
    profile.contextengineid = "";
    return profile;
}

QList<AgentProfileRecord> AgentProfileRepository::DefaultProfiles()
{
    QList<AgentProfileRecord> profiles;
    profiles.append(DefaultProfile("localhost", "127.0.0.1"));
    profiles.append(DefaultProfile("localhostipv6", "::1"));
    return profiles;
}
