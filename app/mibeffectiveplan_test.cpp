#include "mibeffectiveplan.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <iostream>

namespace {
bool check(bool value, const char *message) { if (!value) std::cerr << "FAIL: " << message << '\n'; return value; }
bool writeFile(const QString &path, const QByteArray &data) { QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(data) == data.size(); }
QByteArray mib(const QString &identity, const QStringList &imports = {}) {
    QByteArray data = identity.toUtf8() + " DEFINITIONS ::= BEGIN\n";
    if (!imports.isEmpty()) data += "IMPORTS value FROM " + imports.join("\n other FROM ").toUtf8() + ";\n";
    return data + "END\n";
}
}

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv); bool ok = true; QTemporaryDir temp;
    const QString standards = QDir(temp.path()).filePath("Standards/IETF");
    const QString productA = QDir(temp.path()).filePath("Vendor/Product A");
    const QString productB = QDir(temp.path()).filePath("Vendor/Product B");
    for (const QString &path : {standards, productA, productB}) QDir().mkpath(path);
    writeFile(QDir(standards).filePath("dep.mib"), mib("DEPENDENCY-MIB"));
    writeFile(QDir(standards).filePath("global-root.mib"), mib("ROOT-MIB", {"DEPENDENCY-MIB"}));
    writeFile(QDir(productA).filePath("odd-name.mib"), mib("ROOT-MIB", {"PRODUCT-ONLY-MIB"}));
    writeFile(QDir(productA).filePath("multi.mib"), mib("PRODUCT-ONLY-MIB", {"CYCLE-A"}) + mib("SECOND-IDENTITY-MIB"));
    writeFile(QDir(productA).filePath("cycle-a.mib"), mib("CYCLE-A", {"CYCLE-B"}));
    writeFile(QDir(productA).filePath("cycle-b.mib"), mib("CYCLE-B", {"CYCLE-A"}));
    writeFile(QDir(productA).filePath("missing.mib"), mib("MISSING-USER-MIB", {"ABSENT-MIB"}));
    const QByteArray equivalent = mib("EQUIVALENT-MIB");
    writeFile(QDir(productA).filePath("equivalent-a.mib"), equivalent);
    writeFile(QDir(productA).filePath("equivalent-b.mib"), equivalent);
    writeFile(QDir(productA).filePath("conflict-a.mib"), mib("CONFLICT-MIB") + "-- A\n");
    writeFile(QDir(productA).filePath("conflict-b.mib"), mib("CONFLICT-MIB") + "-- B\n");
    writeFile(QDir(productB).filePath("same-identity.mib"), mib("ROOT-MIB", {"DEPENDENCY-MIB"}));
    MibDependencyIndex index(QDir(temp.path()).filePath("index.json")); index.update({standards, productA, productB});

    MibProfileRecord automatic; automatic.id = "automatic-a"; automatic.name = "Vendor Product A";
    automatic.type = MibProfileType::Folder; automatic.directory = productA;
    automatic.explicitModules = {"SECOND-IDENTITY-MIB", "ROOT-MIB", "MISSING-USER-MIB", "EQUIVALENT-MIB", "CONFLICT-MIB"};
    const auto first = MibEffectivePlanResolver().resolve(automatic, index);
    const auto second = MibEffectivePlanResolver().resolve(automatic, index);
    ok &= check(first.sha256 == second.sha256 && MibEffectivePlanResolver::canonicalBytes(first) == MibEffectivePlanResolver::canonicalBytes(second), "same inputs are byte-identical");
    ok &= check(first.explicitModules == QStringList({"CONFLICT-MIB", "EQUIVALENT-MIB", "MISSING-USER-MIB", "ROOT-MIB", "SECOND-IDENTITY-MIB"}), "explicit identity canonicalization");
    ok &= check(first.member("ROOT-MIB") && first.member("ROOT-MIB")->provider.canonicalPath.contains("Product A") && first.member("ROOT-MIB")->providerReason == MibPlanProviderReason::AutomaticProfileFolder, "automatic product provider policy");
    ok &= check(first.member("ROOT-MIB")->imports == QStringList{"PRODUCT-ONLY-MIB"} && first.dependencyModules.contains("PRODUCT-ONLY-MIB") && !first.dependencyModules.contains("DEPENDENCY-MIB"), "provider-specific closure");
    ok &= check(first.member("PRODUCT-ONLY-MIB")->membershipReason == MibPlanMembershipReason::Dependency && first.member("ROOT-MIB")->membershipReason == MibPlanMembershipReason::Explicit, "membership reasons");
    ok &= check(first.missingModules == QStringList{"ABSENT-MIB"} && first.ambiguousModules == QStringList{"CONFLICT-MIB"}, "missing and ambiguous findings");
    ok &= check(!first.cycles.isEmpty() && first.initialLoadOrder.contains("CYCLE-A") && first.initialLoadOrder.contains("CYCLE-B"), "cycle termination");
    MibProfileRecord all = MibProfileDefinitions::builtIns().first();
    const auto allPlan = MibEffectivePlanResolver().resolve(all, index);
    const auto repeatedAllPlan = MibEffectivePlanResolver().resolve(all, index);
    ok &= check(all.id == MibProfileDefinitions::allId() && all.type == MibProfileType::All &&
                allPlan.profileId == all.id && allPlan.explicitModules == index.moduleNames() &&
                allPlan.sha256 == repeatedAllPlan.sha256,
                "All MIBs is a deterministic built-in Profile backed by an Effective Plan");
    QMap<QString, int> attempts;
    const auto retried = MibBoundedDependencyLoader().load(first,
        [&attempts](const QString &, const QString &identity) {
            MibDependencyLoadAttempt attempt; const int count = ++attempts[identity];
            if (identity != "ROOT-MIB" || count > 1) {
                attempt.success = true; attempt.loadedModuleNames = {identity};
            }
            return attempt;
        });
    ok &= check(attempts.value("ROOT-MIB") == 2 && retried.passes == 2 &&
                retried.failures.size() == first.missingModules.size() + first.ambiguousModules.size(),
                "effective plan materialization retains bounded fixed-point retry");

    MibProfileRecord custom = automatic; custom.id = "custom"; custom.type = MibProfileType::Custom;
    custom.directory = productA; custom.explicitModules = {"ROOT-MIB"}; custom.includeStandardBase = true;
    const auto customPlan = MibEffectivePlanResolver().resolve(custom, index);
    ok &= check(customPlan.member("ROOT-MIB") && !customPlan.member("ROOT-MIB")->provider.canonicalPath.contains("Product A") && customPlan.dependencyModules.contains("DEPENDENCY-MIB"), "custom has no folder affinity");
    ok &= check(customPlan.sha256 != first.sha256,
                "changing selected provider and provider-specific imports changes plan hash");
    ok &= check(customPlan.explicitModules == QStringList{"ROOT-MIB"},
                "computed standards base does not mutate custom explicit membership");
    MibProfileRecord reordered = automatic; std::reverse(reordered.explicitModules.begin(), reordered.explicitModules.end());
    const auto reorderedPlan = MibEffectivePlanResolver().resolve(reordered, index);
    ok &= check(first.sha256 == reorderedPlan.sha256 && first.initialLoadOrder == reorderedPlan.initialLoadOrder, "insertion order independence");
    MibDependencyIndex reorderedIndex(QDir(temp.path()).filePath("reordered-index.json"));
    reorderedIndex.update({productB, productA, standards});
    const auto filesystemReorderedPlan = MibEffectivePlanResolver().resolve(automatic, reorderedIndex);
    ok &= check(first.sha256 == filesystemReorderedPlan.sha256 &&
                first.initialLoadOrder == filesystemReorderedPlan.initialLoadOrder,
                "filesystem and provider insertion order do not affect plan");

    const QString oldHash = first.sha256;
    writeFile(QDir(productA).filePath("odd-name.mib"), mib("ROOT-MIB", {"PRODUCT-ONLY-MIB"}) + "-- changed\n");
    QFile changed(QDir(productA).filePath("odd-name.mib")); changed.setFileTime(QDateTime::currentDateTime().addSecs(2), QFileDevice::FileModificationTime);
    index.update({standards, productA, productB});
    ok &= check(MibEffectivePlanResolver().resolve(automatic, index).sha256 != oldHash, "provider content changes hash");

    QTemporaryDir largeTemp; const QString largeRoot = QDir(largeTemp.path()).filePath("Vendor/Large Product"); QDir().mkpath(largeRoot);
    QStringList explicitLarge;
    for (int i = 0; i < 250; ++i) {
        const QString identity = QStringLiteral("LARGE-%1-MIB").arg(i, 3, 10, QLatin1Char('0'));
        QStringList imports; if (i > 0) imports.append(QStringLiteral("LARGE-%1-MIB").arg(i - 1, 3, 10, QLatin1Char('0')));
        if (i > 2 && i % 17 == 0) imports.append("LARGE-000-MIB");
        writeFile(QDir(largeRoot).filePath(QStringLiteral("file-%1.mib").arg(i)), mib(identity, imports)); explicitLarge.append(identity);
    }
    MibDependencyIndex largeIndex(QDir(largeTemp.path()).filePath("index.json")); largeIndex.update({largeRoot});
    MibProfileRecord largeProfile; largeProfile.id = "large"; largeProfile.name = "Large"; largeProfile.type = MibProfileType::Folder; largeProfile.directory = largeRoot; largeProfile.explicitModules = explicitLarge;
    QElapsedTimer timer; timer.start(); const auto largePlan = MibEffectivePlanResolver().resolve(largeProfile, largeIndex); const qint64 elapsed = timer.elapsed();
    ok &= check(largePlan.explicitModules.size() == 250 && largePlan.effectiveModules.size() == 250 && largePlan.initialLoadOrder.size() == 250 && !largePlan.sha256.isEmpty(), "large profile completeness");
    ok &= check(elapsed < 5000, "large profile performance");
    std::cout << "large-plan explicit=" << largePlan.explicitModules.size() << " effective=" << largePlan.effectiveModules.size() << " elapsed_ms=" << elapsed << '\n';
    return ok ? 0 : 1;
}
