#include "mibeffectiveplan.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <algorithm>
#include <functional>

namespace {
QStringList sortedUnique(QStringList values)
{
    values.removeAll(QString());
    values.removeDuplicates();
    std::sort(values.begin(), values.end(), [](const QString &a, const QString &b) {
        return QString::compare(a, b, Qt::CaseSensitive) < 0;
    });
    return values;
}

QString canonicalPath(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? QFileInfo(path).absoluteFilePath() : canonical);
}

bool isWithin(const QString &path, const QString &directory)
{
    if (directory.isEmpty()) return false;
    const QString candidate = QDir::fromNativeSeparators(canonicalPath(path));
    QString root = QDir::fromNativeSeparators(canonicalPath(directory));
    if (!root.endsWith('/')) root += '/';
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

bool providerLess(const MibIndexedProvider &a, const MibIndexedProvider &b)
{
    if (a.searchPathPrecedence != b.searchPathPrecedence)
        return a.searchPathPrecedence < b.searchPathPrecedence;
    const int folded = QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive);
    return folded == 0 ? a.canonicalPath < b.canonicalPath : folded < 0;
}


bool samePath(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return QString::compare(canonicalPath(a), canonicalPath(b), Qt::CaseInsensitive) == 0;
#else
    return canonicalPath(a) == canonicalPath(b);
#endif
}

bool pathWithin(const QString &path, const QString &directory)
{
    if (directory.isEmpty()) return false;
    const QString candidate = QDir::fromNativeSeparators(canonicalPath(path));
    QString root = QDir::fromNativeSeparators(canonicalPath(directory));
    if (!root.endsWith('/')) root += '/';
#ifdef Q_OS_WIN
    return candidate.startsWith(root, Qt::CaseInsensitive);
#else
    return candidate.startsWith(root, Qt::CaseSensitive);
#endif
}

QJsonArray strings(const QStringList &values)
{
    QJsonArray result; for (const QString &value : values) result.append(value); return result;
}
}

