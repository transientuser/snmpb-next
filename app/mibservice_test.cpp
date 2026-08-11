#include "mibservice.h"
#include "mibdiagnosticmodel.h"
#include "mibtreemodel.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>

namespace {
int failures = 0;
void check(bool condition, const char *message)
{
    if (!condition) { QTextStream(stderr) << "FAIL: " << message << Qt::endl; ++failures; }
}

int nodeCount(const MibTreeNodeRecord &node)
{
    int result = 1;
    for (const auto &child : node.children) result += nodeCount(child);
    return result;
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory created");
    const QString bundled = QStringLiteral(SNMPB_SOURCE_DIR "/libsmi/mibs/ietf");

    const QString malformedPath = temporary.filePath("BROKEN-TEST-MIB");
    QFile malformed(malformedPath);
    check(malformed.open(QIODevice::WriteOnly | QIODevice::Text), "malformed fixture created");
    QTextStream(&malformed)
        << "BROKEN-TEST-MIB DEFINITIONS ::= BEGIN\n"
           "IMPORTS MODULE-IDENTITY FROM SNMPv2-SMI;\n"
           "broken MODULE-IDENTITY LAST-UPDATED \"bad\" STATUS current "
           "DESCRIPTION \"broken\" ::= { enterprises 99999 }\nEND\n";
    malformed.close();
    const QString malformedPath2 = temporary.filePath("BROKEN-TEST-MIB-TWO");
    QFile malformed2(malformedPath2);
    check(malformed2.open(QIODevice::WriteOnly | QIODevice::Text), "second malformed fixture created");
    QTextStream(&malformed2)
        << "BROKEN-TEST-MIB-TWO DEFINITIONS ::= BEGIN\n"
           "broken OBJECT IDENTIFIER ::= { definitelyMissing 1 }\nEND\n";
    malformed2.close();

    smiInit("snmpb-mib-service-test");
    smiSetFlags(smiGetFlags() | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR);
    MibService service;
    service.setSearchPaths({temporary.path(), bundled});
    check(service.searchPaths() == QStringList{temporary.path(), bundled},
          "configured path order is retained");

    MibLoadResult first = service.loadModules({"SNMPv2-MIB"}, 9);
    check(first.status == MibLoadStatus::Success &&
          first.loadedModules.contains("SNMPv2-MIB"), "known bundled module loads canonically");
    MibLoadResult already = service.loadModules({"SNMPv2-MIB"}, 9);
    check(already.alreadyLoadedModules == QStringList{"SNMPv2-MIB"},
          "already-loaded module is classified without reload");
    check(already.diagnostics.isEmpty(), "already-loaded successful request emits no diagnostics");
    MibLoadResult partial = service.loadModules({"IF-MIB", "NO-SUCH-MIB-FOR-TEST"}, 9);
    check(partial.status == MibLoadStatus::Partial &&
          partial.loadedModules.contains("IF-MIB") &&
          partial.unavailableModules == QStringList{"NO-SUCH-MIB-FOR-TEST"},
          "multiple-module partial result is deterministic");
    MibLoadResult preloads = service.loadPreloads({"IF-MIB", "SNMPv2-MIB"}, 9);
    check(preloads.alreadyLoadedModules == QStringList({"IF-MIB", "SNMPv2-MIB"}),
          "preload handling preserves request ordering and loaded state");
    MibLoadResult bad = service.loadModules({malformedPath}, 9);
    check(!bad.diagnostics.isEmpty(), "malformed-MIB diagnostics captured");
    MibLoadResult badAgain = service.loadModules({malformedPath2}, 9);
    QList<MibDiagnosticRecord> multiple = bad.diagnostics;
    multiple.append(badAgain.diagnostics);
    check(multiple.size() > 1 && multiple.first().operationId == bad.operationId &&
          multiple.last().operationId == badAgain.operationId,
          "multiple diagnostics preserve operation and callback ordering");
    for (int i = 0; i < bad.diagnostics.size(); ++i) {
        const auto &diagnostic = bad.diagnostics.at(i);
        check(diagnostic.operationId == bad.operationId && diagnostic.line >= 0 &&
              !diagnostic.message.isEmpty() && !diagnostic.rawText.isEmpty(),
              "diagnostic attribution and original wording retained");
        if (i > 0)
            check(bad.diagnostics.at(i - 1).operationId == diagnostic.operationId,
                  "diagnostic ordering retained within operation");
    }
    MibDiagnosticModel diagnosticModel;
    diagnosticModel.setDiagnostics(bad.diagnostics);
    check(diagnosticModel.rowCount() == bad.diagnostics.size() &&
          diagnosticModel.data(diagnosticModel.index(0, MibDiagnosticModel::MessageColumn)) ==
              bad.diagnostics.first().message &&
          diagnosticModel.data(diagnosticModel.index(0, 0),
                               MibDiagnosticModel::RawTextRole) ==
              bad.diagnostics.first().rawText,
          "diagnostics model exposes structured and original wording");

    const QList<MibModuleRecord> inventory = service.moduleInventory();
    check(inventory.size() >= 2, "multiple loaded modules inventoried");
    for (int i = 1; i < inventory.size(); ++i)
        check(inventory.at(i - 1).name <= inventory.at(i).name,
              "module inventory has deterministic canonical ordering");
    auto snmpModule = std::find_if(inventory.begin(), inventory.end(), [](const auto &module) {
        return module.name == "SNMPv2-MIB";
    });
    check(snmpModule != inventory.end() && snmpModule->loaded &&
          !snmpModule->path.isEmpty() && !snmpModule->imports.isEmpty(),
          "module record snapshots metadata without pointers");

    const MibTreeNodeRecord snapshot = service.treeSnapshot({"SNMPv2-MIB", "IF-MIB"});
    check(snapshot.name == "MIB Tree" && snapshot.oid == "1" &&
          !snapshot.children.isEmpty() && nodeCount(snapshot) > 100,
          "value tree captures hierarchy at useful scale");
    MibTreeModel model;
    model.setSnapshot(snapshot);
    const QModelIndex sysDescr = model.indexForOid("1.3.6.1.2.1.1.1");
    check(sysDescr.isValid() && model.data(sysDescr).toString() == "sysDescr" &&
          model.data(sysDescr, MibTreeModel::ModuleRole).toString() == "SNMPv2-MIB",
          "tree roles and OID identity are available");
    check(model.parent(sysDescr).isValid(), "model parent relationship is valid");
    MibTreeFilterModel filter;
    filter.setSourceModel(&model);
    filter.setFilterFixedString("sysDescr");
    check(filter.rowCount() > 0, "recursive filtering preserves matching ancestry");

    const QString retainedOid = model.oidForIndex(sysDescr);
    model.setSnapshot(service.treeSnapshot({"SNMPv2-MIB"}));
    check(model.indexForOid(retainedOid).isValid(), "selection OID survives compatible rebuild");
    model.setSnapshot(MibTreeNodeRecord{QStringLiteral("1"), QStringLiteral("MIB Tree")});
    check(!model.indexForOid(retainedOid).isValid(), "removed selection safely becomes invalid");

    QSettings settings(temporary.filePath("SnmpB.ini"), QSettings::IniFormat);
    settings.setValue("sentinel", 42); settings.sync();
    const QByteArray before = [&] { QFile f(settings.fileName()); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    service.moduleInventory();
    const QByteArray after = [&] { QFile f(settings.fileName()); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }();
    check(before == after, "viewing inventory does not rewrite settings");

    smiExit();
    return failures == 0 ? 0 : 1;
}
