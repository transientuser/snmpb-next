#include "graphlabelresolver.h"

#include "mibenvironmentregistry.h"

QString GraphLabelResolver::displayLabel(const QString &numericOid)
{
    const auto environment=MibEnvironmentRegistry::active(); if(!environment)return numericOid;
    QStringList suffix; const auto *node=environment->longestPrefixNode(numericOid,&suffix);
    if(!node||node->name.isEmpty())return numericOid;
    for(const QString &part:suffix)if(part!=QStringLiteral("0"))return numericOid;
    return node->qualifiedName.isEmpty()?node->name:node->qualifiedName;
}