MibProfileRuntimeConfiguration MibProfileRuntimeConfigurationBuilder::build(
    const MibProfileRecord &profile, const MibDependencyIndex &library,
    QList<MibRuntimeCollectionReference> orderedCollections) const
{
    MibProfileRuntimeConfiguration configuration;
    configuration.profileIdValue = profile.id;
    configuration.profileTypeValue = profile.type;
    configuration.authorizedFilesValue = profile.members;
    std::sort(configuration.authorizedFilesValue.begin(), configuration.authorizedFilesValue.end(),
        [](const auto &a, const auto &b) {
            return QString::compare(a.canonicalPath, b.canonicalPath, Qt::CaseInsensitive) < 0;
        });
    const bool exactProfile = !profile.members.isEmpty() || !profile.unresolvedLegacyModules.isEmpty();
    configuration.explicitRootsValue = exactProfile
        ? MibProfileMemberIdentities(profile.members)
        : profile.type == MibProfileType::All
        ? sortedUnique(library.moduleNames())
        : profile.type == MibProfileType::Standards
            ? sortedUnique(MibProfileDefinitions::standardsModules())
            : sortedUnique(profile.explicitModules);
    configuration.standardsPolicyValue = !exactProfile && (profile.includeStandardBase ||
            profile.type == MibProfileType::Standards
        ) ? MibRuntimeStandardsPolicy::Fallback : MibRuntimeStandardsPolicy::Excluded;
    configuration.libraryGenerationValue = library.generation();

    for (auto &collection : orderedCollections) {
        collection.id = collection.id.trimmed();
        collection.canonicalRoot = canonicalPath(collection.canonicalRoot);
    }
    if (profile.type == MibProfileType::Folder && !profile.directory.isEmpty()) {
        const QString productRoot = canonicalPath(profile.directory);
        auto product = std::find_if(orderedCollections.begin(), orderedCollections.end(),
            [&productRoot](const auto &collection) {
                return collection.role == MibRuntimeCollectionRole::Product &&
                    samePath(collection.canonicalRoot, productRoot);
            });
        if (product == orderedCollections.end())
            orderedCollections.prepend({profile.id + QStringLiteral("/product"),
                MibRuntimeCollectionRole::Product, productRoot, false});
    }
    configuration.orderedCollectionsValue = orderedCollections;

    if (!configuration.authorizedFilesValue.isEmpty()) {
        for (const auto &member : std::as_const(configuration.authorizedFilesValue)) {
            const QString providerPath = canonicalPath(member.canonicalPath);
            const auto collection = std::find_if(configuration.orderedCollectionsValue.cbegin(),
                configuration.orderedCollectionsValue.cend(), [&providerPath](const auto &candidate) {
                    return pathWithin(providerPath, candidate.canonicalRoot);
                });
            if (collection == configuration.orderedCollectionsValue.cend()) continue;
            for (const QString &identity : member.identities)
                if (!configuration.rootAliasesValue.contains(identity))
                    configuration.rootAliasesValue.insert(identity, {identity, collection->id,
                        providerPath, member.sha256});
        }
    } else {

    QMap<QString, int> identitiesPerFile;
    for (const auto &file : library.files())
        identitiesPerFile.insert(canonicalPath(file.canonicalPath), file.importsByModule.size());
    for (const QString &identity : configuration.explicitRootsValue) {
        const auto providers = library.providersFor(identity);
        for (const auto &collection : configuration.orderedCollectionsValue) {
            QList<MibIndexedProvider> inCollection;
            for (const auto &provider : providers)
                if (pathWithin(provider.canonicalPath, collection.canonicalRoot))
                    inCollection.append(provider);
            if (inCollection.isEmpty()) continue;
            std::sort(inCollection.begin(), inCollection.end(), providerLess);
            const auto &provider = inCollection.first();
            const QString filename = QFileInfo(provider.canonicalPath).fileName();
            const QString stem = QFileInfo(provider.canonicalPath).completeBaseName();
            const bool nativeName = filename.compare(identity, Qt::CaseInsensitive) == 0 ||
                stem.compare(identity, Qt::CaseInsensitive) == 0;
            const QString providerPath = canonicalPath(provider.canonicalPath);
            if (!nativeName || identitiesPerFile.value(providerPath) > 1)
                configuration.rootAliasesValue.insert(identity, {identity, collection.id,
                    providerPath, provider.sha256});
            break;
        }
    }

    }

    if (exactProfile) {
        QMap<QString, QStringList> selectedImports;
        for (const QString &identity : std::as_const(configuration.explicitRootsValue)) {
            const auto alias = configuration.rootAliasesValue.constFind(identity);
            if (alias == configuration.rootAliasesValue.cend()) continue;
            for (const auto &provider : library.providersFor(identity)) {
                if (samePath(provider.canonicalPath, alias->canonicalPath) &&
                    provider.sha256 == alias->sha256) {
                    selectedImports.insert(identity, sortedUnique(provider.imports));
                    break;
                }
            }
        }
        const QSet<QString> authorized(configuration.explicitRootsValue.cbegin(),
                                       configuration.explicitRootsValue.cend());
        QSet<QString> visited;
        QSet<QString> active;
        QStringList dependencyFirst;
        std::function<void(const QString &)> visit = [&](const QString &identity) {
            if (visited.contains(identity) || active.contains(identity)) return;
            active.insert(identity);
            for (const QString &dependency : selectedImports.value(identity))
                if (authorized.contains(dependency)) visit(dependency);
            active.remove(identity);
            visited.insert(identity);
            dependencyFirst.append(identity);
        };
        for (const QString &identity : std::as_const(configuration.explicitRootsValue)) visit(identity);
        configuration.explicitRootsValue = dependencyFirst;
    }

    QJsonObject revision;
    revision.insert(QStringLiteral("id"), profile.id);
    revision.insert(QStringLiteral("type"), static_cast<int>(profile.type));
    revision.insert(QStringLiteral("roots"), strings(configuration.explicitRootsValue));
    revision.insert(QStringLiteral("standards"), static_cast<int>(configuration.standardsPolicyValue));
    configuration.profileRevisionSha256Value = QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(revision).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
    configuration.sha256Value = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(configuration), QCryptographicHash::Sha256).toHex());
    return configuration;
}

