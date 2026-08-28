#include "mibvaluesemantics.h"

#include <QSet>
#include <cstdint>

namespace {
QString numericOid(const Oid &oid) { return QString::fromLatin1(oid.get_printable()); }
qint64 namedNumber(const MibEnvironmentNamedValue &v)
{ return v.value.isSigned ? v.value.signedValue : qint64(v.value.unsignedValue); }
QString typeName(const MibEnvironmentNodeRecord *n)
{
    if (!n) return {};
    return (QStringList{n->syntaxName, n->textualConventionId} +
            n->textualConventionAncestry).join('|');
}
bool integerConstraint(const MibEnvironmentConstraint &c, qint64 value)
{
    const qint64 lo=c.minimum.isSigned?c.minimum.signedValue:qint64(c.minimum.unsignedValue);
    const qint64 hi=c.maximum.isSigned?c.maximum.signedValue:qint64(c.maximum.unsignedValue);
    return value>=lo && value<=hi;
}
bool unsignedConstraint(const MibEnvironmentConstraint &c, quint64 value)
{
    if (c.minimum.isSigned && c.minimum.signedValue < 0) return false;
    const quint64 lo=c.minimum.isSigned?quint64(c.minimum.signedValue):c.minimum.unsignedValue;
    const quint64 hi=c.maximum.isSigned?quint64(c.maximum.signedValue):c.maximum.unsignedValue;
    return value>=lo && value<=hi;
}
}

MibResolvedObject ResolveMibObject(const MibEnvironmentPtr &environment,
                                   const Oid &oid)
{
    MibResolvedObject r; r.environment=environment;
    if (environment) r.node=environment->longestPrefixNode(numericOid(oid), &r.instanceSuffix);
    return r;
}

QString RenderMibOid(const MibEnvironmentPtr &environment, const Oid &oid)
{
    const auto r=ResolveMibObject(environment, oid);
    if (!r.node || r.node->name.isEmpty()) return numericOid(oid);
    return r.node->name+(r.instanceSuffix.isEmpty()?QString():QStringLiteral(".")+r.instanceSuffix.join('.'));
}

QString RenderMibValue(const MibResolvedObject &object, const Vb &source)
{
    Vb vb=source;
    const auto *node=object.node;
    if (!node) return QString::fromLatin1(vb.get_printable_value());
    if (node->baseType==MibEnvironmentBaseType::Enumeration) {
        long value=0; if (vb.get_value(value)==SNMP_CLASS_SUCCESS) {
            for (const auto &n:node->namedValues) if (namedNumber(n)==value)
                return QStringLiteral("%1(%2)").arg(n.name).arg(value);
            return QString::number(value);
        }
    }
    if (node->baseType==MibEnvironmentBaseType::ObjectIdentifier) {
        Oid value; if (vb.get_value(value)==SNMP_CLASS_SUCCESS)
            return RenderMibOid(object.environment, value);
    }
    if (node->baseType==MibEnvironmentBaseType::Bits) {
        unsigned char bytes[65536]; unsigned long length=0;
        if (vb.get_value(bytes,length,sizeof bytes)==SNMP_CLASS_SUCCESS) {
            QStringList values; QSet<int> modeled;
            for (const auto &n:node->namedValues) {
                const int bit=int(n.value.unsignedValue); modeled.insert(bit);
                if (bit>=0 && bit/8<int(length) && (bytes[bit/8]&(0x80>>(bit%8)))) values<<n.name;
            }
            for (int bit=0;bit<int(length)*8;++bit)
                if (!modeled.contains(bit) && (bytes[bit/8]&(0x80>>(bit%8)))) values<<QStringLiteral("bit%1").arg(bit);
            return values.join(' ');
        }
    }
    if (node->baseType==MibEnvironmentBaseType::OctetString &&
        node->displayHint==QStringLiteral("1x:")) {
        unsigned char bytes[65536]; unsigned long length=0;
        if (vb.get_value(bytes,length,sizeof bytes)==SNMP_CLASS_SUCCESS) {
            QStringList values;
            for (unsigned long i=0;i<length;++i)
                values<<QStringLiteral("%1").arg(bytes[i],2,16,QChar('0'));
            return values.join(':');
        }
    }
    if (node->baseType==MibEnvironmentBaseType::Unsigned64) {
        Counter64 value; if (vb.get_value(value)==SNMP_CLASS_SUCCESS)
            return QString::number(Counter64::c64_to_ll(value));
    }
    return QString::fromLatin1(vb.get_printable_value());
}

