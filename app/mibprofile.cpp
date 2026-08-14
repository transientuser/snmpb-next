#include "mibprofile.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

namespace {
QStringList uniqueSorted(QStringList values)
{
    values.removeDuplicates();
    values.sort(Qt::CaseInsensitive);
    return values;
}

QJsonArray stringsToJson(const QStringList &values)
{
    QJsonArray result;
    for (const QString &value : values) result.append(value);
    return result;
}

QStringList stringsFromJson(const QJsonArray &values)
{
    QStringList result;
    for (const QJsonValue &value : values)
        if (value.isString() && !value.toString().isEmpty()) result.append(value.toString());
    return uniqueSorted(result);
}
}

QString MibProfileDefinitions::allId() { return QStringLiteral("builtin.all"); }
QString MibProfileDefinitions::standardsId() { return QStringLiteral("builtin.standards-mib2"); }

QStringList MibProfileDefinitions::standardsModules()
{
    // Deliberately small, reviewable base for ordinary SNMP/MIB-II work.
    return {QStringLiteral("SNMPv2-SMI"), QStringLiteral("SNMPv2-TC"),
            QStringLiteral("SNMPv2-CONF"), QStringLiteral("SNMPv2-MIB"),
            QStringLiteral("IF-MIB"), QStringLiteral("IP-MIB"),
            QStringLiteral("TCP-MIB"), QStringLiteral("UDP-MIB"),
            QStringLiteral("ENTITY-MIB"), QStringLiteral("HOST-RESOURCES-MIB"),
            QStringLiteral("BRIDGE-MIB"), QStringLiteral("Q-BRIDGE-MIB"),
            QStringLiteral("LLDP-MIB"), QStringLiteral("INET-ADDRESS-MIB")};
}

QList<MibProfileRecord> MibProfileDefinitions::builtIns()
{
    return {{allId(), QObject::tr("All MIBs"), MibProfileType::All, {}, false},
            {standardsId(), QObject::tr("Standards / MIB-II"),
             MibProfileType::Standards, standardsModules(), false}};
}

QString MibProfileDefinitions::validCurrentId(const QString &savedId,
                                               const QList<MibProfileRecord> &profiles)
{
    for (const MibProfileRecord &profile : profiles)
        if (profile.id == savedId) return savedId;
    return allId();
}

MibProfileRepository::MibProfileRepository(QString path) : filePath(std::move(path)) {}

QList<MibProfileRecord> MibProfileRepository::load(QString *error) const
{
    QFile file(filePath);
    if (!file.exists()) return {};
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value(QStringLiteral("schemaVersion")).toInt() != 1) {
        if (error) *error = QObject::tr("Unsupported or invalid MIB profile file");
        return {};
    }
    QList<MibProfileRecord> result;
    for (const QJsonValue &value : document.object().value(QStringLiteral("profiles")).toArray()) {
        const QJsonObject object = value.toObject();
        MibProfileRecord profile;
        profile.id = object.value(QStringLiteral("id")).toString();
        profile.name = object.value(QStringLiteral("name")).toString();
        profile.type = MibProfileType::Custom;
        profile.explicitModules = stringsFromJson(object.value(QStringLiteral("modules")).toArray());
        profile.includeStandardBase = object.value(QStringLiteral("includeStandardBase")).toBool();
        if (!profile.id.isEmpty() && !profile.name.isEmpty() &&
            profile.id != MibProfileDefinitions::allId() &&
            profile.id != MibProfileDefinitions::standardsId()) result.append(profile);
    }
    return result;
}

bool MibProfileRepository::save(const QList<MibProfileRecord> &profiles, QString *error) const
{
    QJsonArray items;
    for (const MibProfileRecord &profile : profiles) {
        if (profile.type != MibProfileType::Custom) continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), profile.id);
        object.insert(QStringLiteral("name"), profile.name);
        object.insert(QStringLiteral("type"), QStringLiteral("custom"));
        object.insert(QStringLiteral("modules"), stringsToJson(uniqueSorted(profile.explicitModules)));
        object.insert(QStringLiteral("includeStandardBase"), profile.includeStandardBase);
        items.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 1);
    root.insert(QStringLiteral("profiles"), items);
    QDir().mkpath(QFileInfo(filePath).absolutePath());
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly) ||
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

MibProfileService::MibProfileService(MibProfileRepository value)
    : repository(std::move(value)) { reload(); }

bool MibProfileService::reload(QString *error)
{
    QString detail;
    customProfiles = repository.load(&detail);
    if (error) *error = detail;
    return detail.isEmpty();
}

bool MibProfileService::persist(QString *error) const { return repository.save(customProfiles, error); }

QList<MibProfileRecord> MibProfileService::profiles() const
{
    QList<MibProfileRecord> result = builtInProfiles;
    result.append(customProfiles);
    return result;
}

