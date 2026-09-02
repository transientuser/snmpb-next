#include "mibprofile.h"

#include "mibdependencyindex.h"
#include "mibcandidatefilter.h"

#include <QDir>
#include <QFile>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <utility>

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

QMap<QString, MibProviderPin> pinsFromJson(const QJsonObject &values)
{
    QMap<QString, MibProviderPin> result;
    for (auto it = values.begin(); it != values.end(); ++it) {
        const QJsonObject pin = it.value().toObject();
        const QString path = pin.value(QStringLiteral("path")).toString();
        const QString hash = pin.value(QStringLiteral("sha256")).toString();
        if (!it.key().isEmpty() && !path.isEmpty() && !hash.isEmpty())
            result.insert(it.key(), {path, hash});
    }
    return result;
}

QJsonObject pinsToJson(const QMap<QString, MibProviderPin> &pins)
{
    QJsonObject result;
    for (auto it = pins.begin(); it != pins.end(); ++it)
        result.insert(it.key(), QJsonObject{{QStringLiteral("path"), it->canonicalPath},
                                            {QStringLiteral("sha256"), it->sha256}});
    return result;
}

QString canonicalFilePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}

QList<MibProfileMember> membersFromJson(const QJsonArray &values)
{
    QList<MibProfileMember> result;
    for (const QJsonValue &value : values) {
        const QJsonObject object = value.toObject();
        MibProfileMember member;
        member.canonicalPath = canonicalFilePath(object.value("path").toString());
        member.sha256 = object.value("sha256").toString();
        member.identities = stringsFromJson(object.value("identities").toArray());
        member.reason = object.value("reason").toString() == QStringLiteral("dependency")
            ? MibProfileMemberReason::Dependency : MibProfileMemberReason::Added;
        if (!member.canonicalPath.isEmpty()) result.append(member);
    }
    return result;
}

QJsonArray membersToJson(QList<MibProfileMember> members)
{
    std::sort(members.begin(), members.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive) < 0;
    });
    QJsonArray result;
    for (const auto &member : members)
        result.append(QJsonObject{{"path", QDir::fromNativeSeparators(member.canonicalPath)},
            {"sha256", member.sha256}, {"identities", stringsToJson(uniqueSorted(member.identities))},
            {"reason", member.reason == MibProfileMemberReason::Dependency
                ? QStringLiteral("dependency") : QStringLiteral("added")}});
    return result;
}
}

MibProfileMemberState MibProfileMemberCurrentState(const MibProfileMember &member)
{
    QFile file(member.canonicalPath);
    if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly))
        return MibProfileMemberState::Missing;
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex());
    return hash == member.sha256 ? MibProfileMemberState::Current
                                 : MibProfileMemberState::Changed;
}

QList<MibProfileMember> MibProfileMembersFromFiles(
    const QStringList &files, MibProfileMemberReason reason, QStringList *diagnostics)
{
    QList<MibProfileMember> result;
    QSet<QString> seen;
    for (const QString &path : files) {
        const QString canonical = canonicalFilePath(path);
#ifdef Q_OS_WIN
        const QString key = canonical.toLower();
#else
        const QString key = canonical;
#endif
        if (canonical.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        QFile file(canonical);
        if (!QFileInfo(file).isFile() || !file.open(QIODevice::ReadOnly)) {
            if (diagnostics) diagnostics->append(QObject::tr("Profile file is not readable: %1").arg(canonical));
            continue;
        }
        const QByteArray content = file.readAll();
        const auto scan = MibImportScanner::scan(content);
        if (scan.moduleNames.isEmpty()) {
            if (diagnostics) diagnostics->append(QObject::tr("No declared MIB identity found: %1").arg(canonical));
            continue;
        }
        result.append({canonical, QString::fromLatin1(QCryptographicHash::hash(
            content, QCryptographicHash::Sha256).toHex()), uniqueSorted(scan.moduleNames), reason});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive) < 0;
    });
    return result;
}

QStringList MibProfileMemberIdentities(const QList<MibProfileMember> &members)
{
    QStringList result;
    for (const auto &member : members) result.append(member.identities);
    return uniqueSorted(result);
}

