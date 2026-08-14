#include "mibprofile.h"
#include "mibtreemodel.h"

#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message)
{
    if (!value) std::cerr << message << '\n';
    return value;
}

MibCatalog catalog(std::initializer_list<MibCatalogEntry> values)
{
    MibCatalog result; result.setEntries(QList<MibCatalogEntry>(values)); return result;
}

MibCatalogEntry entry(const QString &name, const QStringList &imports = {})
{
    MibCatalogEntry value; value.moduleName = name; value.imports = imports; return value;
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
    ok &= check(reloaded.remove(duplicate, &error), "delete custom");

    const QStringList available{"VENDOR-A", "DEP-A", "SHARED", "CYCLE-A", "CYCLE-B",
                                "SNMPv2-SMI", "IF-MIB"};
    const MibCatalog dependencies = catalog({entry("VENDOR-A", {"DEP-A", "SHARED"}),
        entry("DEP-A", {"SHARED", "CYCLE-A"}), entry("SHARED"),
        entry("CYCLE-A", {"CYCLE-B"}), entry("CYCLE-B", {"CYCLE-A"}),
        entry("SNMPv2-SMI"), entry("IF-MIB")});
    const auto resolved = MibProfileResolver().resolve(saved, available, dependencies);
    ok &= check(resolved.effectiveModules.contains("DEP-A") &&
                resolved.effectiveModules.contains("SHARED"), "dependency expansion");
    ok &= check(resolved.effectiveModules.count("SHARED") == 1, "shared dependency deduplicated");
    ok &= check(resolved.effectiveModules.contains("CYCLE-A") &&
                resolved.effectiveModules.contains("CYCLE-B"), "cycle safe");
    ok &= check(resolved.effectiveModules.contains("SNMPv2-SMI") &&
                resolved.effectiveModules.contains("IF-MIB"), "standard base included");
    ok &= check(resolved.missingModules.contains("MISSING-LATER"), "missing reference reported");
    QStringList withReinstalled = available; withReinstalled.append("MISSING-LATER");
    ok &= check(!MibProfileResolver().resolve(saved, withReinstalled, dependencies)
                  .missingModules.contains("MISSING-LATER"), "reinstalled identity recovers");

    MibProfileRecord comprehensive{"all-local", "All local", MibProfileType::Custom,
                                   available, true};
    QStringList withoutLldp = available;
    const auto missingStandard = MibProfileResolver().resolve(
        comprehensive, withoutLldp, dependencies);
    int lldpCount = 0;
    for (const auto &requirement : missingStandard.requirements) if (requirement.moduleName == "LLDP-MIB") {
        ++lldpCount;
        ok &= check(requirement.missing, "missing standards module marked missing");
        ok &= check(requirement.reason == "Standards / MIB-II base", "standards reason retained");
    }
    ok &= check(lldpCount == 1, "missing standards module represented once");

    const MibCatalog sharedMissingCatalog = catalog({entry("ROOT-A", {"SHARED-MISSING"}),
        entry("ROOT-B", {"SHARED-MISSING"}), entry("SHARED-MISSING")});
    MibProfileRecord imported{"imported", "Imported", MibProfileType::Custom,
                              {"ROOT-A", "ROOT-B"}, false};
    const auto missingImported = MibProfileResolver().resolve(
        imported, {"ROOT-A", "ROOT-B"}, sharedMissingCatalog);
    int sharedMissingCount = 0;
    for (const auto &requirement : missingImported.requirements)
        if (requirement.moduleName == "SHARED-MISSING") {
            ++sharedMissingCount;
            ok &= check(requirement.missing && requirement.reason == "Imported dependency",
                        "missing imported dependency state/reason");
        }
    ok &= check(sharedMissingCount == 1, "shared missing dependency represented once");
    const auto afterDownload = MibProfileResolver().resolve(
        imported, {"ROOT-A", "ROOT-B", "SHARED-MISSING"}, sharedMissingCatalog);
    int presentCount = 0;
    for (const auto &requirement : afterDownload.requirements)
        if (requirement.moduleName == "SHARED-MISSING") {
            ++presentCount; ok &= check(!requirement.missing, "downloaded dependency no longer missing");
        }
    ok &= check(presentCount == 1, "successful download refresh keeps one present dependency");

    const auto all = MibProfileResolver().resolve(*reloaded.find(MibProfileDefinitions::allId()),
                                                   available, dependencies);
    ok &= check(QSet<QString>(all.effectiveModules.cbegin(), all.effectiveModules.cend()) ==
                QSet<QString>(available.cbegin(), available.cend()),
                "All MIBs is every available usable module");
    const auto standards = MibProfileResolver().resolve(
        *reloaded.find(MibProfileDefinitions::standardsId()), available, dependencies);
    ok &= check(standards.explicitModules == MibProfileDefinitions::standardsModules(),
                "maintainable standards definition");

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

    QFile persisted(path); ok &= check(persisted.open(QIODevice::ReadOnly), "profile file exists");
    const QByteArray json = persisted.readAll();
    ok &= check(json.contains("\"schemaVersion\": 1") && json.contains(id.toUtf8()),
                "versioned schema and stable id persisted");
    return ok ? 0 : 1;
}
