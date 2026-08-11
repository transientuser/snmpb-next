#include "mibservice.h"

#include "mibdiagnosticcollector.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <cstdlib>

namespace {
QString safe(const char *text) { return QString::fromLocal8Bit(text ? text : ""); }

QString accessName(SmiAccess value)
{
    switch (value) {
    case SMI_ACCESS_NOT_ACCESSIBLE: return QStringLiteral("not-accessible");
    case SMI_ACCESS_NOTIFY: return QStringLiteral("notify");
    case SMI_ACCESS_READ_ONLY: return QStringLiteral("read-only");
    case SMI_ACCESS_READ_WRITE: return QStringLiteral("read-write");
    case SMI_ACCESS_INSTALL: return QStringLiteral("install");
    case SMI_ACCESS_INSTALL_NOTIFY: return QStringLiteral("install-notify");
    case SMI_ACCESS_REPORT_ONLY: return QStringLiteral("report-only");
    default: return {};
    }
}

QString statusName(SmiStatus value)
{
    switch (value) {
    case SMI_STATUS_CURRENT: return QStringLiteral("current");
    case SMI_STATUS_DEPRECATED: return QStringLiteral("deprecated");
    case SMI_STATUS_MANDATORY: return QStringLiteral("mandatory");
    case SMI_STATUS_OPTIONAL: return QStringLiteral("optional");
    case SMI_STATUS_OBSOLETE: return QStringLiteral("obsolete");
    default: return {};
    }
}
}

MibService::MibService(SmiErrorHandler *restoreHandler, int restoreErrorLevel)
    : restoreErrorHandler(restoreHandler), restoredErrorLevel(restoreErrorLevel) {}

void MibService::setSearchPaths(const QStringList &paths)
{
    smiSetPath(paths.join(QLatin1Char(';')).toLocal8Bit().constData());
}

QStringList MibService::searchPaths() const
{
    std::unique_ptr<char, decltype(&std::free)> path{smiGetPath(), std::free};
    return path ? QString::fromLocal8Bit(path.get()).split(';', Qt::KeepEmptyParts)
                : QStringList();
}

MibLoadResult MibService::loadModules(const QStringList &modules, int errorLevel)
{
    MibLoadResult result;
    result.operationId = nextOperationId++;
    result.requestedModules = modules;
    for (const QString &requested : modules) {
        if (smiIsLoaded(requested.toLocal8Bit().constData())) {
            result.alreadyLoadedModules.append(requested);
            continue;
        }
        MibDiagnosticCollector collector(result.operationId, requested);
        collector.install(errorLevel);
        char *canonical = smiLoadModule(requested.toLocal8Bit().constData());
        collector.finish(restoreErrorHandler, restoredErrorLevel);
        result.diagnostics.append(collector.diagnostics());
        if (canonical)
            result.loadedModules.append(QString::fromLocal8Bit(canonical));
        else
            result.unavailableModules.append(requested);
    }
    if (!result.unavailableModules.isEmpty())
        result.status = result.loadedModules.isEmpty() && result.alreadyLoadedModules.isEmpty()
            ? MibLoadStatus::Failure : MibLoadStatus::Partial;
    return result;
}

MibLoadResult MibService::loadPreloads(const QStringList &preloads, int errorLevel)
{
    return loadModules(preloads, errorLevel);
}

QString MibService::languageName(SmiLanguage language)
{
    switch (language) {
    case SMI_LANGUAGE_SMIV1: return QStringLiteral("SMIv1");
    case SMI_LANGUAGE_SMIV2: return QStringLiteral("SMIv2");
    case SMI_LANGUAGE_SMING: return QStringLiteral("SMIng");
    case SMI_LANGUAGE_SPPI: return QStringLiteral("SPPI");
    default: return QStringLiteral("Unknown");
    }
}

QString MibService::oidText(SmiNode *node)
{
    const char *text = node ? smiRenderOID(node->oidlen, node->oid, SMI_RENDER_NUMERIC) : nullptr;
    return safe(text);
}

MibModuleRecord MibService::snapshotModule(SmiModule *module)
{
    MibModuleRecord record;
    if (!module)
        return record;
    record.name = safe(module->name);
    record.path = safe(module->path);
    record.language = languageName(module->language);
    record.organization = safe(module->organization);
    record.contactInfo = safe(module->contactinfo);
    record.description = safe(module->description);
    record.loaded = true;
    if (SmiRevision *revision = smiGetFirstRevision(module))
        record.lastRevision = QDateTime::fromSecsSinceEpoch(revision->date, Qt::UTC);
    if (SmiNode *root = smiGetModuleIdentityNode(module)) {
        record.rootName = safe(root->name);
        record.rootOid = oidText(root);
    }
    for (SmiImport *item = smiGetFirstImport(module); item; item = smiGetNextImport(item)) {
        const QString dependency = safe(item->module);
        if (!record.imports.contains(dependency))
            record.imports.append(dependency);
    }
    return record;
}

QList<MibModuleRecord> MibService::moduleInventory() const
{
    QList<MibModuleRecord> result;
    for (SmiModule *module = smiGetFirstModule(); module; module = smiGetNextModule(module))
        result.append(snapshotModule(module));
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.name < b.name;
    });
    return result;
}

MibTreeNodeRecord MibService::snapshotNode(SmiNode *node)
{
    MibTreeNodeRecord record;
    if (!node)
        return record;
    record.oid = oidText(node);
    record.name = safe(node->name);
    if (SmiModule *module = smiGetNodeModule(node))
        record.moduleName = safe(module->name);
    record.nodeKind = node->nodekind;
    record.access = accessName(node->access);
    record.status = statusName(node->status);
    record.units = safe(node->units);
    record.description = safe(node->description);
    record.reference = safe(node->reference);
    if (SmiType *type = smiGetNodeType(node)) {
        record.typeName = safe(type->name);
        record.baseType = QString::number(type->basetype);
    }
    return record;
}

void MibService::appendTree(SmiNode *node, const QSet<QString> &included,
                            MibTreeNodeRecord *parent)
{
    if (!node || !parent)
        return;
    MibTreeNodeRecord current = snapshotNode(node);
    const bool directlyIncluded = included.isEmpty() || included.contains(current.moduleName);
    for (SmiNode *child = smiGetFirstChildNode(node); child;
         child = smiGetNextChildNode(child))
        appendTree(child, included, &current);
    if (directlyIncluded || !current.children.isEmpty())
        parent->children.append(std::move(current));
}

MibTreeNodeRecord MibService::treeSnapshot(const QStringList &includedModules) const
{
    MibTreeNodeRecord root;
    root.oid = QStringLiteral("1");
    root.name = QStringLiteral("MIB Tree");
    appendTree(smiGetNode(nullptr, "iso"), QSet<QString>(includedModules.begin(),
               includedModules.end()), &root);
    return root;
}