int SnmpSyntaxForMibNode(const MibEnvironmentNodeRecord *node)
{
    if (!node) return sNMP_SYNTAX_OCTETS;
    const QString name=typeName(node);
    switch(node->baseType) {
    case MibEnvironmentBaseType::Integer32:
    case MibEnvironmentBaseType::Enumeration:return sNMP_SYNTAX_INT32;
    case MibEnvironmentBaseType::Unsigned32:
        if (name.contains("TimeTicks",Qt::CaseInsensitive)) return sNMP_SYNTAX_TIMETICKS;
        if (name.contains("Counter32",Qt::CaseInsensitive)||name.contains("COUNTER")) return sNMP_SYNTAX_CNTR32;
        if (name.contains("Gauge32",Qt::CaseInsensitive)||name.contains("GAUGE")) return sNMP_SYNTAX_GAUGE32;
        return sNMP_SYNTAX_UINT32;
    case MibEnvironmentBaseType::OctetString:
        if (name.contains("IpAddress",Qt::CaseInsensitive)) return sNMP_SYNTAX_IPADDR;
        if (name.contains("Opaque",Qt::CaseInsensitive)) return sNMP_SYNTAX_OPAQUE;
        return sNMP_SYNTAX_OCTETS;
    case MibEnvironmentBaseType::Bits:return sNMP_SYNTAX_BITS;
    case MibEnvironmentBaseType::ObjectIdentifier:return sNMP_SYNTAX_OID;
    case MibEnvironmentBaseType::Unsigned64:return sNMP_SYNTAX_CNTR64;
    default:return sNMP_SYNTAX_OCTETS;
    }
}

bool IsWritableMibNode(const MibEnvironmentNodeRecord *node)
{ return node && node->access==MibEnvironmentAccess::ReadWrite; }

bool ValidateMibSetValue(const MibEnvironmentNodeRecord *node, int syntax,
                         const QString &value, QString *error)
{
    auto fail=[&](const QString &s){if(error)*error=s;return false;};
    if (!IsWritableMibNode(node)) return fail(QStringLiteral("Object is not writable"));
    bool ok=false; qint64 signedValue=0; quint64 unsignedValue=0;
    if (syntax==sNMP_SYNTAX_INT32) {
        signedValue=value.toLongLong(&ok); if(!ok||signedValue<INT32_MIN||signedValue>INT32_MAX)return fail(QStringLiteral("Invalid integer value"));
        if(node->baseType==MibEnvironmentBaseType::Enumeration&&!node->namedValues.isEmpty()){
            bool found=false;for(const auto&n:node->namedValues)found|=namedNumber(n)==signedValue;
            if(!found)return fail(QStringLiteral("Invalid enumeration value"));}
    } else if (syntax==sNMP_SYNTAX_CNTR32||syntax==sNMP_SYNTAX_GAUGE32||syntax==sNMP_SYNTAX_UINT32||syntax==sNMP_SYNTAX_TIMETICKS) {
        unsignedValue=value.toULongLong(&ok);if(!ok||unsignedValue>UINT32_MAX)return fail(QStringLiteral("Invalid unsigned value"));
    } else if (syntax==sNMP_SYNTAX_CNTR64) { unsignedValue=value.toULongLong(&ok);if(!ok)return fail(QStringLiteral("Invalid Counter64 value")); }
    else if (syntax==sNMP_SYNTAX_OID) { Oid oid(value.toLatin1().constData());if(!value.isEmpty()&&!oid.valid())return fail(QStringLiteral("Invalid Object Identifier value")); }
    else if (syntax==sNMP_SYNTAX_IPADDR) { IpAddress ip(value.toLatin1().constData());if(!value.isEmpty()&&!ip.valid())return fail(QStringLiteral("Invalid IP address value")); }
    for(const auto&c:node->constraints){
        if(c.isSizeConstraint){const quint64 size=quint64(value.toLatin1().size());if(!unsignedConstraint(c,size))return fail(QStringLiteral("Value violates SIZE constraint"));}
        else if(syntax==sNMP_SYNTAX_INT32){if(!integerConstraint(c,signedValue))return fail(QStringLiteral("Value violates range constraint"));}
        else if(ok&&!unsignedConstraint(c,unsignedValue))return fail(QStringLiteral("Value violates range constraint"));
    }
    return true;
}