QByteArray MibProfileRuntimeConfigurationBuilder::canonicalBytes(
    const MibProfileRuntimeConfiguration &configuration)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), MibProfileRuntimeConfiguration::SchemaVersion);
    root.insert(QStringLiteral("profileId"), configuration.profileIdValue);
    root.insert(QStringLiteral("profileType"), static_cast<int>(configuration.profileTypeValue));
    root.insert(QStringLiteral("profileRevision"), configuration.profileRevisionSha256Value);
    root.insert(QStringLiteral("roots"), strings(configuration.explicitRootsValue));
    root.insert(QStringLiteral("standards"), static_cast<int>(configuration.standardsPolicyValue));
    root.insert(QStringLiteral("libraryGeneration"), QString::number(configuration.libraryGenerationValue));
    QJsonArray collections;
    for (const auto &collection : configuration.orderedCollectionsValue) {
        collections.append(QJsonObject{{QStringLiteral("id"), collection.id},
            {QStringLiteral("role"), static_cast<int>(collection.role)},
            {QStringLiteral("root"), QDir::fromNativeSeparators(collection.canonicalRoot)},
            {QStringLiteral("pibs"), collection.includesPibs}});
    }
    root.insert(QStringLiteral("collections"), collections);
    QJsonArray aliases;
    for (auto it = configuration.rootAliasesValue.cbegin(); it != configuration.rootAliasesValue.cend(); ++it) {
        const auto &alias = it.value();
        aliases.append(QJsonObject{{QStringLiteral("identity"), alias.identity},
            {QStringLiteral("collection"), alias.collectionId},
            {QStringLiteral("path"), QDir::fromNativeSeparators(alias.canonicalPath)},
            {QStringLiteral("hash"), alias.sha256}});
    }
    root.insert(QStringLiteral("aliases"), aliases);
    QJsonArray files;
    for (const auto &member : configuration.authorizedFilesValue)
        files.append(QJsonObject{{QStringLiteral("path"), QDir::fromNativeSeparators(member.canonicalPath)},
            {QStringLiteral("hash"), member.sha256},
            {QStringLiteral("identities"), strings(sortedUnique(member.identities))},
            {QStringLiteral("reason"), static_cast<int>(member.reason)}});
    root.insert(QStringLiteral("authorizedFiles"), files);
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QStringList MibRuntimePathConfiguration::orderedPaths() const
{
    QStringList result;
    for (const auto &entry : entriesValue) result.append(entry.canonicalPath);
    return result;
}

