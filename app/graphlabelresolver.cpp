#include "graphlabelresolver.h"

#include "smi.h"
#include <QList>

QString GraphLabelResolver::displayLabel(const QString &numericOid)
{
    QList<SmiSubid> ids;
    for (const QString &part : numericOid.split('.', Qt::SkipEmptyParts))
    {
        bool ok=false; const uint value=part.toUInt(&ok); if(!ok) return numericOid; ids.append(value);
    }
    SmiNode *node=ids.isEmpty()?nullptr:smiGetNodeByOID(ids.size(),ids.data());
    if(!node||!node->name) return numericOid;
    if (node->oidlen > unsigned(ids.size())) return numericOid;
    for (unsigned int i=0;i<node->oidlen;++i) if(node->oid[i]!=ids[int(i)]) return numericOid;
    for (int i=int(node->oidlen);i<ids.size();++i) if(ids[i]!=0) return numericOid;
    SmiModule *module=smiGetNodeModule(node);
    return module&&module->name
        ? QString::fromLatin1(module->name)+QStringLiteral("::")+QString::fromLatin1(node->name)
        : QString::fromLatin1(node->name);
}
