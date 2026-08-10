#include "preferredmibresolver.h"

#include <QCoreApplication>
#include <iostream>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const PreferredMibResolution result = PreferredMibResolver::resolve(
        {" IF-MIB ", "SNMPv2-MIB", "MISSING-MIB", "IF-MIB"},
        {"IF-MIB", "SNMPv2-MIB"}, {"SNMPv2-MIB", "OTHER-MIB"});
    const bool ok = result.toLoad == QStringList{"IF-MIB"} &&
                    result.alreadyLoaded == QStringList{"SNMPv2-MIB"} &&
                    result.unavailable == QStringList{"MISSING-MIB"};
    if (!ok) std::cerr << "preferred MIB resolution failed\n";
    return ok ? 0 : 1;
}
