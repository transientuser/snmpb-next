#include "mibprofile.h"
#include "mibeffectiveplan.h"
#include "mibtreemodel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>
#include <algorithm>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

bool containsModule(const QAbstractItemModel &model, const QString &module,
                    const QModelIndex &parent = {})
{
    for (int row=0; row<model.rowCount(parent); ++row) {
        const QModelIndex index = model.index(row, 0, parent);
        if (index.data(MibTreeModel::ModuleRole).toString() == module ||
            containsModule(model, module, index)) return true;
    }
    return false;
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    bool ok = true;
    QTemporaryDir temp;
    const QString path = temp.filePath("mib-profiles.json");
    MibProfileService service{MibProfileRepository(path)};
    ok &= check(service.profiles().size() == 2, "two built-in profiles");
    ok &= check(service.find(MibProfileDefinitions::allId())->type == MibProfileType::All,
                "All MIBs built-in");
    ok &= check(!service.remove(MibProfileDefinitions::allId()), "All cannot be deleted");
    ok &= check(!service.remove(MibProfileDefinitions::standardsId()), "Standards cannot be deleted");
    ok &= check(MibProfileDefinitions::validCurrentId("deleted", service.profiles()) ==
                MibProfileDefinitions::allId(), "missing current profile falls back to All");

    QString error;
    const QString id = service.create("Vendor family", &error);
    ok &= check(!id.isEmpty() && error.isEmpty(), "create custom profile");
    MibProfileRecord changed = *service.find(id);
    changed.explicitModules = {"VENDOR-A", "MISSING-LATER"};
    changed.includeStandardBase = true;
    changed.providerPins.insert("VENDOR-A", {"C:/MIBs/vendor-a.mib", QString(64, 'a')});
    ok &= check(service.update(changed, &error), "update members/base");
    ok &= check(service.rename(id, "Vendor renamed", &error), "rename custom");
    const QString duplicate = service.duplicate(id, "Vendor copy", &error);
    ok &= check(!duplicate.isEmpty() && duplicate != id, "duplicate stable identity");

    MibProfileService reloaded{MibProfileRepository(path)};
    const MibProfileRecord *savedPointer = reloaded.find(id);
    ok &= check(savedPointer && savedPointer->name == "Vendor renamed", "rename persisted");
    const MibProfileRecord saved = savedPointer ? *savedPointer : MibProfileRecord{};
    ok &= check(saved.explicitModules.contains("MISSING-LATER"),
                "missing identity survives reload");
    ok &= check(saved.includeStandardBase, "standard base survives reload");
    ok &= check(saved.providerPins.value("VENDOR-A").canonicalPath == "C:/MIBs/vendor-a.mib" &&
                saved.providerPins.value("VENDOR-A").sha256 == QString(64, 'a'),
                "provider pin path and authoritative hash survive reload");
    ok &= check(reloaded.remove(duplicate, &error), "delete custom");

    MibTreeNodeRecord root; root.name = "root";
    MibTreeNodeRecord vendor; vendor.name = "vendor"; vendor.moduleName = "VENDOR-A";
    MibTreeNodeRecord unrelated; unrelated.name = "other"; unrelated.moduleName = "OTHER-MIB";
    root.children = {vendor, unrelated};
    MibTreeModel tree; tree.setSnapshot(root);
    MibTreeFilterModel filter; filter.setSourceModel(&tree);
    filter.setVisibleModules({"VENDOR-A"});
    ok &= check(containsModule(filter, "VENDOR-A") && !containsModule(filter, "OTHER-MIB"),
                "profile filters visible tree");
    filter.showAllModules();
    ok &= check(containsModule(filter, "VENDOR-A") && containsModule(filter, "OTHER-MIB"),
                "All MIBs restores tree without mutating inventory");
    filter.setVisibleModules({});
    ok &= check(!containsModule(filter, "VENDOR-A") && !containsModule(filter, "OTHER-MIB"),
                "empty custom profile shows no modules");

    const QString foldersRoot = temp.filePath("MIBs");
    QDir().mkpath(foldersRoot + "/Standards/IETF");
    QDir().mkpath(foldersRoot + "/Unassigned/Misc");
    QDir().mkpath(foldersRoot + "/Extreme/VOSS/Base");
    QDir().mkpath(foldersRoot + "/Extreme/EXOS");
    QDir().mkpath(foldersRoot + "/Aruba/AOS-CX");
    QDir().mkpath(foldersRoot + "/EmptyRoot");
    QDir().mkpath(foldersRoot + "/SimpleProduct");
    QFile standardMib(foldersRoot + "/Standards/IETF/BASE-SMI.mib");
    standardMib.open(QIODevice::WriteOnly);
    standardMib.write("BASE-SMI DEFINITIONS ::= BEGIN\nEND\n"); standardMib.close();
    QFile unassignedMib(foldersRoot + "/Unassigned/Misc/MISC-MIB.mib");
    unassignedMib.open(QIODevice::WriteOnly);
    unassignedMib.write("MISC-MIB DEFINITIONS ::= BEGIN\nEND\n"); unassignedMib.close();
    QFile simpleMib(foldersRoot + "/SimpleProduct/simple.mib");
    simpleMib.open(QIODevice::WriteOnly);
    simpleMib.write("SIMPLE-IDENTITY DEFINITIONS ::= BEGIN\nEND\n"); simpleMib.close();
    QFile folderMib(foldersRoot + "/Extreme/VOSS/Base/synro.mib");
    folderMib.open(QIODevice::WriteOnly);
    folderMib.write("SYNOPTICS-ROOT-MIB DEFINITIONS ::= BEGIN\nEND\n"
                    "SECOND-IDENTITY-MIB DEFINITIONS ::= BEGIN\nEND\n");
    folderMib.close();
    QFile exosMib(foldersRoot + "/Extreme/EXOS/shared.mib");
    exosMib.open(QIODevice::WriteOnly);
    exosMib.write("SYNOPTICS-ROOT-MIB DEFINITIONS ::= BEGIN\nEND\n"); exosMib.close();
    ok &= check(reloaded.refreshAutomaticProfiles(foldersRoot, &error), "automatic profiles refresh");
    const auto folderProfiles = reloaded.profiles();
    const auto voss = std::find_if(folderProfiles.begin(), folderProfiles.end(), [](const auto &profile) {
        return profile.name == "Extreme VOSS" && profile.type == MibProfileType::Folder;
    });
    const auto aruba = std::find_if(folderProfiles.begin(), folderProfiles.end(), [](const auto &profile) {
        return profile.name == "Aruba AOS-CX" && profile.type == MibProfileType::Folder;
    });
    ok &= check(voss != folderProfiles.end() &&
                voss->explicitModules.contains("SYNOPTICS-ROOT-MIB") &&
                voss->explicitModules.contains("SECOND-IDENTITY-MIB"),
                "recursive folder scan maps filename to every declared identity");
    ok &= check(aruba != folderProfiles.end() && aruba->explicitModules.isEmpty(),
                "empty product directory remains a zero-member profile");
    const QStringList forbidden{"Standards", "IETF", "Unassigned", "Misc", "Extreme", "Base", "EmptyRoot"};
    for (const QString &name : forbidden)
        ok &= check(std::none_of(folderProfiles.begin(), folderProfiles.end(),
            [&name](const auto &profile) { return profile.name == name; }),
            qPrintable("non-profile directory excluded: " + name));
    ok &= check(std::any_of(folderProfiles.begin(), folderProfiles.end(), [](const auto &profile) {
        return profile.name == "Extreme EXOS" &&
               profile.explicitModules.contains("SYNOPTICS-ROOT-MIB");
    }), "same declared identity may belong to multiple product profiles");
    ok &= check(std::any_of(folderProfiles.begin(), folderProfiles.end(), [](const auto &profile) {
        return profile.name == "SimpleProduct" && profile.explicitModules.contains("SIMPLE-IDENTITY");
    }), "direct-file first-level fallback creates one automatic profile");

    const QString largeProduct = foldersRoot + "/Extreme Networks/Fabric Engine";
    QDir().mkpath(largeProduct);
    for (int i = 0; i < 240; ++i) {
        const QString identity = QStringLiteral("FE-PRODUCT-%1-MIB").arg(i, 3, 10, QLatin1Char('0'));
        QFile mib(QDir(largeProduct).filePath(QStringLiteral("vendor-file-%1.mib").arg(i)));
        mib.open(QIODevice::WriteOnly);
        mib.write((identity + QStringLiteral(" DEFINITIONS ::= BEGIN\nEND\n")).toUtf8());
    }
    QFile diffserv(QDir(largeProduct).filePath("diffserv-tc.mib"));
    diffserv.open(QIODevice::WriteOnly);
    diffserv.write("DIFFSERV-TC DEFINITIONS ::= BEGIN\nEND\n"); diffserv.close();
    ok &= check(reloaded.refreshAutomaticProfiles(foldersRoot, &error), "large automatic profile refresh");
    const auto largeProfiles = reloaded.profiles();
    const auto fabric = std::find_if(largeProfiles.begin(), largeProfiles.end(), [](const auto &profile) {
        return profile.name == "Extreme Networks Fabric Engine";
    });
    ok &= check(fabric != largeProfiles.end() && fabric->explicitModules.size() == 241 &&
                fabric->explicitModules.contains("DIFFSERV-TC"),
                "large product folder maps every declared identity, including diffserv-tc");
    if (fabric != largeProfiles.end()) {
        MibDependencyIndex profileIndex(temp.filePath("profile-index.json"));
        profileIndex.update({foldersRoot});
        const MibEffectivePlan plan = MibEffectivePlanResolver().resolve(*fabric, profileIndex);
        ok &= check(plan.explicitModules == fabric->explicitModules &&
                    plan.profileType == MibProfileType::Folder,
                    "automatic folder explicit membership enters the authoritative plan unchanged");
    }
    const QString stableId = voss == folderProfiles.end() ? QString() : voss->id;
    ok &= check(reloaded.refreshAutomaticProfiles(foldersRoot, &error), "unchanged folder refresh succeeds");
    const MibProfileRecord *stable = reloaded.find(stableId);
    ok &= check(stable && stable->id == stableId && stable->type == MibProfileType::Folder &&
                !reloaded.update(*stable, &error),
                "folder profile keeps stable ID and is read-only");
    QFile::remove(folderMib.fileName());
    reloaded.refreshAutomaticProfiles(foldersRoot, &error);
    ok &= check(reloaded.find(stableId) && reloaded.find(stableId)->explicitModules.isEmpty(),
                "removing the last provider removes folder explicit membership");

    QFile persisted(path); ok &= check(persisted.open(QIODevice::ReadOnly), "profile file exists");
    const QByteArray json = persisted.readAll();
    ok &= check(json.contains("\"schemaVersion\": 3") && json.contains(id.toUtf8()) &&
                json.contains("\"providerPins\"") &&
                !json.contains("SYNOPTICS-ROOT-MIB"),
                "versioned schema and stable id persisted");
    return ok ? 0 : 1;
}