MibRuntimePathConfiguration MibRuntimePathConfigurationBuilder::derive(
    const MibProfileRuntimeConfiguration &configuration,
    const MibDependencyIndex &library) const
{
    MibRuntimePathConfiguration result;
    const auto &collections = configuration.orderedCollections();
    if (configuration.standardsPolicy() == MibRuntimeStandardsPolicy::Fallback &&
        std::none_of(collections.cbegin(), collections.cend(), [](const auto &collection) {
            return collection.role == MibRuntimeCollectionRole::Standards;
        })) {
        result.diagnosticsValue.append(QObject::tr(
            "Standards fallback requires an authorized Standards collection"));
    }

    QSet<QString> collectionIds;
    QSet<QString> seenPaths;
    const auto pathKey = [](const QString &path) {
#ifdef Q_OS_WIN
        return QDir::fromNativeSeparators(path).toLower();
#else
        return QDir::fromNativeSeparators(path);
#endif
    };
    const auto append = [&result, &seenPaths, &pathKey](
        const QString &path, const MibRuntimeCollectionReference &collection) {
        const QString canonical = canonicalPath(path);
        const QString key = pathKey(canonical);
        if (seenPaths.contains(key)) return;
        seenPaths.insert(key);
        result.entriesValue.append({canonical, collection.id, collection.role,
                                    collection.includesPibs});
    };

    for (const auto &collection : collections) {
        if (collection.id.isEmpty() || collection.canonicalRoot.isEmpty()) {
            result.diagnosticsValue.append(QObject::tr(
                "Runtime collection identity and root are required"));
            continue;
        }
        if (collectionIds.contains(collection.id)) {
            result.diagnosticsValue.append(QObject::tr(
                "Duplicate runtime collection identity: %1").arg(collection.id));
            continue;
        }
        collectionIds.insert(collection.id);
        if (collection.role == MibRuntimeCollectionRole::Pib && !collection.includesPibs)
            continue;

        const QString root = canonicalPath(collection.canonicalRoot);
        const QFileInfo rootInfo(root);
        if (!rootInfo.isDir() || !rootInfo.isReadable()) {
            result.diagnosticsValue.append(QObject::tr(
                "Runtime collection is not a readable directory: %1").arg(root));
            continue;
        }
        append(root, collection);
        QStringList descendants;
        for (const auto &file : library.files()) {
            if (!samePath(file.canonicalPath, root) && !pathWithin(file.canonicalPath, root))
                continue;
            const QFileInfo directory(QFileInfo(file.canonicalPath).absolutePath());
            if (directory.isDir() && directory.isReadable())
                descendants.append(canonicalPath(directory.absoluteFilePath()));
        }
        descendants.removeDuplicates();
        std::sort(descendants.begin(), descendants.end(), [](const QString &a, const QString &b) {
            const int folded = QString::compare(QDir::fromNativeSeparators(a),
                QDir::fromNativeSeparators(b), Qt::CaseInsensitive);
            return folded == 0 ? a < b : folded < 0;
        });
        for (const QString &directory : std::as_const(descendants)) append(directory, collection);
    }

    for (auto it = configuration.rootAliases().cbegin();
         it != configuration.rootAliases().cend(); ++it) {
        const MibRuntimeRootAlias &alias = it.value();
        const auto collection = std::find_if(collections.cbegin(), collections.cend(),
            [&alias](const auto &candidate) { return candidate.id == alias.collectionId; });
        if (collection == collections.cend() ||
            (!samePath(alias.canonicalPath, collection->canonicalRoot) &&
             !pathWithin(alias.canonicalPath, collection->canonicalRoot))) {
            result.diagnosticsValue.append(QObject::tr(
                "Root alias for %1 is outside its authorized collection").arg(alias.identity));
            continue;
        }
        const QString aliasDirectory = canonicalPath(QFileInfo(alias.canonicalPath).absolutePath());
        if (!seenPaths.contains(pathKey(aliasDirectory)))
            result.diagnosticsValue.append(QObject::tr(
                "Root alias for %1 is outside the derived runtime paths").arg(alias.identity));
    }
    result.diagnosticsValue.removeDuplicates();
    result.sha256Value = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(result, configuration.sha256()), QCryptographicHash::Sha256).toHex());
    return result;
}