const MibProfileRecord *MibProfileService::find(const QString &id) const
{
    for (const MibProfileRecord &profile : builtInProfiles)
        if (profile.id == id) return &profile;
    for (const MibProfileRecord &profile : customProfiles)
        if (profile.id == id) return &profile;
    return nullptr;
}

bool MibProfileService::isBuiltIn(const QString &id)
{
    return id == MibProfileDefinitions::allId() || id == MibProfileDefinitions::standardsId();
}

QString MibProfileService::create(const QString &name, QString *error)
{
    if (name.trimmed().isEmpty()) { if (error) *error = QObject::tr("Profile name is required"); return {}; }
    MibProfileRecord record{QUuid::createUuid().toString(QUuid::WithoutBraces),
                            name.trimmed(), MibProfileType::Custom, {}, true};
    customProfiles.append(record);
    if (!persist(error)) { customProfiles.removeLast(); return {}; }
    return record.id;
}

QString MibProfileService::duplicate(const QString &id, const QString &name, QString *error)
{
    const MibProfileRecord *source = find(id);
    if (!source) { if (error) *error = QObject::tr("Profile not found"); return {}; }
    MibProfileRecord copy = *source;
    copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    copy.name = name.trimmed().isEmpty() ? source->name + QObject::tr(" Copy") : name.trimmed();
    copy.type = MibProfileType::Custom;
    customProfiles.append(copy);
    if (!persist(error)) { customProfiles.removeLast(); return {}; }
    return copy.id;
}

bool MibProfileService::rename(const QString &id, const QString &name, QString *error)
{
    if (isBuiltIn(id) || name.trimmed().isEmpty()) return false;
    for (MibProfileRecord &profile : customProfiles) if (profile.id == id) {
        const QString old = profile.name; profile.name = name.trimmed();
        if (!persist(error)) { profile.name = old; return false; }
        return true;
    }
    return false;
}

bool MibProfileService::remove(const QString &id, QString *error)
{
    if (isBuiltIn(id)) return false;
    for (qsizetype i = 0; i < customProfiles.size(); ++i) if (customProfiles[i].id == id) {
        const MibProfileRecord removed = customProfiles.takeAt(i);
        if (!persist(error)) { customProfiles.insert(i, removed); return false; }
        return true;
    }
    return false;
}

bool MibProfileService::update(const MibProfileRecord &value, QString *error)
{
    if (isBuiltIn(value.id) || value.type != MibProfileType::Custom) return false;
    for (MibProfileRecord &profile : customProfiles) if (profile.id == value.id) {
        const MibProfileRecord old = profile; profile = value;
        profile.explicitModules = uniqueSorted(profile.explicitModules);
        if (!persist(error)) { profile = old; return false; }
        return true;
    }
    return false;
}

MibProfileEffectiveSet MibProfileResolver::resolve(const MibProfileRecord &profile,
    const QStringList &availableModules, const MibCatalog &catalog) const
{
    MibProfileEffectiveSet result;
    if (profile.type == MibProfileType::All) {
        result.explicitModules = uniqueSorted(availableModules);
        result.effectiveModules = result.explicitModules;
        return result;
    }
    result.explicitModules = profile.type == MibProfileType::Standards
        ? MibProfileDefinitions::standardsModules() : profile.explicitModules;
    QStringList roots = result.explicitModules;
    if (profile.type == MibProfileType::Custom && profile.includeStandardBase)
        roots.append(MibProfileDefinitions::standardsModules());
    roots = uniqueSorted(roots);
    QMap<QString, MibLibraryStatus> known;
    for (const QString &module : availableModules) known.insert(module, MibLibraryStatus::Installed);
    const MibDependencyPlan plan = MibDependencyResolver().resolve(roots, known, catalog);
    QStringList effective = roots;
    const auto collect = [&effective](const auto &self, const MibDependencyNode &node) -> void {
        effective.append(node.moduleName);
        for (const MibDependencyNode &child : node.dependencies) self(self, child);
    };
    for (const MibDependencyNode &root : plan.roots) collect(collect, root);
    result.effectiveModules = uniqueSorted(effective);
    result.missingModules = uniqueSorted(plan.unresolved);
    QStringList automatic = result.effectiveModules;
    for (const QString &module : result.explicitModules) automatic.removeAll(module);
    result.automaticDependencies = uniqueSorted(automatic);
    const QStringList standards = MibProfileDefinitions::standardsModules();
    for (const QString &module : result.automaticDependencies) {
        MibProfileEffectiveSet::Requirement requirement;
        requirement.moduleName = module;
        requirement.missing = !availableModules.contains(module);
        requirement.reason = profile.type == MibProfileType::Custom &&
            profile.includeStandardBase && standards.contains(module)
            ? QObject::tr("Standards / MIB-II base")
            : QObject::tr("Imported dependency");
        result.requirements.append(requirement);
    }
    return result;
}
