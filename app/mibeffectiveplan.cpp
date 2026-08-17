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

QJsonArray strings(const QStringList &values)
{
    QJsonArray result; for (const QString &value : values) result.append(value); return result;
}
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
    plan.explicitModules = profile.type == MibProfileType::All
        ? sortedUnique(library.moduleNames())
        : profile.type == MibProfileType::Standards
            ? sortedUnique(MibProfileDefinitions::standardsModules())
            : sortedUnique(profile.explicitModules);
    const QSet<QString> explicitSet(plan.explicitModules.cbegin(), plan.explicitModules.cend());
    QMap<QString, MibEffectivePlanMember> resolved;
    QStringList pending = plan.explicitModules;
    if ((profile.type == MibProfileType::Custom || profile.type == MibProfileType::Folder) &&
        profile.includeStandardBase)
        pending = sortedUnique(pending + MibProfileDefinitions::standardsModules());
    while (!pending.isEmpty()) {
        const QString identity = pending.takeFirst();
        if (resolved.contains(identity)) continue;
        MibEffectivePlanMember item;
        item.identity = identity;
        item.membershipReason = explicitSet.contains(identity)
            ? MibPlanMembershipReason::Explicit : MibPlanMembershipReason::Dependency;
        QList<MibIndexedProvider> choices = library.providersFor(identity);
        std::sort(choices.begin(), choices.end(), providerLess);
        item.alternatives = choices;
        if (choices.isEmpty()) {
            item.membershipReason = MibPlanMembershipReason::Missing;
            plan.missingModules.append(identity);
        } else {
            QList<MibIndexedProvider> eligible = choices;
            if (profile.type == MibProfileType::Folder && explicitSet.contains(identity)) {
                QList<MibIndexedProvider> local;
                for (const auto &choice : choices)
                    if (isWithin(choice.canonicalPath, profile.directory)) local.append(choice);
                if (!local.isEmpty()) {
                    eligible = local;
                    item.providerReason = MibPlanProviderReason::AutomaticProfileFolder;
                }
            }
            if (item.providerReason != MibPlanProviderReason::AutomaticProfileFolder) {
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
            if (eligible.size() > 1 && (hashes.size() != 1 || hashes.contains(QString()))) {
                item.membershipReason = MibPlanMembershipReason::Ambiguous;
                item.providerReason = MibPlanProviderReason::Ambiguous;
                plan.ambiguousModules.append(identity);
            } else {
                if (eligible.size() > 1 && item.providerReason != MibPlanProviderReason::AutomaticProfileFolder)
                    item.providerReason = MibPlanProviderReason::EquivalentProviders;
                item.provider = eligible.first();
                item.imports = sortedUnique(item.provider.imports);
                pending.append(item.imports);
                pending = sortedUnique(pending);
            }
        }
        resolved.insert(identity, item);
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
    QJsonArray members;
    for (const auto &item : plan.members) {
        QJsonObject value;
        value.insert(QStringLiteral("identity"), item.identity);
        value.insert(QStringLiteral("membership"), static_cast<int>(item.membershipReason));
        value.insert(QStringLiteral("providerReason"), static_cast<int>(item.providerReason));
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
    for (const QString &identity : std::as_const(pending))
        result.failures.append({identity, MibDependencyFailureKind::ParserSemanticFailure, QObject::tr("Planned provider did not materialize")});
    result.elapsedMsecs = timer.elapsed(); return result;
}
