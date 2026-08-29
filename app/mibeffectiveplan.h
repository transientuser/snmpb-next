#ifndef MIBEFFECTIVEPLAN_H
#define MIBEFFECTIVEPLAN_H

#include "mibdependencyindex.h"
#include "mibprofile.h"

#include <QList>
#include <QStringList>

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
