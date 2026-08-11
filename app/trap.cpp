#include "trap.h"

#include "preferences.h"
#include "snmpb.h"
#include "trappresenter.h"

#include <QTreeWidget>
#include <QBoxLayout>
#include <QPushButton>

Trap::Trap(Snmpb *snmpb)
    : s(snmpb), trapService(snmpb->PreferencesObj()->GetTrapHistoryLimit(), this)
{
    s->MainUI()->TrapContent->header()->hide();
    s->MainUI()->TrapContent->setSortingEnabled(false);
    connect(s->MainUI()->TrapLog, &QTreeWidget::currentItemChanged,
            this, &Trap::SelectedTrap);
    connect(this, SIGNAL(TrapProperties(const QString&)),
            s->MainUI()->TrapInfo, SLOT(setHtml(const QString&)));
    connect(&trapService, &TrapService::recordAdded, this, &Trap::RefreshHistory);
    connect(&trapService, &TrapService::historyReset, this, &Trap::RefreshHistory);
    auto *clearButton = new QPushButton(tr("Clear History"), s->MainUI()->TrapLog->parentWidget());
    clearButton->setToolTip(tr("Remove all retained traps from the in-memory history"));
    clearButton->setEnabled(false);
    if (auto *layout = qobject_cast<QBoxLayout *>(s->MainUI()->TrapLog->parentWidget()->layout()))
        layout->insertWidget(1, clearButton, 0, Qt::AlignLeft);
    connect(clearButton, &QPushButton::clicked, &trapService, &TrapService::clear);
    connect(&trapService, &TrapService::recordAdded, clearButton, [clearButton] { clearButton->setEnabled(true); });
    connect(&trapService, &TrapService::historyReset, clearButton, [clearButton] { clearButton->setEnabled(false); });
}

Trap::~Trap() { trapService.stop(); }

TrapService *Trap::service() { return &trapService; }

bool Trap::Receive(const Pdu &pdu, const TrapEndpoint &endpoint,
                   const QDateTime &received)
{
    return trapService.receive(pdu, endpoint, received);
}

void Trap::RefreshHistory()
{
    QTreeWidget *log = s->MainUI()->TrapLog;
    log->clear();
    TrapPresenter presenter;
    const auto &records = trapService.history().records();
    for (int i = 0; i < records.size(); ++i) {
        auto *item = new QTreeWidgetItem(log, presenter.present(records.at(i)).summaryColumns);
        item->setData(0, Qt::UserRole, i);
    }
}

void Trap::SelectedTrap(QTreeWidgetItem *item, QTreeWidgetItem *)
{
    s->MainUI()->TrapContent->clear();
    if (!item) {
        emit TrapProperties(QString());
        return;
    }
    const TrapRecord *record = trapService.history().recordAt(item->data(0, Qt::UserRole).toInt());
    if (!record)
        return;
    const TrapPresentation view = TrapPresenter().present(*record);
    new QTreeWidgetItem(s->MainUI()->TrapContent, QStringList(view.communityText));
    auto *bindings = new QTreeWidgetItem(s->MainUI()->TrapContent,
        QStringList(tr("Bindings (%1)").arg(view.varbindLines.size())));
    bindings->setExpanded(s->PreferencesObj()->GetExpandTrapBinding());
    for (const QString &line : view.varbindLines)
        new QTreeWidgetItem(bindings, QStringList(line));
    emit TrapProperties(view.notificationHtml);
}
