#ifndef MIBPARSERNODESAFETY_H
#define MIBPARSERNODESAFETY_H

#include "smi.h"

#include <QList>

inline bool MibParserNodeHasReadableOid(const SmiNode *node)
{
    return node && (node->oidlen == 0 || node->oid != nullptr);
}

inline QList<quint32> MibParserNodeOidParts(const SmiNode *node)
{
    QList<quint32> result;
    if (!MibParserNodeHasReadableOid(node)) return result;
    result.reserve(node->oidlen);
    for (unsigned int i = 0; i < node->oidlen; ++i) result.append(node->oid[i]);
    return result;
}

#endif
