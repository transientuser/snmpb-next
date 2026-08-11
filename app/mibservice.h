#ifndef MIBSERVICE_H
#define MIBSERVICE_H

#include "mibrecords.h"
#include "smi.h"
#include <QSet>

class MibService
{
public:
    explicit MibService(SmiErrorHandler *restoreHandler = nullptr,
                        int restoreErrorLevel = 0);
    void setSearchPaths(const QStringList &paths);
    QStringList searchPaths() const;
    MibLoadResult loadModules(const QStringList &modules, int errorLevel = 9);
    MibLoadResult loadPreloads(const QStringList &preloads, int errorLevel = 9);
    QList<MibModuleRecord> moduleInventory() const;
    MibTreeNodeRecord treeSnapshot(const QStringList &includedModules) const;
    static MibModuleRecord snapshotModule(SmiModule *module);
    static MibTreeNodeRecord snapshotNode(SmiNode *node);

private:
    static QString languageName(SmiLanguage language);
    static QString oidText(SmiNode *node);
    static void appendTree(SmiNode *node, const QSet<QString> &included,
                           MibTreeNodeRecord *parent);
    SmiErrorHandler *restoreErrorHandler;
    int restoredErrorLevel;
    quint64 nextOperationId = 1;
};

#endif
