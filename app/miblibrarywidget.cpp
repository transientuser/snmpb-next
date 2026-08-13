#include "miblibrarywidget.h"

#include "diagnosticlogger.h"
#include "mibdownloadtransport.h"
#include "mibmodule.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
void appendNode(QTreeWidgetItem *parent, const MibDependencyNode &node)
{
    auto *item = new QTreeWidgetItem(parent,
        {node.moduleName, node.cycle ? QStringLiteral("Cycle") : MibLibraryStatusText(node.status)});
    for (const MibDependencyNode &child : node.dependencies) appendNode(item, child);
}
}

MibLibraryWidget::MibLibraryWidget(MibModule *value, const QStringList &paths,
    QWidget *parent, MibDownloadTransport *providedTransport)
    : QWidget(parent), modules(value), bundledPaths(paths),
      transport(providedTransport ? providedTransport : new QtMibDownloadTransport(this))
{
    setObjectName("MibLibraryWorkspace");
    auto *layout = new QVBoxLayout(this);
    auto *filters = new QHBoxLayout;
    search = new QLineEdit(this); search->setPlaceholderText(tr("Search modules"));
    sourceFilter = new QComboBox(this); sourceFilter->addItem(tr("All sources"));
    statusFilter = new QComboBox(this); statusFilter->addItem(tr("All statuses"));
    filters->addWidget(search, 1); filters->addWidget(sourceFilter); filters->addWidget(statusFilter);
    layout->addLayout(filters);
    table = new QTableWidget(this); table->setObjectName("MibLibraryTable");
    table->setColumnCount(5); table->setHorizontalHeaderLabels(
        {tr(""), tr("Module"), tr("Revision"), tr("Source"), tr("Status")});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(table, 2);
    auto *selection = new QHBoxLayout;
    auto *selectAll = new QPushButton(tr("Select All Visible"), this);
    auto *clear = new QPushButton(tr("Clear"), this);
    refreshButton = new QPushButton(tr("Refresh Catalog"), this);
    refreshButton->setObjectName("MibLibraryRefreshCatalog");
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setEnabled(false);
    downloadButton = new QPushButton(tr("Download Selected"), this);
    auto *updateInstalled = new QPushButton(tr("Update Installed"), this);
    auto *resolve = new QPushButton(tr("Resolve Dependencies"), this);
    auto *missingLoad = new QPushButton(tr("Download Missing && Load"), this);
    for (QPushButton *button : {selectAll, clear, refreshButton, cancelButton,
                                downloadButton, updateInstalled, resolve, missingLoad})
        selection->addWidget(button);
    selection->addStretch(); layout->addLayout(selection);
    dependencies = new QTreeWidget(this); dependencies->setObjectName("MibDependencyTree");
    dependencies->setHeaderLabels({tr("Dependency"), tr("Status")});
    layout->addWidget(dependencies, 1);
    status = new QLabel(tr("Ready"), this); status->setWordWrap(true); layout->addWidget(status);
    connect(search, &QLineEdit::textChanged, this, &MibLibraryWidget::applyFilter);
    connect(sourceFilter, &QComboBox::currentTextChanged, this, &MibLibraryWidget::applyFilter);
    connect(statusFilter, &QComboBox::currentTextChanged, this, &MibLibraryWidget::applyFilter);
    connect(selectAll, &QPushButton::clicked, this, &MibLibraryWidget::selectVisible);
    connect(clear, &QPushButton::clicked, this, &MibLibraryWidget::clearSelection);
    connect(refreshButton, &QPushButton::clicked, this, &MibLibraryWidget::refreshCatalog);
    connect(cancelButton, &QPushButton::clicked, transport, &MibDownloadTransport::cancel);
    connect(downloadButton, &QPushButton::clicked, this, [this]() { downloadSelected(false); });
    connect(updateInstalled, &QPushButton::clicked, this, [this]() {
        status->setText(tr("No reliably comparable installed revisions are available"));
    });
    connect(resolve, &QPushButton::clicked, this, &MibLibraryWidget::resolveSelected);
    connect(missingLoad, &QPushButton::clicked, this, [this]() { downloadSelected(true); });
    connect(table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        if (row < 0 || row >= records.size()) return;
        const auto &record = records[row];
        status->setText(tr("Module: %1\nLocal: %2\nSource: %3\nDownloaded: %4\nSHA-256: %5")
            .arg(record.moduleName, record.localPath, record.sourceUrl,
                 record.downloadedAt.isValid() ? record.downloadedAt.toString(Qt::ISODate) : QString(),
                 record.sha256));
    });
    connect(transport, &MibDownloadTransport::finished, this, [this](const MibDownloadResult &result) {
        if (catalogRefreshInProgress) {
            catalogRefreshInProgress = false;
            refreshButton->setEnabled(true); cancelButton->setEnabled(false);
            QString error; MibCatalog refreshed;
            if (!result.error.isEmpty() ||
                !IanaMibSourceProvider::parseIndex(result.content, &refreshed, &error)) {
                sourceStatusText = tr("IANA refresh failed — using cached catalog: %1")
                    .arg(result.error.isEmpty() ? error : result.error);
                refresh(); DiagnosticLogger::log("MIB", sourceStatusText); return;
            }
            MibCatalogCacheInfo info{QDateTime::currentDateTimeUtc(),
                IanaMibSourceProvider::id(), IanaMibSourceProvider::indexUrl().toString()};
            if (!MibCatalogCache::save(catalogCachePath(), refreshed, info, &error)) {
                sourceStatusText = tr("IANA refresh could not be cached — using prior catalog: %1").arg(error);
                refresh(); DiagnosticLogger::log("MIB", sourceStatusText); return;
            }
            catalog = refreshed;
            sourceStatusText = tr("IANA current — refreshed %1 — %n modules", nullptr,
                                  catalog.entries().size())
                .arg(info.refreshedAt.toLocalTime().toString(Qt::ISODate));
            refresh();
            return;
        }
        const QString module = downloadQueue.isEmpty() ? QString() : downloadQueue.takeFirst();
        const MibCatalogEntry *found = catalog.find(module); QString error;
        MibCatalogEntry entry = found ? *found : MibCatalogEntry{};
        const auto scanned = MibImportScanner::scan(result.content);
        entry.imports = scanned.imports;
        if (!scanned.revision.isEmpty()) entry.revision = scanned.revision;
        if (!found || !result.error.isEmpty() ||
            !library.install(entry, result.content, bundledPaths, nullptr, &error,
                [this](const QString &path, QString *validationError) {
                    return modules && modules->ValidateModuleFile(path, validationError);
                })) {
            const QString reason = result.error.isEmpty() ? error : result.error;
            sessionState.fail(module, reason);
            status->setText(tr("%1 failed\nStatus: Failed\nReason: %2")
                            .arg(module, reason));
            DiagnosticLogger::log("MIB", tr("Download failed module=%1 initial=%2 http=%3 final=%4 reason=%5 redirects=%6")
                .arg(module, result.initialUrl.toString(QUrl::FullyEncoded))
                .arg(result.httpStatus)
                .arg(result.finalUrl.toString(QUrl::FullyEncoded), reason,
                     result.redirectTrace.join(QStringLiteral(" | "))));
            const QString visibleFailure = status->text();
            downloadQueue.clear();
            downloadButton->setEnabled(true); refresh();
            status->setText(visibleFailure); return;
        }
        sessionState.succeed(module);
        catalog.upsert(entry);
        DiagnosticLogger::log("MIB", tr("Installed module=%1 source=%2 initial=%3 final=%4 http=%5 redirects=%6")
            .arg(module, entry.sourceName,
                 result.initialUrl.toString(QUrl::FullyEncoded),
                 result.finalUrl.toString(QUrl::FullyEncoded))
            .arg(result.httpStatus)
            .arg(result.redirectTrace.join(QStringLiteral(" | "))));
        refresh();
        QMap<QString, MibLibraryStatus> known;
        for (const auto &record : records) known.insert(record.moduleName, record.status);
        downloadQueue = MibDependencyResolver().resolve(requestedTargets, known, catalog).orderedDownloads;
        downloadNext();
    });
    loadCachedCatalog(); refresh();
}

