#include "tabletraversal.h"

#include <QStringList>

bool HasVarbindAt(int varbindCount, int index)
{
    return index >= 0 && index < varbindCount;
}

bool IsOidInSubtree(const Oid &oid, const Oid &root)
{
    return oid.len() >= root.len() && oid.nCompare(root.len(), root) == 0;
}

bool BuildFirstColumnRoot(const Oid &tableRow, const Oid &returned,
                          Oid *columnRoot)
{
    if (!columnRoot || returned.len() <= tableRow.len() ||
        !IsOidInSubtree(returned, tableRow))
        return false;

    *columnRoot = tableRow;
    *columnRoot += returned[tableRow.len()];
    return true;
}

bool ExtractOidSuffix(const Oid &root, const Oid &oid, QString *suffix)
{
    if (!suffix || !IsOidInSubtree(oid, root))
        return false;

    QStringList parts;
    for (unsigned long i = root.len(); i < oid.len(); ++i)
        parts.append(QString::number(oid[i]));
    *suffix = parts.join(QLatin1Char('.'));
    return true;
}

bool HasValidColumnInfo(const SmiNode *node)
{
    return node && node->nodekind == SMI_NODEKIND_COLUMN && node->name &&
           node->oidlen > 0 && node->oid;
}

bool RenderSmiNodeOid(const SmiNode *node, Oid *oid)
{
    if (!oid || !node || node->oidlen == 0 || !node->oid)
        return false;
    char *rendered = smiRenderOID(node->oidlen, node->oid, SMI_RENDER_NUMERIC);
    if (!rendered)
        return false;
    *oid = Oid(rendered);
    return oid->valid();
}
