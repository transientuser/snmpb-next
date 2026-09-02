#ifndef MIBEFFECTIVEPLAN_H
#define MIBEFFECTIVEPLAN_H

#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QList>
#include <QMap>
#include <QStringList>

enum class MibRuntimeCollectionRole { Product, Standards, General, Pib };
enum class MibRuntimeStandardsPolicy { Excluded, Fallback };

struct MibRuntimeCollectionReference {
    QString id;
    MibRuntimeCollectionRole role = MibRuntimeCollectionRole::General;
    QString canonicalRoot;
    bool includesPibs = false;
};

struct MibRuntimeRootAlias {
    QString identity;
    QString collectionId;
    QString canonicalPath;
    QString sha256;
};

// Phase A transition value: immutable runtime instructions.
// It deliberately coexists with the provider-oriented Effective Plan until
// later phases move the loader to Profile-controlled search paths.
class MibProfileRuntimeConfiguration {
public:
    static constexpr int SchemaVersion = 2;

    QString profileId() const { return profileIdValue; }
    MibProfileType profileType() const { return profileTypeValue; }
    QString profileRevisionSha256() const { return profileRevisionSha256Value; }
    const QList<MibRuntimeCollectionReference> &orderedCollections() const { return orderedCollectionsValue; }
    const QStringList &explicitRoots() const { return explicitRootsValue; }
    MibRuntimeStandardsPolicy standardsPolicy() const { return standardsPolicyValue; }
    quint64 libraryGeneration() const { return libraryGenerationValue; }
    const QMap<QString, MibRuntimeRootAlias> &rootAliases() const { return rootAliasesValue; }
    const QList<MibProfileMember> &authorizedFiles() const { return authorizedFilesValue; }
    QString sha256() const { return sha256Value; }

private:
    friend class MibProfileRuntimeConfigurationBuilder;
    QString profileIdValue;
    MibProfileType profileTypeValue = MibProfileType::Custom;
    QString profileRevisionSha256Value;
    QList<MibRuntimeCollectionReference> orderedCollectionsValue;
    QStringList explicitRootsValue;
    MibRuntimeStandardsPolicy standardsPolicyValue = MibRuntimeStandardsPolicy::Excluded;
    quint64 libraryGenerationValue = 0;
    QMap<QString, MibRuntimeRootAlias> rootAliasesValue;
    QList<MibProfileMember> authorizedFilesValue;
    QString sha256Value;
};

class MibProfileRuntimeConfigurationBuilder
{
public:
    MibProfileRuntimeConfiguration build(
        const MibProfileRecord &profile, const MibDependencyIndex &library,
        QList<MibRuntimeCollectionReference> orderedCollections) const;
    static QByteArray canonicalBytes(const MibProfileRuntimeConfiguration &configuration);
};

struct MibRuntimePathEntry {
    QString canonicalPath;
    QString collectionId;
    MibRuntimeCollectionRole collectionRole = MibRuntimeCollectionRole::General;
    bool includesPibs = false;
};

class MibRuntimePathConfiguration {
public:
    static constexpr int SchemaVersion = 1;

    const QList<MibRuntimePathEntry> &entries() const { return entriesValue; }
    QStringList orderedPaths() const;
    bool isValid() const { return diagnosticsValue.isEmpty(); }
    const QStringList &diagnostics() const { return diagnosticsValue; }
    QString sha256() const { return sha256Value; }

private:
    friend class MibRuntimePathConfigurationBuilder;
    QList<MibRuntimePathEntry> entriesValue;
    QStringList diagnosticsValue;
    QString sha256Value;
};

class MibRuntimePathConfigurationBuilder
{
public:
    MibRuntimePathConfiguration derive(
        const MibProfileRuntimeConfiguration &configuration,
        const MibDependencyIndex &library) const;
    static QByteArray canonicalBytes(const MibRuntimePathConfiguration &configuration,
                                     const QString &runtimeConfigurationSha256);
};

enum class MibPlanMembershipReason { Explicit, Dependency, Missing, Ambiguous, PinFailure };
enum class MibPlanProviderReason {
    None, ExplicitPin, SingleProvider, EquivalentProviders, AutomaticProfileFolder,
    GlobalPrecedence, Ambiguous, InvalidPin
};

struct MibEffectivePlanMember {
    QString identity;
    MibPlanMembershipReason membershipReason = MibPlanMembershipReason::Dependency;
    MibPlanProviderReason providerReason = MibPlanProviderReason::None;
    MibProviderPin requestedPin;
    MibIndexedProvider provider;
    QList<MibIndexedProvider> alternatives;
    QStringList imports;
};

struct MibEffectivePlan {
    static constexpr int SchemaVersion = 2;
    static constexpr int PolicyVersion = 2;
    static constexpr int MaximumConvergencePasses = 8;

    QString profileId;
    QString profileName;
    MibProfileType profileType = MibProfileType::Custom;
    quint64 libraryGeneration = 0;
    QStringList explicitModules;
    QList<MibEffectivePlanMember> members;
    QStringList effectiveModules;
    QStringList dependencyModules;
    QStringList missingModules;
    QStringList ambiguousModules;
    QStringList pinFailureModules;
    QStringList nonConvergentModules;
    QStringList cycles;
    QStringList initialLoadOrder;
    int convergencePasses = 0;
    bool converged = true;
    QString sha256;
    MibProfileRuntimeConfiguration runtimeConfiguration;
    MibRuntimePathConfiguration runtimePaths;
    bool hasRuntimePaths = false;

    const MibEffectivePlanMember *member(const QString &identity) const;
    bool isComplete() const { return converged && missingModules.isEmpty() &&
        ambiguousModules.isEmpty() && pinFailureModules.isEmpty(); }
};

class MibEffectivePlanResolver
{
public:
    MibEffectivePlan resolve(const MibProfileRecord &profile,
                             const MibDependencyIndex &library) const;
    static QByteArray canonicalBytes(const MibEffectivePlan &plan);
};

#endif
