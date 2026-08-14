#include "mibdependencyindex.h"

#include "mibcandidatefilter.h"
#include "miblibrary.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <algorithm>

namespace {
QJsonArray strings(const QStringList &values) { QJsonArray a; for (const auto &v : values) a.append(v); return a; }
QStringList stringList(const QJsonArray &values) { QStringList r; for (const auto &v : values) if (v.isString()) r.append(v.toString()); return r; }
QString canonical(const QFileInfo &file) {
    const QString value = file.canonicalFilePath();
    return QDir::cleanPath(value.isEmpty() ? file.absoluteFilePath() : value);
}
QString keyPath(const QString &value) {
#ifdef Q_OS_WIN
    return QDir::cleanPath(value).toLower();
#else
    return QDir::cleanPath(value);
#endif
}
}

MibDependencyIndex::MibDependencyIndex(QString path)
    : filePath(path.isEmpty() ? defaultPath() : std::move(path)) {}

QString MibDependencyIndex::defaultPath()
{
    // Keep this application-owned path stable even when a utility or test has
    // a different QCoreApplication name. The GUI's legacy application identity
    // resolves AppLocalDataLocation to this same SnmpB directory.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
        .filePath(QStringLiteral("SnmpB/mibs/dependency-index-v1.json"));
}

bool MibDependencyIndex::load(QString *error)
{
    currentLoadDiagnostic.clear();
    QFile file(filePath);
    if (!file.exists()) {
        records.clear(); providers.clear(); profileChecks.clear(); currentGeneration = 0;
        currentLoadStatus = MibDependencyIndexLoadStatus::Missing;
        currentLoadDiagnostic = QObject::tr("Dependency index does not exist");
        if (error) error->clear();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        currentLoadStatus = MibDependencyIndexLoadStatus::ReadError;
        currentLoadDiagnostic = file.errorString(); if (error) *error = currentLoadDiagnostic; return false;
    }
    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        currentLoadStatus = MibDependencyIndexLoadStatus::EmptyFile;
        currentLoadDiagnostic = QObject::tr("Dependency index is empty");
        if (error) *error = currentLoadDiagnostic; return false;
    }
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parse);
    const QJsonObject root = document.object();
    if (parse.error != QJsonParseError::NoError || !document.isObject()) {
        currentLoadStatus = MibDependencyIndexLoadStatus::MalformedJson;
        currentLoadDiagnostic = QObject::tr("Malformed dependency index JSON: %1").arg(parse.errorString());
        if (error) *error = currentLoadDiagnostic; return false;
    }
    if (root.value("schemaVersion").toInt(-1) != 1) {
        currentLoadStatus = MibDependencyIndexLoadStatus::UnsupportedSchema;
        currentLoadDiagnostic = QObject::tr("Unsupported dependency index schema version: %1")
            .arg(root.value("schemaVersion").toVariant().toString());
        if (error) *error = currentLoadDiagnostic; return false;
    }
    QList<MibDependencyFileRecord> loaded;
    for (const QJsonValue &value : root.value("files").toArray()) {
        const QJsonObject object = value.toObject(); MibDependencyFileRecord record;
        record.canonicalPath = object.value("path").toString();
        record.searchPathPrecedence = object.value("precedence").toInt(-1);
        record.filename = object.value("filename").toString(); record.size = qint64(object.value("size").toDouble(-1));
        record.modifiedMsecs = qint64(object.value("modifiedMsecs").toDouble(-1));
        record.sha256 = object.value("sha256").toString(); record.checkState = object.value("checkState").toString();
        record.diagnostic = object.value("diagnostic").toString();
        record.lastCheckedUtc = QDateTime::fromString(object.value("lastCheckedUtc").toString(), Qt::ISODate);
        const QJsonObject modules = object.value("modules").toObject();
        for (auto it = modules.begin(); it != modules.end(); ++it)
            record.importsByModule.insert(it.key(), stringList(it.value().toArray()));
        if (!record.canonicalPath.isEmpty()) loaded.append(record);
    }
    records = loaded; currentGeneration = quint64(root.value("generation").toDouble());
    const bool scannerUpgradeRequired = root.value("scannerVersion").toInt() != 2;
    if (scannerUpgradeRequired) for (auto &record : records) record.size = -1;
    profileChecks.clear();
    const QJsonObject checks = root.value("profileChecks").toObject();
    for (auto it = checks.begin(); it != checks.end(); ++it) {
        const QJsonObject o = it.value().toObject(); MibProfileDependencyCheck check;
        check.profileSignature = o.value("signature").toString();
        check.indexGeneration = quint64(o.value("generation").toDouble());
        check.effectiveModules = stringList(o.value("effectiveModules").toArray());
        check.dependencies = stringList(o.value("dependencies").toArray());
        check.unresolved = stringList(o.value("unresolved").toArray());
        check.failureSummaries = stringList(o.value("failureSummaries").toArray());
        check.checkedUtc = QDateTime::fromString(o.value("checkedUtc").toString(), Qt::ISODate);
        check.elapsedMsecs = qint64(o.value("elapsedMsecs").toDouble()); profileChecks.insert(it.key(), check);
    }
    if (scannerUpgradeRequired) profileChecks.clear();
    rebuildProviders(); currentLoadStatus = MibDependencyIndexLoadStatus::Loaded;
    currentLoadDiagnostic = records.isEmpty() ? QObject::tr("Valid empty dependency index") : QString();
    if (error) error->clear();
    return true;
}

