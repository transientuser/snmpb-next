#include "trappresenter.h"

#include "smi.h"
#include "snmp_pp/smi.h"

#include <QCoreApplication>

namespace {
SmiNode *nodeFor(const QString &oid)
{
    const QByteArray bytes = oid.toLatin1();
    SmiNode *node = smiGetNode(nullptr, bytes.constData());
    if (node)
        return node;

    QList<SmiSubid> ids;
    for (const QString &part : oid.split('.', Qt::SkipEmptyParts)) {
        bool ok = false;
        const uint value = part.toUInt(&ok);
        if (!ok)
            return nullptr;
        ids.append(value);
    }
    return ids.isEmpty() ? nullptr : smiGetNodeByOID(ids.size(), ids.data());
}

QString versionText(TrapSnmpVersion version)
{
    switch (version) {
    case TrapSnmpVersion::V1: return QStringLiteral("SNMPv1");
    case TrapSnmpVersion::V2c: return QStringLiteral("SNMPv2c");
    case TrapSnmpVersion::V3: return QStringLiteral("SNMPv3");
    default: return QStringLiteral("Unknown");
    }
}

SmiType *effectiveType(SmiNode *node)
{
    SmiType *type = node ? smiGetNodeType(node) : nullptr;
    if (type && !type->name && type->basetype != SMI_BASETYPE_ENUM &&
        type->basetype != SMI_BASETYPE_BITS)
        type = smiGetParentType(type);
    return type;
}

QByteArray octetsFromPrintable(const QString &printable)
{
    QByteArray bytes;
    for (const QString &line : printable.split('\n')) {
        const int textColumn = line.indexOf(QStringLiteral("   "), 2);
        const QString hex = textColumn >= 0 ? line.left(textColumn) : line;
        for (const QString &token : hex.simplified().split(' ', Qt::SkipEmptyParts)) {
            bool ok = false;
            const uint byte = token.size() == 2 ? token.toUInt(&ok, 16) : 0;
            if (!ok)
                break;
            bytes.append(char(byte));
        }
    }
    return bytes.isEmpty() ? printable.toLatin1() : bytes;
}

QString rendered(SmiValue &value, SmiType *type, int flags)
{
    const char *text = smiRenderValue(&value, type, flags);
    return text ? QString::fromLocal8Bit(text) : QString();
}
}

QString TrapPresenter::symbolicOid(const QString &numericOid)
{
    SmiNode *node = nodeFor(numericOid);
    if (!node || !node->name)
        return numericOid;
    const QStringList parts = numericOid.split('.', Qt::SkipEmptyParts);
    QString result = QString::fromLocal8Bit(node->name);
    for (int i = int(node->oidlen); i < parts.size(); ++i)
        result += QLatin1Char('.') + parts.at(i);
    return result;
}