QByteArray MibRuntimePathConfigurationBuilder::canonicalBytes(
    const MibRuntimePathConfiguration &configuration,
    const QString &runtimeConfigurationSha256)
{
    QJsonObject root{{QStringLiteral("schema"), MibRuntimePathConfiguration::SchemaVersion},
                     {QStringLiteral("runtimeConfiguration"), runtimeConfigurationSha256}};
    QJsonArray paths;
    for (const auto &entry : configuration.entriesValue)
        paths.append(QJsonObject{{QStringLiteral("path"), QDir::fromNativeSeparators(entry.canonicalPath)},
            {QStringLiteral("collection"), entry.collectionId},
            {QStringLiteral("role"), static_cast<int>(entry.collectionRole)},
            {QStringLiteral("pibs"), entry.includesPibs}});
    root.insert(QStringLiteral("paths"), paths);
    root.insert(QStringLiteral("diagnostics"), strings(configuration.diagnosticsValue));
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

const MibEffectivePlanMember *MibEffectivePlan::member(const QString &identity) const
{
    for (const auto &item : members) if (item.identity == identity) return &item;
    return nullptr;
}

MibEffectivePlan MibEffectivePlanResolver::resolve(const MibProfileRecord &profile,
                                                    const MibDependencyIndex &library) const
{
    MibEffectivePlan plan;
    plan.profileId = profile.id;
    plan.profileName = profile.name;
    plan.profileType = profile.type;
    plan.libraryGeneration = library.generation();
    const bool exactProfile = !profile.members.isEmpty() || !profile.unresolvedLegacyModules.isEmpty();
    plan.explicitModules = exactProfile ? MibProfileMemberIdentities(profile.members)
        : profile.type == MibProfileType::All
        ? sortedUnique(library.moduleNames())
        : profile.type == MibProfileType::Standards
            ? sortedUnique(MibProfileDefinitions::standardsModules())
            : sortedUnique(profile.explicitModules);
    const QSet<QString> explicitSet(plan.explicitModules.cbegin(), plan.explicitModules.cend());
    QMap<QString, MibEffectivePlanMember> resolved;
    QStringList pending = plan.explicitModules;
    if (exactProfile) plan.missingModules.append(profile.unresolvedLegacyModules);
    if (!exactProfile && (profile.type == MibProfileType::Custom || profile.type == MibProfileType::Folder) &&
        profile.includeStandardBase)
        pending = sortedUnique(pending + MibProfileDefinitions::standardsModules());
    while (!pending.isEmpty() && plan.convergencePasses < MibEffectivePlan::MaximumConvergencePasses) {
        ++plan.convergencePasses;
        const QStringList pass = sortedUnique(pending);
        pending.clear();
        for (const QString &identity : pass) {
        if (resolved.contains(identity)) continue;
        MibEffectivePlanMember item;
        item.identity = identity;
        item.membershipReason = explicitSet.contains(identity)
            ? MibPlanMembershipReason::Explicit : MibPlanMembershipReason::Dependency;
        QList<MibIndexedProvider> choices = library.providersFor(identity);
        std::sort(choices.begin(), choices.end(), providerLess);
        item.alternatives = choices;
        if (exactProfile) {
            QList<MibIndexedProvider> selected;
            for (const auto &member : profile.members) {
                if (!member.identities.contains(identity)) continue;
                for (const auto &choice : choices)
                    if (samePath(choice.canonicalPath, member.canonicalPath) &&
                        choice.sha256 == member.sha256) selected.append(choice);
            }
            choices = selected;
        }
        const auto pin = profile.providerPins.constFind(identity);
        if (pin != profile.providerPins.cend()) item.requestedPin = *pin;
        if (choices.isEmpty()) {
            if (pin != profile.providerPins.cend()) {
                item.membershipReason = MibPlanMembershipReason::PinFailure;
                item.providerReason = MibPlanProviderReason::InvalidPin;
                plan.pinFailureModules.append(identity);
            } else {
                item.membershipReason = MibPlanMembershipReason::Missing;
                plan.missingModules.append(identity);
            }
        } else {
            QList<MibIndexedProvider> eligible = choices;
            if (exactProfile) item.providerReason = MibPlanProviderReason::ExplicitPin;
            if (pin != profile.providerPins.cend()) {
                eligible.clear();
                for (const auto &choice : choices)
                    if (samePath(choice.canonicalPath, pin->canonicalPath) &&
                        choice.sha256 == pin->sha256) eligible.append(choice);
                if (eligible.size() == 1) {
                    item.providerReason = MibPlanProviderReason::ExplicitPin;
                } else {
                    item.membershipReason = MibPlanMembershipReason::PinFailure;
                    item.providerReason = MibPlanProviderReason::InvalidPin;
                    plan.pinFailureModules.append(identity);
                }
            } else if (profile.type == MibProfileType::Folder && explicitSet.contains(identity)) {
                QList<MibIndexedProvider> local;
                for (const auto &choice : choices)
                    if (isWithin(choice.canonicalPath, profile.directory)) local.append(choice);
                if (!local.isEmpty()) {
                    eligible = local;
                    item.providerReason = MibPlanProviderReason::AutomaticProfileFolder;
                }
            }
            if (item.providerReason != MibPlanProviderReason::ExplicitPin &&
                item.providerReason != MibPlanProviderReason::AutomaticProfileFolder &&
                item.providerReason != MibPlanProviderReason::InvalidPin) {
                const int best = eligible.first().searchPathPrecedence;
                QList<MibIndexedProvider> preferred;
                for (const auto &choice : eligible)
                    if (choice.searchPathPrecedence == best) preferred.append(choice);
                eligible = preferred;
                item.providerReason = choices.size() == 1
                    ? MibPlanProviderReason::SingleProvider : MibPlanProviderReason::GlobalPrecedence;
            }
            QSet<QString> hashes;
            for (const auto &choice : eligible) hashes.insert(choice.sha256);
            if (item.providerReason == MibPlanProviderReason::InvalidPin) {
                // An explicit but stale/wrong pin is a hard plan finding. Do not
                // silently substitute a different conflicting provider.
            } else if (eligible.size() > 1 && (hashes.size() != 1 || hashes.contains(QString()))) {
                item.membershipReason = MibPlanMembershipReason::Ambiguous;
                item.providerReason = MibPlanProviderReason::Ambiguous;
                plan.ambiguousModules.append(identity);
            } else {
                if (eligible.size() > 1 && item.providerReason != MibPlanProviderReason::AutomaticProfileFolder)
                    item.providerReason = MibPlanProviderReason::EquivalentProviders;
                item.provider = eligible.first();
                item.imports = sortedUnique(item.provider.imports);
                if (exactProfile) {
                    for (const QString &dependency : item.imports)
                        if (!explicitSet.contains(dependency)) plan.missingModules.append(dependency);
                } else pending.append(item.imports);
            }
        }
        resolved.insert(identity, item);
        }
        pending = sortedUnique(pending);
        for (auto it = pending.begin(); it != pending.end(); ) {
            if (resolved.contains(*it)) it = pending.erase(it);
            else ++it;
        }
    }
    pending = sortedUnique(pending);
    if (!pending.isEmpty()) {
        plan.converged = false;
        plan.nonConvergentModules = pending;
    }

    plan.members = resolved.values();
    for (const auto &item : plan.members) {
        if (item.membershipReason == MibPlanMembershipReason::Explicit ||
            item.membershipReason == MibPlanMembershipReason::Dependency) {
            plan.effectiveModules.append(item.identity);
            if (item.membershipReason == MibPlanMembershipReason::Dependency)
                plan.dependencyModules.append(item.identity);
        }
    }
    plan.effectiveModules = sortedUnique(plan.effectiveModules);
    plan.dependencyModules = sortedUnique(plan.dependencyModules);
    plan.missingModules = sortedUnique(plan.missingModules);
    plan.ambiguousModules = sortedUnique(plan.ambiguousModules);
    plan.pinFailureModules = sortedUnique(plan.pinFailureModules);

    QSet<QString> visited, active;
    std::function<void(const QString &, QStringList)> visit = [&](const QString &identity, QStringList stack) {
        if (visited.contains(identity)) return;
        if (active.contains(identity)) {
            const int start = stack.indexOf(identity);
            QStringList cycle = start >= 0 ? stack.mid(start) : stack;
            cycle.append(identity);
            plan.cycles.append(cycle.join(QStringLiteral(" -> ")));
            return;
        }
        active.insert(identity); stack.append(identity);
        const auto *item = plan.member(identity);
        if (item) for (const QString &dependency : item->imports) visit(dependency, stack);
        active.remove(identity); visited.insert(identity);
        if (item && (item->membershipReason == MibPlanMembershipReason::Explicit ||
                     item->membershipReason == MibPlanMembershipReason::Dependency))
            plan.initialLoadOrder.append(identity);
    };
    for (const QString &root : plan.explicitModules) visit(root, {});
    for (const QString &identity : plan.effectiveModules) visit(identity, {});
    plan.cycles = sortedUnique(plan.cycles);
    plan.initialLoadOrder.removeDuplicates();
    plan.sha256 = QString::fromLatin1(QCryptographicHash::hash(
        canonicalBytes(plan), QCryptographicHash::Sha256).toHex());
    return plan;
}

QByteArray MibEffectivePlanResolver::canonicalBytes(const MibEffectivePlan &plan)
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), MibEffectivePlan::SchemaVersion);
    root.insert(QStringLiteral("policy"), MibEffectivePlan::PolicyVersion);
    root.insert(QStringLiteral("profileType"), static_cast<int>(plan.profileType));
    root.insert(QStringLiteral("libraryGeneration"), QString::number(plan.libraryGeneration));
    root.insert(QStringLiteral("explicit"), strings(plan.explicitModules));
    root.insert(QStringLiteral("converged"), plan.converged);
    root.insert(QStringLiteral("authorityError"), plan.authorityError);
    root.insert(QStringLiteral("convergencePasses"), plan.convergencePasses);
    root.insert(QStringLiteral("nonConvergent"), strings(plan.nonConvergentModules));
    QJsonArray members;
    for (const auto &item : plan.members) {
        QJsonObject value;
        value.insert(QStringLiteral("identity"), item.identity);
        value.insert(QStringLiteral("membership"), static_cast<int>(item.membershipReason));
        value.insert(QStringLiteral("providerReason"), static_cast<int>(item.providerReason));
        value.insert(QStringLiteral("pinPath"), QDir::fromNativeSeparators(item.requestedPin.canonicalPath));
        value.insert(QStringLiteral("pinHash"), item.requestedPin.sha256);
        value.insert(QStringLiteral("path"), QDir::fromNativeSeparators(item.provider.canonicalPath));
        value.insert(QStringLiteral("hash"), item.provider.sha256);
        value.insert(QStringLiteral("origin"), item.provider.origin);
        value.insert(QStringLiteral("precedence"), item.provider.searchPathPrecedence);
        value.insert(QStringLiteral("imports"), strings(item.imports));
        QJsonArray alternatives;
        for (const auto &provider : item.alternatives) {
            QJsonObject alternative;
            alternative.insert(QStringLiteral("path"), QDir::fromNativeSeparators(provider.canonicalPath));
            alternative.insert(QStringLiteral("hash"), provider.sha256);
            alternative.insert(QStringLiteral("origin"), provider.origin);
            alternative.insert(QStringLiteral("precedence"), provider.searchPathPrecedence);
            alternative.insert(QStringLiteral("imports"), strings(sortedUnique(provider.imports)));
            alternatives.append(alternative);
        }
        value.insert(QStringLiteral("alternatives"), alternatives);
        members.append(value);
    }
    root.insert(QStringLiteral("members"), members);
    root.insert(QStringLiteral("order"), strings(plan.initialLoadOrder));
    root.insert(QStringLiteral("cycles"), strings(plan.cycles));
    if (plan.hasRuntimePaths) {
        root.insert(QStringLiteral("runtimeConfiguration"), plan.runtimeConfiguration.sha256());
        root.insert(QStringLiteral("runtimePaths"), plan.runtimePaths.sha256());
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

MibDependencyCheckResult MibBoundedDependencyLoader::load(
    const MibEffectivePlan &plan, const LoadFile &loadFile) const
{
    QElapsedTimer timer; timer.start(); MibDependencyCheckResult result;
    result.requested = plan.explicitModules; result.dependencies = plan.dependencyModules;
    QStringList pending = plan.initialLoadOrder;
    const int maximumPasses = pending.size() + 2;
    while (!pending.isEmpty() && result.passes < maximumPasses) {
        ++result.passes; QStringList next; int progress = 0;
        for (const QString &identity : std::as_const(pending)) {
            const MibEffectivePlanMember *member = plan.member(identity);
            if (!member || member->provider.canonicalPath.isEmpty()) { next.append(identity); continue; }
            const MibDependencyLoadAttempt attempt = loadFile(member->provider.canonicalPath, identity);
            if (attempt.success) {
                ++progress; result.loaded.append(attempt.loadedModuleNames.isEmpty()
                    ? QStringList{identity} : attempt.loadedModuleNames);
            } else next.append(identity);
        }
        pending = next;
        if (progress == 0) { ++result.noProgressStops; break; }
    }
    result.loaded.removeDuplicates();
    for (const QString &identity : std::as_const(plan.missingModules))
        result.failures.append({identity, MibDependencyFailureKind::MissingProvider, QObject::tr("No indexed provider")});
    for (const QString &identity : std::as_const(plan.ambiguousModules))
        result.failures.append({identity, MibDependencyFailureKind::AmbiguousProvider, QObject::tr("Provider conflict")});
    for (const QString &identity : std::as_const(plan.pinFailureModules))
        result.failures.append({identity, MibDependencyFailureKind::AmbiguousProvider, QObject::tr("Explicit provider pin is invalid")});
    for (const QString &identity : std::as_const(plan.nonConvergentModules))
        result.failures.append({identity, MibDependencyFailureKind::DependencyUnresolved, QObject::tr("Provider/dependency planning did not converge")});
    for (const QString &identity : std::as_const(pending))
        result.failures.append({identity, MibDependencyFailureKind::ParserSemanticFailure, QObject::tr("Planned provider did not materialize")});
    result.elapsedMsecs = timer.elapsed(); return result;
}