QString MibDependencyIndexLoadStatusText(MibDependencyIndexLoadStatus status)
{
    switch (status) {
    case MibDependencyIndexLoadStatus::NotLoaded: return QObject::tr("not-loaded");
    case MibDependencyIndexLoadStatus::Loaded: return QObject::tr("loaded");
    case MibDependencyIndexLoadStatus::Missing: return QObject::tr("missing");
    case MibDependencyIndexLoadStatus::EmptyFile: return QObject::tr("empty-file");
    case MibDependencyIndexLoadStatus::MalformedJson: return QObject::tr("malformed-json");
    case MibDependencyIndexLoadStatus::UnsupportedSchema: return QObject::tr("unsupported-schema");
    case MibDependencyIndexLoadStatus::ReadError: return QObject::tr("read-error");
    }
    return {};
}

bool MibDependencyIndex::save(QString *error) const
{
    QJsonArray files;
    for (const auto &record : records) {
        QJsonObject modules; for (auto it = record.importsByModule.begin(); it != record.importsByModule.end(); ++it) modules.insert(it.key(), strings(it.value()));
        QJsonObject o{{"path", record.canonicalPath}, {"precedence", record.searchPathPrecedence},
            {"filename", record.filename}, {"size", double(record.size)}, {"modifiedMsecs", double(record.modifiedMsecs)},
            {"sha256", record.sha256}, {"modules", modules}, {"checkState", record.checkState},
            {"diagnostic", record.diagnostic}, {"lastCheckedUtc", record.lastCheckedUtc.toUTC().toString(Qt::ISODate)}};
        files.append(o);
    }
    QJsonObject checks;
    for (auto it = profileChecks.begin(); it != profileChecks.end(); ++it) {
        const auto &c = it.value(); checks.insert(it.key(), QJsonObject{{"signature", c.profileSignature},
            {"generation", double(c.indexGeneration)}, {"effectiveModules", strings(c.effectiveModules)},
            {"dependencies", strings(c.dependencies)}, {"unresolved", strings(c.unresolved)},
            {"failureSummaries", strings(c.failureSummaries)},
            {"checkedUtc", c.checkedUtc.toUTC().toString(Qt::ISODate)}, {"elapsedMsecs", double(c.elapsedMsecs)}});
    }
    QDir().mkpath(QFileInfo(filePath).absolutePath()); QSaveFile file(filePath);
    const QJsonObject root{{"schemaVersion", 1}, {"scannerVersion", 2}, {"generation", double(currentGeneration)},
                           {"files", files}, {"profileChecks", checks}};
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString(); return false;
    }
    return true;
}