bool MibProfileRequiresExactMigration(const MibProfileRecord &profile)
{
    return profile.members.isEmpty() && profile.unresolvedLegacyModules.isEmpty() &&
        (!profile.explicitModules.isEmpty() || profile.includeStandardBase);
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
    return profiles.isEmpty() ? QString() : profiles.first().id;
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
        !QSet<int>{1, 2, 3, 4}.contains(document.object().value(QStringLiteral("schemaVersion")).toInt())) {
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
        profile.providerPins = pinsFromJson(object.value(QStringLiteral("providerPins")).toObject());
        profile.members = membersFromJson(object.value(QStringLiteral("members")).toArray());
        profile.unresolvedLegacyModules = stringsFromJson(
            object.value(QStringLiteral("unresolvedLegacyModules")).toArray());
        if (!profile.members.isEmpty())
            profile.explicitModules = uniqueSorted(MibProfileMemberIdentities(profile.members) +
                                                   profile.unresolvedLegacyModules);
        if (!profile.id.isEmpty() && !profile.name.isEmpty()) result.append(profile);
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
        if (!profile.providerPins.isEmpty())
            object.insert(QStringLiteral("providerPins"), pinsToJson(profile.providerPins));
        if (!profile.members.isEmpty()) object.insert(QStringLiteral("members"), membersToJson(profile.members));
        if (!profile.unresolvedLegacyModules.isEmpty())
            object.insert(QStringLiteral("unresolvedLegacyModules"),
                          stringsToJson(uniqueSorted(profile.unresolvedLegacyModules)));
        items.append(object);
    }
    QJsonObject root;
    root.insert(QStringLiteral("schemaVersion"), 4);
    root.insert(QStringLiteral("ordinaryProfilesMigrated"), true);
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

bool MibProfileRepository::ordinaryProfileMigrationComplete() const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    return QJsonDocument::fromJson(file.readAll()).object()
        .value(QStringLiteral("ordinaryProfilesMigrated")).toBool();
}

MibProfileService::MibProfileService(MibProfileRepository value)
    : repository(std::move(value))
{
    const bool migrated = repository.ordinaryProfileMigrationComplete();
    reload();
    if (!migrated) {
        for (MibProfileRecord profile : std::as_const(builtInProfiles)) {
            profile.type = MibProfileType::Custom;
            if (profile.id == MibProfileDefinitions::allId()) profile.explicitModules.clear();
            if (std::none_of(customProfiles.cbegin(), customProfiles.cend(),
                [&profile](const auto &existing) { return existing.id == profile.id; }))
                customProfiles.append(profile);
        }
        QString ignored;
        persist(&ignored);
    }
    builtInProfiles.clear();
}

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
    Q_UNUSED(id)
    return false;
}

QString MibProfileService::create(const QString &name, QString *error)
{
    if (name.trimmed().isEmpty()) { if (error) *error = QObject::tr("Profile name is required"); return {}; }
    MibProfileRecord record{QUuid::createUuid().toString(QUuid::WithoutBraces),
                            name.trimmed(), MibProfileType::Custom, {}, false};
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
    if (value.members.isEmpty() && value.unresolvedLegacyModules.isEmpty() &&
        (!value.explicitModules.isEmpty() || value.includeStandardBase)) {
        if (error) *error = QObject::tr(
            "Profiles must use exact physical files; legacy identity membership requires migration");
        return false;
    }
    for (MibProfileRecord &profile : customProfiles) if (profile.id == value.id) {
        const MibProfileRecord old = profile; profile = value;
        if (!profile.members.isEmpty())
            profile.explicitModules = uniqueSorted(MibProfileMemberIdentities(profile.members) +
                                                   profile.unresolvedLegacyModules);
        profile.explicitModules = uniqueSorted(profile.explicitModules);
        if (!persist(error)) { profile = old; return false; }
        return true;
    }
    return false;
}

