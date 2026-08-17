#ifndef MIBEFFECTIVEPLAN_H
#define MIBEFFECTIVEPLAN_H

#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QList>
#include <QStringList>

enum class MibPlanMembershipReason { Explicit, Dependency, Missing, Ambiguous };
enum class MibPlanProviderReason {
    None, SingleProvider, EquivalentProviders, AutomaticProfileFolder,
    GlobalPrecedence, Ambiguous
};

struct MibEffectivePlanMember {
    QString identity;
    MibPlanMembershipReason membershipReason = MibPlanMembershipReason::Dependency;
    MibPlanProviderReason providerReason = MibPlanProviderReason::None;
    MibIndexedProvider provider;
    QList<MibIndexedProvider> alternatives;
    QStringList imports;
};

struct MibEffectivePlan {
    static constexpr int SchemaVersion = 1;
    static constexpr int PolicyVersion = 1;

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
    QStringList cycles;
    QStringList initialLoadOrder;
    QString sha256;

    const MibEffectivePlanMember *member(const QString &identity) const;
    bool isComplete() const { return missingModules.isEmpty() && ambiguousModules.isEmpty(); }
};

class MibEffectivePlanResolver
{
public:
    MibEffectivePlan resolve(const MibProfileRecord &profile,
                             const MibDependencyIndex &library) const;
    static QByteArray canonicalBytes(const MibEffectivePlan &plan);
};

#endif
