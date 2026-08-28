#ifndef MIBVALUESEMANTICS_H
#define MIBVALUESEMANTICS_H

#include "mibenvironment.h"
#include "snmp_pp/snmp_pp.h"

struct MibResolvedObject
{
    MibEnvironmentPtr environment;
    const MibEnvironmentNodeRecord *node = nullptr;
    QStringList instanceSuffix;
};

MibResolvedObject ResolveMibObject(const MibEnvironmentPtr &environment,
                                   const Oid &oid);
QString RenderMibOid(const MibEnvironmentPtr &environment, const Oid &oid);
QString RenderMibValue(const MibResolvedObject &object, const Vb &varbind);
int SnmpSyntaxForMibNode(const MibEnvironmentNodeRecord *node);
bool IsWritableMibNode(const MibEnvironmentNodeRecord *node);
bool ValidateMibSetValue(const MibEnvironmentNodeRecord *node, int syntax,
                         const QString &value, QString *error = nullptr);

#endif