void MibLibraryWidget::loadCachedCatalog()
{
    QString error; MibCatalog parsed; MibCatalogCacheInfo info;
    if (MibCatalogCache::load(catalogCachePath(), &parsed, &info, &error)) {
        catalog = parsed;
        if (info.refreshedAt.isValid()) sourceStatusText = tr("IANA cached — refreshed %1")
            .arg(info.refreshedAt.toLocalTime().toString(Qt::ISODate));
    }
}

QString MibLibraryWidget::catalogCachePath() const
{
    return QDir(library.rootPath()).filePath("cache/catalog-v1.json");
}

void MibLibraryWidget::refreshCatalog()
{
    if (catalogRefreshInProgress) return;
    catalogRefreshInProgress = true;
    refreshButton->setEnabled(false); cancelButton->setEnabled(true);
    status->setText(tr("Refreshing authoritative IANA catalog…"));
    DiagnosticLogger::log("MIB", status->text());
    transport->get(IanaMibSourceProvider::indexUrl());
}

void MibLibraryWidget::refresh()
{
    records = library.inventory(bundledPaths, catalog); table->setRowCount(records.size());
    QStringList sources{tr("All sources")}, statuses{tr("All statuses")};
    for (int row = 0; row < records.size(); ++row) {
        const auto &record = records[row];
        if (!sessionState.failureReason(record.moduleName).isEmpty()) {
            status->setText(tr("%1\nStatus: Failed\nReason: %2")
                .arg(record.moduleName, sessionState.failureReason(record.moduleName)));
            return;
        }
        auto *check = new QTableWidgetItem; check->setCheckState(Qt::Unchecked);
        table->setItem(row, 0, check);
        table->setItem(row, 1, new QTableWidgetItem(record.moduleName));
        table->setItem(row, 2, new QTableWidgetItem(record.revision));
        table->setItem(row, 3, new QTableWidgetItem(record.sourceName));
        const QString state = MibLibraryStatusText(
            sessionState.status(record.moduleName, record.status));
        table->setItem(row, 4, new QTableWidgetItem(state));
        if (!record.sourceName.isEmpty() && !sources.contains(record.sourceName)) sources.append(record.sourceName);
        if (!statuses.contains(state)) statuses.append(state);
    }
    const QString oldSource = sourceFilter->currentText(), oldStatus = statusFilter->currentText();
    sourceFilter->clear(); sourceFilter->addItems(sources); statusFilter->clear(); statusFilter->addItems(statuses);
    sourceFilter->setCurrentText(oldSource); statusFilter->setCurrentText(oldStatus); applyFilter();
    status->setText(sourceStatusText.isEmpty()
        ? tr("%n modules in library", nullptr, records.size())
        : tr("%n modules in library — %1", nullptr, records.size()).arg(sourceStatusText));
    for (int row = 0; row < records.size(); ++row)
        if (!sessionState.failureReason(records[row].moduleName).isEmpty()) {
            table->item(row, 4)->setToolTip(sessionState.failureReason(records[row].moduleName));
        }
}