MibDependencyScanResult MibDependencyIndex::update(const QStringList &searchPaths, QString *error)
{
    QElapsedTimer timer; timer.start(); MibDependencyScanResult result;
    QMap<QString, MibDependencyFileRecord> old;
    for (const auto &record : records) old.insert(keyPath(record.canonicalPath), record);
    QList<MibDependencyFileRecord> next; QSet<QString> seen;
    for (int precedence = 0; precedence < searchPaths.size(); ++precedence) {
        QDir dir(searchPaths[precedence]);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
            if (!MibCandidateFilter::accepts(info.fileName())) continue;
            const QString path = canonical(info), key = keyPath(path); if (seen.contains(key)) continue; seen.insert(key);
            const qint64 mtime = info.lastModified().toMSecsSinceEpoch();
            if (old.contains(key) && old[key].size == info.size() && old[key].modifiedMsecs == mtime &&
                old[key].searchPathPrecedence == precedence) { next.append(old[key]); ++result.reused; continue; }
            QFile file(path); MibDependencyFileRecord record; record.canonicalPath = path;
            record.searchPathPrecedence = precedence; record.filename = info.fileName(); record.size = info.size();
            record.modifiedMsecs = mtime; record.lastCheckedUtc = QDateTime::currentDateTimeUtc();
            if (!file.open(QIODevice::ReadOnly)) { record.checkState = "unreadable"; record.diagnostic = file.errorString(); }
            else { const QByteArray content = file.readAll(); record.sha256 = QString::fromLatin1(QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex());
                const auto scan = MibImportScanner::scan(content); record.importsByModule = scan.importsByModule;
                record.checkState = scan.moduleNames.isEmpty() ? "no-declaration" : "discovered";
                if (scan.malformedImports) record.diagnostic = QObject::tr("Declaration found; IMPORTS block is incomplete"); }
            next.append(record); ++result.scanned; result.changed = true;
        }
    }
    for (auto it = old.begin(); it != old.end(); ++it) if (!seen.contains(it.key())) ++result.deleted;
    if (result.deleted > 0) result.changed = true;
    records = next; if (result.changed) { ++currentGeneration; profileChecks.clear(); }
    rebuildProviders(); result.elapsedMsecs = timer.elapsed();
    if (result.changed && !save(error)) return result;
    return result;
}

MibDependencyInspection MibDependencyIndex::inspect(const QStringList &searchPaths) const
{
    QElapsedTimer timer; timer.start(); MibDependencyInspection result;
    QMap<QString, MibDependencyFileRecord> indexed;
    for (const auto &record : records) indexed.insert(keyPath(record.canonicalPath), record);
    QSet<QString> seen;
    for (int precedence = 0; precedence < searchPaths.size(); ++precedence) {
        QDir dir(searchPaths[precedence]);
        for (const QFileInfo &info : dir.entryInfoList(QDir::Files | QDir::Readable, QDir::Name)) {
            if (!MibCandidateFilter::accepts(info.fileName())) continue;
            MibPhysicalCandidate candidate; candidate.canonicalPath = canonical(info);
            const QString key = keyPath(candidate.canonicalPath); if (seen.contains(key)) continue; seen.insert(key);
            candidate.searchPathPrecedence = precedence; candidate.filename = info.fileName();
            candidate.size = info.size(); candidate.modifiedMsecs = info.lastModified().toMSecsSinceEpoch();
            candidate.indexed = indexed.contains(key);
            candidate.changed = !candidate.indexed || indexed[key].size != candidate.size ||
                indexed[key].modifiedMsecs != candidate.modifiedMsecs ||
                indexed[key].searchPathPrecedence != precedence;
            if (candidate.changed) ++result.newOrChanged; else ++result.unchanged;
            result.candidates.append(candidate);
        }
    }
    for (auto it = indexed.begin(); it != indexed.end(); ++it) if (!seen.contains(it.key())) ++result.deleted;
    result.elapsedMsecs = timer.elapsed(); return result;
}

void MibDependencyIndex::rebuildProviders()
{
    providers.clear();
    for (int i = 0; i < records.size(); ++i)
        for (auto it = records[i].importsByModule.begin(); it != records[i].importsByModule.end(); ++it)
            providers[it.key()].append(i);
}

MibProviderResolution MibDependencyIndex::provider(const QString &moduleName) const
{
    MibProviderResolution result; const QList<int> choices = providers.value(moduleName);
    if (choices.isEmpty()) return result;
    int best = records[choices.first()].searchPathPrecedence;
    for (int index : choices) best = std::min(best, records[index].searchPathPrecedence);
    for (int index : choices) if (records[index].searchPathPrecedence == best) result.alternatives.append(records[index].canonicalPath);
    result.alternatives.removeDuplicates();
    if (result.alternatives.size() == 1) { result.status = MibProviderStatus::Found; result.path = result.alternatives.first(); }
    else result.status = MibProviderStatus::Ambiguous;
    return result;
}

QStringList MibDependencyIndex::imports(const QString &moduleName) const
{
    const auto resolved = provider(moduleName); if (resolved.status != MibProviderStatus::Found) return {};
    for (const auto &record : records) if (keyPath(record.canonicalPath) == keyPath(resolved.path)) return record.importsByModule.value(moduleName);
    return {};
}