bool MibProfileService::importCustomProfile(const QString &stableId, const QString &name,
                                             const QStringList &modules, QString *error)
{
    if (stableId.isEmpty() || isBuiltIn(stableId)) return false;
    for (MibProfileRecord &profile : customProfiles) if (profile.id == stableId) {
        const MibProfileRecord old = profile;
        profile.name = name; profile.type = MibProfileType::Custom;
        if (profile.members.isEmpty() && profile.unresolvedLegacyModules.isEmpty()) {
            profile.explicitModules = uniqueSorted(modules);
        } else {
            const QStringList exactIdentities = MibProfileMemberIdentities(profile.members);
            for (const QString &identity : modules)
                if (!exactIdentities.contains(identity) &&
                    !profile.unresolvedLegacyModules.contains(identity))
                    profile.unresolvedLegacyModules.append(identity);
            profile.unresolvedLegacyModules = uniqueSorted(profile.unresolvedLegacyModules);
            profile.explicitModules = uniqueSorted(exactIdentities +
                                                   profile.unresolvedLegacyModules);
        }
        if (!persist(error)) { profile = old; return false; }
        return true;
    }
    MibProfileRecord profile;
    profile.id = stableId; profile.name = name; profile.type = MibProfileType::Custom;
    profile.explicitModules = uniqueSorted(modules);
    customProfiles.append(profile);
    if (!persist(error)) { customProfiles.removeLast(); return false; }
    return true;
}

bool MibProfileService::migrateLegacyProfiles(const MibDependencyIndex &index, QString *error)
{
    const QList<MibProfileRecord> previous = customProfiles;
    bool changed = false;
    for (MibProfileRecord &profile : customProfiles) {
        const bool rawLegacy = MibProfileRequiresExactMigration(profile);
        if (!rawLegacy && profile.unresolvedLegacyModules.isEmpty()) continue;
        const MibProfileRecord before = profile;
        QStringList identities = rawLegacy ? profile.explicitModules
                                           : profile.unresolvedLegacyModules;
        if (rawLegacy && profile.includeStandardBase)
            identities.append(MibProfileDefinitions::standardsModules());
        identities = uniqueSorted(identities);
        QMap<QString, MibProfileMemberReason> reasons;
        for (const QString &identity : std::as_const(identities))
            reasons.insert(identity, MibProfileMemberReason::Added);
        QList<MibProfileMember> members = profile.members;
        QStringList unresolved;
        QSet<QString> processed;
        QSet<QString> seenPaths;
        for (const auto &member : std::as_const(members)) {
#ifdef Q_OS_WIN
            seenPaths.insert(QDir::fromNativeSeparators(member.canonicalPath).toLower());
#else
            seenPaths.insert(QDir::fromNativeSeparators(member.canonicalPath));
#endif
        }
        while (!identities.isEmpty()) {
            const QString identity = identities.takeFirst();
            if (processed.contains(identity)) continue;
            processed.insert(identity);
            const QList<MibIndexedProvider> providers = index.providersFor(identity);
            if (providers.size() != 1) {
                unresolved.append(identity);
                continue;
            }
            for (const QString &dependency : providers.first().imports) {
                if (!reasons.contains(dependency))
                    reasons.insert(dependency, MibProfileMemberReason::Dependency);
                if (!processed.contains(dependency)) identities.append(dependency);
            }
            const QString path = providers.first().canonicalPath;
#ifdef Q_OS_WIN
            const QString key = QDir::fromNativeSeparators(path).toLower();
#else
            const QString key = QDir::fromNativeSeparators(path);
#endif
            if (seenPaths.contains(key)) {
                if (reasons.value(identity) == MibProfileMemberReason::Added)
                    for (MibProfileMember &member : members) {
#ifdef Q_OS_WIN
                        const bool same = QDir::fromNativeSeparators(member.canonicalPath)
                            .compare(QDir::fromNativeSeparators(path), Qt::CaseInsensitive) == 0;
#else
                        const bool same = QDir::fromNativeSeparators(member.canonicalPath) ==
                            QDir::fromNativeSeparators(path);
#endif
                        if (same)
                            member.reason = MibProfileMemberReason::Added;
                    }
                continue;
            }
            const QList<MibProfileMember> exact = MibProfileMembersFromFiles(
                {path}, reasons.value(identity));
            if (exact.size() != 1 || exact.first().sha256 != providers.first().sha256) {
                unresolved.append(identity);
                continue;
            }
            seenPaths.insert(key);
            members.append(exact.first());
        }
        profile.members = members;
        profile.unresolvedLegacyModules = uniqueSorted(unresolved);
        profile.explicitModules = uniqueSorted(MibProfileMemberIdentities(members) +
                                               profile.unresolvedLegacyModules);
        profile.includeStandardBase = false;
        changed |= profile.members != before.members ||
            profile.unresolvedLegacyModules != before.unresolvedLegacyModules ||
            profile.explicitModules != before.explicitModules ||
            profile.includeStandardBase != before.includeStandardBase;
    }
    if (!changed) return true;
    if (persist(error)) return true;
    customProfiles = previous;
    return false;
}

