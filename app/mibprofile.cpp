#include "mibprofile.h"
#include "mibcandidatefilter.h"

#include <QDir>
#include <QFile>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
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
        !QSet<int>{1, 2}.contains(document.object().value(QStringLiteral("schemaVersion")).toInt())) {
        if (error) *error = QObject::tr("Unsupported or invalid MIB profile file");
        return {};
    }
    QList<MibProfileRecord> result;
    for (const QJsonValue &value : document.object().value(QStringLiteral("profiles")).toArray()) {
        const QJsonObject object = value.toObject();
        MibProfileRecord profile;
        profile.id = object.value(QStringLiteral("id")).toString();
        profile.name = object.value(QStringLiteral("name")).toString();
        profile.type = object.value(QStringLiteral("type")).toString() == QStringLiteral("folder")
            ? MibProfileType::Folder : MibProfileType::Custom;
        profile.explicitModules = stringsFromJson(object.value(QStringLiteral("modules")).toArray());
        profile.includeStandardBase = object.value(QStringLiteral("includeStandardBase")).toBool();
        profile.directory = object.value(QStringLiteral("directory")).toString();
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
        if (profile.type != MibProfileType::Custom && profile.type != MibProfileType::Folder) continue;
        QJsonObject object;
        object.insert(QStringLiteral("id"), profile.id);
        object.insert(QStringLiteral("name"), profile.name);
        object.insert(QStringLiteral("type"), profile.type == MibProfileType::Folder
            ? QStringLiteral("folder") : QStringLiteral("custom"));
        if (profile.type == MibProfileType::Custom)
            object.insert(QStringLiteral("modules"), stringsToJson(uniqueSorted(profile.explicitModules)));
        else object.insert(QStringLiteral("directory"), profile.directory);
        object.insert(QStringLiteral("includeStandardBase"), profile.includeStandardBase);
        items.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 2);
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
    const QList<MibProfileRecord> loaded = repository.load(&detail);
    customProfiles.clear(); folderProfiles.clear();
    for (const MibProfileRecord &profile : loaded)
        (profile.type == MibProfileType::Folder ? folderProfiles : customProfiles).append(profile);
    if (error) *error = detail;
    return detail.isEmpty();
}

bool MibProfileService::persist(QString *error) const
{
    QList<MibProfileRecord> stored = customProfiles;
    stored.append(folderProfiles);
    return repository.save(stored, error);
}

QList<MibProfileRecord> MibProfileService::profiles() const
{
    QList<MibProfileRecord> result = builtInProfiles;
    result.append(customProfiles);
    result.append(folderProfiles);
    return result;
}

const MibProfileRecord *MibProfileService::find(const QString &id) const
{
    for (const MibProfileRecord &profile : builtInProfiles)
        if (profile.id == id) return &profile;
    for (const MibProfileRecord &profile : customProfiles)
        if (profile.id == id) return &profile;
    for (const MibProfileRecord &profile : folderProfiles)
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

bool MibProfileService::refreshAutomaticProfiles(const QString &mibRoot, QString *error)
{
    QMap<QString, MibProfileRecord> existing;
    for (const MibProfileRecord &profile : folderProfiles)
        existing.insert(QDir::cleanPath(profile.directory), profile);
    QList<MibProfileRecord> discovered;
    const auto addProfile = [&](const QString &directory, const QString &name,
                                QList<MibProfileRecord> *target) {
        MibProfileRecord profile = existing.value(directory);
        if (profile.id.isEmpty()) profile.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        profile.name = name;
        profile.type = MibProfileType::Folder;
        profile.directory = directory;
        profile.includeStandardBase = false;
        profile.explicitModules.clear();
        QDirIterator files(directory, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString path = files.next();
            if (!MibCandidateFilter::accepts(QFileInfo(path).fileName())) continue;
            QFile input(path);
            if (!input.open(QIODevice::ReadOnly)) continue;
            profile.explicitModules.append(MibImportScanner::scan(input.readAll()).moduleNames);
        }
        profile.explicitModules = uniqueSorted(profile.explicitModules);
        target->append(profile);
    };
    const QDir root(mibRoot);
    for (const QFileInfo &firstLevel : root.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
        const QString firstName = firstLevel.fileName();
        if (firstName.compare(QStringLiteral("Standards"), Qt::CaseInsensitive) == 0 ||
            firstName.compare(QStringLiteral("Unassigned"), Qt::CaseInsensitive) == 0 ||
            firstName.compare(QStringLiteral("Library"), Qt::CaseInsensitive) == 0 ||
            firstName.compare(QStringLiteral("Profiles"), Qt::CaseInsensitive) == 0)
            continue;
        const QDir vendor(firstLevel.absoluteFilePath());
        const QFileInfoList products = vendor.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        if (!products.isEmpty()) {
            for (const QFileInfo &product : products)
                addProfile(QDir::cleanPath(product.absoluteFilePath()),
                           firstName + QStringLiteral(" ") + product.fileName(), &discovered);
            continue;
        }
        bool hasDirectCandidate = false;
        for (const QFileInfo &file : vendor.entryInfoList(QDir::Files | QDir::Readable))
            if (MibCandidateFilter::accepts(file.fileName())) { hasDirectCandidate = true; break; }
        if (hasDirectCandidate)
            addProfile(QDir::cleanPath(firstLevel.absoluteFilePath()), firstName, &discovered);
    }
    // Development-prototype Profiles/<name> folders remain intact and are
    // recognized as one automatic profile each during the transition.
    const QDir prototypeProfiles(root.filePath(QStringLiteral("Profiles")));
    for (const QFileInfo &folder : prototypeProfiles.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable, QDir::Name | QDir::IgnoreCase)) {
        addProfile(QDir::cleanPath(folder.absoluteFilePath()), folder.fileName(), &discovered);
    }
    const QList<MibProfileRecord> previous = folderProfiles;
    folderProfiles = discovered;
    if (!persist(error)) { folderProfiles = previous; return false; }
    return true;
}
