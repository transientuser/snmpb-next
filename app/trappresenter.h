#ifndef TRAPPRESENTER_H
#define TRAPPRESENTER_H

#include "traprecord.h"

#include <QStringList>

struct TrapPresentation
{
    QStringList summaryColumns;
    QString notificationHtml;
    QString communityText;
    QStringList varbindLines;
};

class TrapPresenter
{
public:
    TrapPresentation present(const TrapRecord &record, bool showAgentName = false) const;
    static QString symbolicOid(const QString &numericOid);
    static QString formattedValue(const TrapVarbind &varbind);
};

#endif
