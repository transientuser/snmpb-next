#include "profilemetadataservice.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << "FAIL: " << message << '\n';
    return value;
}
QByteArray contents(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    bool ok = check(dir.isValid(), "temporary directory");
    const QString metadataFile = dir.filePath("profile-metadata.conf");
    const QString agentsFile = dir.filePath("agents.conf");
    const QString treeFile = dir.filePath("device-tree.conf");
    { QFile f(agentsFile); if (!f.open(QIODevice::WriteOnly)) return 1; f.write("agents"); }
    { QFile f(treeFile); if (!f.open(QIODevice::WriteOnly)) return 1; f.write("tree"); }
    const QByteArray agentsBefore = contents(agentsFile);
    const QByteArray treeBefore = contents(treeFile);

    ProfileMetadataService service(metadataFile);
    ok &= check(service.allMetadata().isEmpty() && !QFile::exists(metadataFile),
                "missing sidecar is empty and not created by reading");
    ProfileMetadataRecord first{"id-a", "Core router notes",
                                {" core ", "LAB", "Core", ""}};
    first.preferredMibs = {" IF-MIB ", "SNMPv2-MIB", "IF-MIB"};
    ok &= check(service.update(first), "metadata update");
    const ProfileMetadataRecord normalized = service.metadataForProfile("id-a");
    ok &= check(normalized.tags == QStringList({"core", "LAB"}),
                "case-insensitive first-spelling tag normalization");
    ok &= check(normalized.preferredMibs == QStringList({"IF-MIB", "SNMPv2-MIB"}),
                "preferred MIB normalization");
    ok &= check(contents(agentsFile) == agentsBefore && contents(treeFile) == treeBefore,
                "metadata operations changed another configuration file");

    ProfileMetadataService reloaded(metadataFile);
    ok &= check(reloaded.metadataForProfile("id-a").notes == "Core router notes" &&
                reloaded.metadataForProfile("id-a").tags == QStringList({"core", "LAB"}),
                "notes and tags round-trip");
    ok &= check(reloaded.metadataForProfile("id-a").preferredMibs ==
                    QStringList({"IF-MIB", "SNMPv2-MIB"}),
                "preferred MIBs round-trip");
    const QByteArray beforeCancel = contents(metadataFile);
    ProfileMetadataRecord cancelled = reloaded.metadataForProfile("id-a");
    cancelled.notes = "cancelled edit";
    cancelled.preferredMibs = {"CANCELLED-MIB"};
    ProfileMetadataRecord cancelledNew{"new-draft", "not persisted", {"draft"}};
    ok &= check(contents(metadataFile) == beforeCancel &&
                reloaded.metadataForProfile("new-draft").notes.isEmpty() &&
                !reloaded.metadataForProfile("id-a").preferredMibs.contains("CANCELLED-MIB"),
                "profile/metadata working copies or new-profile Cancel mutated storage");
    cancelled = reloaded.metadataForProfile("id-a");
    cancelled.notes = "accepted edit";
    ok &= check(reloaded.update(cancelled) &&
                reloaded.metadataForProfile("id-a").notes == "accepted edit",
                "metadata OK did not persist working copy");
    ok &= check(reloaded.copy("id-a", "id-b") &&
                reloaded.metadataForProfile("id-b").notes == "accepted edit" &&
                reloaded.metadataForProfile("id-b").preferredMibs.contains("IF-MIB"),
                "duplicate copies metadata to stable destination ID");
    ok &= check(reloaded.remove("id-a") &&
                reloaded.metadataForProfile("id-a").notes.isEmpty(),
                "delete removes metadata");

    const QByteArray beforeRead = contents(metadataFile);
    ProfileMetadataRepository repository(metadataFile);
    repository.load();
    ok &= check(contents(metadataFile) == beforeRead, "read rewrote metadata sidecar");

    const QString malformed = dir.filePath("malformed.conf");
    QSettings bad(malformed, QSettings::IniFormat);
    bad.setValue("schema/version", 1);
    bad.beginWriteArray("profiles");
    bad.setArrayIndex(0); bad.setValue("notes", "missing id");
    bad.setArrayIndex(1); bad.setValue("profileId", "valid");
    bad.setValue("notes", "kept");
    bad.setArrayIndex(2); bad.setValue("profileId", "valid");
    bad.setValue("notes", "duplicate ignored");
    bad.endArray(); bad.sync();
    const QList<ProfileMetadataRecord> loaded =
        ProfileMetadataRepository(malformed).load();
    ok &= check(loaded.size() == 1 && loaded.first().notes == "kept" &&
                loaded.first().preferredMibs.isEmpty(),
                "v1 schema upgrade and malformed records");
    return ok ? 0 : 1;
}