QString TrapPresenter::formattedValue(const TrapVarbind &varbind)
{
    SmiType *type = effectiveType(nodeFor(varbind.oid));
    if (!type)
        return varbind.printableValue;

    SmiValue value{};
    value.basetype = type->basetype;
    bool ok = false;
    switch (type->basetype) {
    case SMI_BASETYPE_INTEGER32:
    case SMI_BASETYPE_ENUM:
        value.value.integer32 = varbind.printableValue.toInt(&ok);
        return ok ? rendered(value, type, SMI_RENDER_ALL) : varbind.printableValue;
    case SMI_BASETYPE_UNSIGNED32:
        if (varbind.syntax == sNMP_SYNTAX_TIMETICKS)
            return varbind.printableValue;
        value.value.unsigned32 = varbind.printableValue.toUInt(&ok);
        return ok ? rendered(value, type, SMI_RENDER_ALL) : varbind.printableValue;
    case SMI_BASETYPE_UNSIGNED64:
        value.value.unsigned64 = varbind.printableValue.toULongLong(&ok);
        if (ok) {
            const QString result = rendered(value, type, SMI_RENDER_ALL);
            return result.contains(QStringLiteral("%I64")) ? varbind.printableValue : result;
        }
        return varbind.printableValue;
    case SMI_BASETYPE_OBJECTIDENTIFIER: {
        QList<SmiSubid> ids;
        for (const QString &part : varbind.printableValue.split('.', Qt::SkipEmptyParts)) {
            const uint id = part.toUInt(&ok);
            if (!ok)
                return varbind.printableValue;
            ids.append(id);
        }
        if (ids.isEmpty())
            return varbind.printableValue;
        value.value.oid = ids.data();
        value.len = ids.size();
        return rendered(value, type, SMI_RENDER_NAME);
    }
    case SMI_BASETYPE_OCTETSTRING:
    case SMI_BASETYPE_BITS: {
        if (varbind.syntax == sNMP_SYNTAX_OPAQUE ||
            varbind.syntax == sNMP_SYNTAX_IPADDR)
            return varbind.printableValue;
        QByteArray bytes = octetsFromPrintable(varbind.printableValue);
        if (type->format && QByteArray(type->format) == "1x:") {
            QStringList octets;
            for (unsigned char byte : bytes)
                octets << QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0'));
            return octets.join(QLatin1Char(':'));
        }
        value.value.ptr = reinterpret_cast<unsigned char *>(bytes.data());
        value.len = bytes.size();
        return rendered(value, type, SMI_RENDER_ALL);
    }
    default:
        return varbind.printableValue;
    }
}

TrapPresentation TrapPresenter::present(const TrapRecord &record, bool) const
{
    TrapPresentation output;
    const QString notification = symbolicOid(record.notificationOid);
    output.summaryColumns
        << QStringLiteral("%1").arg(record.recordId, 4, 10, QLatin1Char('0'))
        << record.receivedTimestamp.date().toString(Qt::ISODate)
        << record.receivedTimestamp.time().toString(Qt::ISODate)
        << QString::number(record.notificationTicks)
        << notification << record.messageType << versionText(record.snmpVersion)
        << record.sourceAddress << QString::number(record.sourcePort);
    output.communityText = QCoreApplication::translate("TrapItem", "Community: %1")
                               .arg(record.community);

    for (int i = 0; i < record.varbinds.size(); ++i) {
        const TrapVarbind &vb = record.varbinds.at(i);
        output.varbindLines << QStringLiteral("#%1 %2: %3")
                                   .arg(i).arg(symbolicOid(vb.oid), formattedValue(vb));
    }

    SmiNode *node = nodeFor(record.notificationOid);
    if (!node || node->nodekind != SMI_NODEKIND_NOTIFICATION)
        return output;
    SmiModule *module = smiGetNodeModule(node);
    const char *rendered = smiRenderOID(node->oidlen, node->oid, SMI_RENDER_NUMERIC);
    output.notificationHtml = QStringLiteral(
        "<table border=\"1\" cellpadding=\"0\" cellspacing=\"0\" align=\"left\">"
        "<tr><td><b>Name:</b></td><td><font color=#009000><b>%1</b></font></td>"
        "<tr><td><b>Oid:</b></td><td>%2</td></tr>"
        "<tr><td><b>Units:</b></td><td>%3</td></tr>"
        "<tr><td><b>Module:</b></td><td>%4</td></tr>"
        "<tr><td><b>Reference:</b></td><td><font face=fixed color=blue>%5</font></td></tr>"
        "<tr><td><b>Description:</b></td><td><font face=fixed color=blue>%6</font></td></tr>"
        "</table>")
        .arg(QString::fromLocal8Bit(node->name ? node->name : ""),
             QString::fromLocal8Bit(rendered ? rendered : ""),
             QString::fromLocal8Bit(node->units ? node->units : ""),
             QString::fromLocal8Bit(module && module->name ? module->name : ""),
             QString::fromLocal8Bit(node->reference ? node->reference : "").toHtmlEscaped(),
             QString::fromLocal8Bit(node->description ? node->description : "").toHtmlEscaped());
    return output;
}