QStringList MibDependencyIndex::moduleNames() const { QStringList r = providers.keys(); r.sort(Qt::CaseInsensitive); return r; }
void MibDependencyIndex::setProfileCheck(const QString &id, const MibProfileDependencyCheck &check) { profileChecks[id] = check; }
MibProfileDependencyCheck MibDependencyIndex::profileCheck(const QString &id) const { return profileChecks.value(id); }
bool MibDependencyIndex::profileCheckCurrent(const QString &id, const QString &signature) const {
    const auto c = profileChecks.value(id); return !c.checkedUtc.isNull() && c.profileSignature == signature && c.indexGeneration == currentGeneration;
}
QString MibDependencyIndex::profileSignature(const QStringList &modules, bool base) {
    QStringList normalized = modules; normalized.removeDuplicates(); normalized.sort(Qt::CaseSensitive);
    return QString::fromLatin1(QCryptographicHash::hash((normalized.join('\n') + (base ? "\n+base" : "\n-base")).toUtf8(), QCryptographicHash::Sha256).toHex());
}
void MibDependencyIndex::recordVerification(const QString &moduleName, bool success, const QString &diagnostic)
{
    const auto resolved = provider(moduleName); if (resolved.status != MibProviderStatus::Found) return;
    for (auto &record : records) if (keyPath(record.canonicalPath) == keyPath(resolved.path)) {
        record.checkState = success ? QStringLiteral("verified") : QStringLiteral("failed");
        record.diagnostic = diagnostic; record.lastCheckedUtc = QDateTime::currentDateTimeUtc(); break;
    }
}

MibDependencyCheckResult MibBoundedDependencyLoader::load(const QStringList &roots,
    const MibDependencyIndex &index, const LoadFile &loadFile) const
{
    QElapsedTimer timer; timer.start(); MibDependencyCheckResult result; result.requested = roots;
    QStringList pending, all = roots; QSet<QString> visited;
    for (qsizetype cursor = 0; cursor < all.size(); ++cursor) {
        const QString module = all[cursor]; if (visited.contains(module)) continue; visited.insert(module);
        pending.append(module); for (const QString &dependency : index.imports(module)) if (!visited.contains(dependency)) all.append(dependency);
    }
    for (const QString &module : pending) if (!roots.contains(module)) result.dependencies.append(module);
    const int hardMaximum = pending.size() + 2; QMap<QString, QString> diagnostics;
    while (!pending.isEmpty() && result.passes < hardMaximum) {
        ++result.passes; int progress = 0; QStringList failed;
        for (const QString &module : pending) {
            const auto provider = index.provider(module);
            if (provider.status != MibProviderStatus::Found) { failed.append(module); continue; }
            const auto attempt = loadFile(provider.path, module); diagnostics[module] = attempt.diagnostic;
            if (attempt.success && attempt.loadedModuleNames.contains(module)) { result.loaded.append(module); ++progress; }
            else failed.append(module);
        }
        pending = failed;
        if (progress == 0) { ++result.noProgressStops; break; }
    }
    for (const QString &module : pending) {
        const auto provider = index.provider(module); MibDependencyFailure failure; failure.moduleName = module;
        if (provider.status == MibProviderStatus::Missing) { failure.kind = MibDependencyFailureKind::MissingProvider; failure.detail = QObject::tr("No indexed provider"); }
        else if (provider.status == MibProviderStatus::Ambiguous) { failure.kind = MibDependencyFailureKind::AmbiguousProvider; failure.detail = provider.alternatives.join(QStringLiteral("; ")); }
        else { bool dependencyFailed = false; for (const QString &dependency : index.imports(module)) if (!result.loaded.contains(dependency)) dependencyFailed = true;
            failure.kind = dependencyFailed ? MibDependencyFailureKind::DependencyUnresolved : MibDependencyFailureKind::ParserSemanticFailure;
            failure.detail = diagnostics.value(module); }
        result.failures.append(failure);
    }
    result.loaded.removeDuplicates(); result.dependencies.removeDuplicates(); result.elapsedMsecs = timer.elapsed(); return result;
}

QString MibDependencyFailureText(MibDependencyFailureKind kind)
{
    switch (kind) {
    case MibDependencyFailureKind::MissingProvider: return QObject::tr("Missing provider");
    case MibDependencyFailureKind::AmbiguousProvider: return QObject::tr("Ambiguous provider");
    case MibDependencyFailureKind::ParserSemanticFailure: return QObject::tr("Parser/semantic failure");
    case MibDependencyFailureKind::DependencyUnresolved: return QObject::tr("Dependency unresolved");
    }
    return {};
}