void MibLibraryWidget::applyFilter()
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool matchSearch = table->item(row, 1)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 2)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 3)->text().contains(search->text(), Qt::CaseInsensitive);
        const bool matchSource = sourceFilter->currentIndex() <= 0 || table->item(row, 3)->text() == sourceFilter->currentText();
        const bool matchStatus = statusFilter->currentIndex() <= 0 || table->item(row, 4)->text() == statusFilter->currentText();
        table->setRowHidden(row, !(matchSearch && matchSource && matchStatus));
    }
}
void MibLibraryWidget::selectVisible() { for (int row=0; row<table->rowCount(); ++row) if (!table->isRowHidden(row)) table->item(row,0)->setCheckState(Qt::Checked); }
void MibLibraryWidget::clearSelection() { for (int row=0; row<table->rowCount(); ++row) table->item(row,0)->setCheckState(Qt::Unchecked); }
QStringList MibLibraryWidget::selectedModules() const { QStringList result; for(int row=0;row<table->rowCount();++row) if(table->item(row,0)->checkState()==Qt::Checked) result.append(table->item(row,1)->text()); return result; }

void MibLibraryWidget::resolveSelected()
{
    QMap<QString, MibLibraryStatus> known;
    MibCatalog working = catalog; QList<MibCatalogEntry> entries = catalog.entries();
    for (const MibLibraryRecord &record : records) {
        known.insert(record.moduleName, record.status);
        if (!working.find(record.moduleName) && !record.localPath.isEmpty()) {
            QFile file(record.localPath); if (!file.open(QIODevice::ReadOnly)) continue;
            const auto scan = MibImportScanner::scan(file.readAll());
            MibCatalogEntry entry; entry.moduleName = record.moduleName; entry.imports = scan.imports;
            entries.append(entry);
        }
    }
    working.setEntries(entries);
    const MibDependencyPlan plan = MibDependencyResolver().resolve(selectedModules(), known, working);
    dependencies->clear();
    for (const MibDependencyNode &root : plan.roots) appendNode(dependencies->invisibleRootItem(), root);
    dependencies->expandAll();
    status->setText(plan.unresolved.isEmpty() ? tr("Dependency plan resolved") :
        tr("Unresolved: %1").arg(plan.unresolved.join(", ")));
    DiagnosticLogger::log("MIB", status->text());
}

void MibLibraryWidget::downloadSelected(bool loadAfter)
{
    requestedTargets = selectedModules();
    for (const QString &module : requestedTargets) sessionState.begin(module);
    QMap<QString, MibLibraryStatus> known;
    for (const auto &record : records) known.insert(record.moduleName, record.status);
    const MibDependencyPlan plan = MibDependencyResolver().resolve(requestedTargets, known, catalog);
    downloadQueue = plan.orderedDownloads; loadAfterDownload = loadAfter;
    if (downloadQueue.isEmpty()) { resolveSelected(); return; }
    downloadButton->setEnabled(false); downloadNext();
}

void MibLibraryWidget::downloadNext()
{
    if (downloadQueue.isEmpty()) {
        downloadButton->setEnabled(true); refresh();
        if (modules) { modules->RescanPath(); if (loadAfterDownload) modules->LoadPreferredModules(requestedTargets); }
        status->setText(tr("Download and validation complete")); return;
    }
    const MibCatalogEntry *entry = catalog.find(downloadQueue.first());
    if (!entry) { status->setText(tr("No catalog entry for %1").arg(downloadQueue.first())); downloadQueue.clear(); downloadButton->setEnabled(true); return; }
    status->setText(tr("Downloading %1 from %2").arg(entry->moduleName, entry->sourceName));
    DiagnosticLogger::log("MIB", tr("Downloading module=%1 initial=%2 source=%3")
        .arg(entry->moduleName, QUrl(entry->url).toString(QUrl::FullyEncoded), entry->sourceName));
    transport->get(QUrl(entry->url));
}