bool MibProfileService::migrateLegacyFolderProfiles(QString *error)
{
    if (folderProfiles.isEmpty()) return true;
    const QList<MibProfileRecord> previousCustom = customProfiles;
    const QList<MibProfileRecord> previousFolders = folderProfiles;
    for (MibProfileRecord profile : std::as_const(folderProfiles)) {
        QStringList files;
        QDirIterator iterator(profile.directory, QDir::Files | QDir::Readable,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString path = iterator.next();
            if (MibCandidateFilter::accepts(QFileInfo(path).fileName())) files.append(path);
        }
        profile.members = MibProfileMembersFromFiles(files);
        profile.explicitModules = MibProfileMemberIdentities(profile.members);
        profile.type = MibProfileType::Custom;
        customProfiles.append(profile);
    }
    folderProfiles.clear();
    if (!persist(error)) {
        customProfiles = previousCustom;
        folderProfiles = previousFolders;
        return false;
    }
    return true;
}

bool MibProfileService::addFiles(const QString &id, const QStringList &files,
                                 MibProfileMemberReason reason, QString *error)
{
    const MibProfileRecord *current = find(id);
    if (!current || current->type != MibProfileType::Custom || isBuiltIn(id)) return false;
    if (files.isEmpty()) {
        if (error) *error = QObject::tr("No unambiguous exact MIB files were selected");
        return false;
    }
    QStringList diagnostics;
    const QList<MibProfileMember> additions = MibProfileMembersFromFiles(files, reason, &diagnostics);
    if (!diagnostics.isEmpty() && additions.isEmpty()) {
        if (error) *error = diagnostics.join(QStringLiteral("; "));
        return false;
    }
    MibProfileRecord changed = *current;
    for (const auto &member : additions) {
        const auto existing = std::find_if(changed.members.cbegin(), changed.members.cend(),
            [&member](const auto &candidate) {
#ifdef Q_OS_WIN
                return candidate.canonicalPath.compare(member.canonicalPath, Qt::CaseInsensitive) == 0;
#else
                return candidate.canonicalPath == member.canonicalPath;
#endif
            });
        if (existing == changed.members.cend()) changed.members.append(member);
    }
    changed.explicitModules = MibProfileMemberIdentities(changed.members);
    for (const auto &member : additions)
        for (const QString &identity : member.identities)
            changed.unresolvedLegacyModules.removeAll(identity);
    changed.explicitModules = uniqueSorted(changed.explicitModules +
                                           changed.unresolvedLegacyModules);
    return update(changed, error);
}

bool MibProfileService::addFolder(const QString &id, const QString &folder, QString *error)
{
    QStringList files;
    QDirIterator iterator(folder, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        if (MibCandidateFilter::accepts(QFileInfo(path).fileName())) files.append(path);
    }
    return addFiles(id, files, MibProfileMemberReason::Added, error);
}

MibProfileModuleAdditionResult MibAddModulesToEditableProfile(
    MibProfileService &service, const QString &profileId, const QStringList &moduleIdentities)
{
    MibProfileModuleAdditionResult result;
    const MibProfileRecord *current = service.find(profileId);
    if (!current) return result;
    if (current->type != MibProfileType::Custom) {
        result.status = MibProfileModuleAdditionStatus::ReadOnly;
        return result;
    }

    MibProfileRecord updated = *current;
    for (const QString &value : moduleIdentities) {
        const QString identity = value.trimmed();
        if (identity.isEmpty() || updated.explicitModules.contains(identity)) continue;
        updated.explicitModules.append(identity);
        result.addedModules.append(identity);
    }
    result.addedModules = uniqueSorted(result.addedModules);
    if (result.addedModules.isEmpty()) {
        result.status = MibProfileModuleAdditionStatus::Unchanged;
        return result;
    }
    if (!service.update(updated, &result.error)) {
        result.status = MibProfileModuleAdditionStatus::PersistenceFailed;
        result.addedModules.clear();
        return result;
    }
    result.status = MibProfileModuleAdditionStatus::Updated;
    return result;
}
