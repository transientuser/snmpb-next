#include "miblibrarywidget.h"

#include "diagnosticlogger.h"
#include "mibdownloadtransport.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
class CheckableHeaderView : public QHeaderView
{
public:
    explicit CheckableHeaderView(QWidget *parent) : QHeaderView(Qt::Horizontal, parent)
    {
        indicator = new QCheckBox(viewport());
        indicator->setObjectName(QStringLiteral("MibLibraryHeaderSelectAll"));
        indicator->setTristate(true);
        indicator->setFocusPolicy(Qt::NoFocus);
        connect(indicator, &QCheckBox::clicked, this, [this]() {
            if (clicked) clicked();
        });
        connect(this, &QHeaderView::sectionResized, this,
                [this](int, int, int) { positionIndicator(); });
        connect(this, &QHeaderView::geometriesChanged, this,
                [this]() { positionIndicator(); });
    }
    Qt::CheckState checkState() const { return state; }
    void setCheckState(Qt::CheckState value)
    {
        setProperty("checkState", static_cast<int>(value));
        if (state != value) {
            state = value;
            const QSignalBlocker blocker(indicator);
            indicator->setCheckState(value);
        }
    }
    std::function<void()> clicked;
protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QHeaderView::resizeEvent(event);
        positionIndicator();
    }
private:
    void positionIndicator()
    {
        const QSize size = indicator->sizeHint();
        indicator->setGeometry(sectionViewportPosition(0) + 4,
            (viewport()->height() - size.height()) / 2, size.width(), size.height());
        indicator->raise();
    }
    QCheckBox *indicator = nullptr;
    Qt::CheckState state = Qt::Unchecked;
};

void appendNode(QTreeWidgetItem *parent, const MibDependencyNode &node)
{
    auto *item = new QTreeWidgetItem(parent,
        {node.moduleName, node.cycle ? QStringLiteral("Cycle") : MibLibraryStatusText(node.status)});
    for (const MibDependencyNode &child : node.dependencies) appendNode(item, child);
}

QString normalizedLineEndings(QString text)
{
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return text;
}

QString reflowMibDisplayText(const QString &source)
{
    QStringList paragraphs;
    QStringList paragraphLines;
    const auto finishParagraph = [&]() {
        if (!paragraphLines.isEmpty()) {
            paragraphs.append(paragraphLines.join(QLatin1Char(' ')));
            paragraphLines.clear();
        }
    };
    for (const QString &line : normalizedLineEndings(source).split(
             QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) finishParagraph();
        else paragraphLines.append(trimmed);
    }
    finishParagraph();
    return paragraphs.join(QStringLiteral("\n\n")).trimmed();
}

QString normalizeContactDisplayText(const QString &source)
{
    QStringList lines;
    bool pendingBlankLine = false;
    for (QString line : normalizedLineEndings(source).split(
             QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        while (line.endsWith(QLatin1Char(' ')) || line.endsWith(QLatin1Char('\t')))
            line.chop(1);
        if (line.trimmed().isEmpty()) {
            pendingBlankLine = !lines.isEmpty();
            continue;
        }
        if (pendingBlankLine) lines.append(QString());
        lines.append(line);
        pendingBlankLine = false;
    }
    return lines.join(QLatin1Char('\n'));
}
}

MibLibraryWidget::MibLibraryWidget(const QStringList &paths,
    QWidget *parent, MibDownloadTransport *providedTransport, Callbacks providedCallbacks)
    : QWidget(parent), callbacks(std::move(providedCallbacks)), bundledPaths(paths),
      library(),
      transport(providedTransport ? providedTransport : new QtMibDownloadTransport(this))
      , profiles(MibProfileRepository(QDir(library.rootPath()).filePath("profiles-v1.json")))
{
    setObjectName("MibLibraryWorkspace");
    auto *outer = new QVBoxLayout(this);
    auto *workspaceTabs = new QTabWidget(this);
    workspaceTabs->setObjectName("MibLibraryTabs");
    outer->addWidget(workspaceTabs);
    auto *inventoryPage = new QWidget(workspaceTabs);
    auto *layout = new QVBoxLayout(inventoryPage);
    workspaceTabs->addTab(inventoryPage, tr("Inventory"));
    auto *filters = new QHBoxLayout;
    search = new QLineEdit(this); search->setObjectName("MibLibrarySearch");
    search->setPlaceholderText(tr("Search modules"));
    sourceFilter = new QComboBox(this); sourceFilter->addItem(tr("All sources"));
    statusFilter = new QComboBox(this); statusFilter->hide();
    refreshButton = new QPushButton(tr("Refresh Catalog"), this);
    refreshButton->setObjectName("MibLibraryRefreshCatalog");
    auto *checkUpdates = new QPushButton(tr("Check for Updates"), this);
    checkUpdates->setObjectName("MibLibraryCheckForUpdates");
    cancelButton = new QPushButton(tr("Cancel"), this);
    cancelButton->setObjectName("MibLibraryCancel"); cancelButton->hide();
    filters->addWidget(search, 1); filters->addWidget(sourceFilter);
    filters->addWidget(refreshButton); filters->addWidget(checkUpdates); filters->addWidget(cancelButton);
    layout->addLayout(filters);
    table = new QTableWidget(this); table->setObjectName("MibLibraryTable");
    table->setColumnCount(4); table->setHorizontalHeaderLabels(
        {QString(), tr("Module"), tr("Revision"), tr("Origin")});
    auto *checkHeader = new CheckableHeaderView(table);
    checkHeader->setObjectName("MibLibraryCheckableHeader");
    checkHeader->setProperty("indicatorAlignment", QStringLiteral("left"));
    checkHeader->setToolTip(tr("Select all filtered modules"));
    table->setHorizontalHeader(checkHeader);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *selection = new QHBoxLayout;
    selectedCount = new QLabel(tr("0 selected"), this);
    selectedCount->setObjectName("MibLibrarySelectedCount");
    downloadButton = new QPushButton(tr("Download"), this);
    downloadButton->setObjectName("MibLibraryDownload");
    installDependenciesButton = new QPushButton(tr("Install with Dependencies"), this);
    installDependenciesButton->setObjectName("MibLibraryInstallWithDependencies");
    selection->addWidget(selectedCount); selection->addStretch();
    selection->addWidget(downloadButton); selection->addWidget(installDependenciesButton);
    auto *tablePane = new QWidget(inventoryPage);
    tablePane->setObjectName("MibLibraryInventoryTablePane");
    auto *tableLayout = new QVBoxLayout(tablePane);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    tableLayout->addWidget(table, 1);
    tableLayout->addLayout(selection);
    detailsTabs = new QTabWidget(this);
    detailsTabs->setObjectName("MibLibraryDetailsTabs");
    auto *infoPage = new QWidget(detailsTabs);
    auto *infoLayout = new QVBoxLayout(infoPage);
    const auto addFields = [this, infoPage](QFormLayout *form,
                                            const QList<QPair<QString, QString>> &fields) {
        for (const auto &field : fields) {
            auto *valueLabel = new QLabel(infoPage);
            valueLabel->setTextFormat(Qt::PlainText);
            valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
            valueLabel->setObjectName(QStringLiteral("MibLibraryInfo_%1").arg(field.first));
            valueLabel->setWordWrap(true); infoValues.insert(field.first, valueLabel);
            form->addRow(field.second + QLatin1Char(':'), valueLabel);
            infoLabels.insert(field.first, form->labelForField(valueLabel));
        }
    };
    auto *moduleGroup = new QGroupBox(tr("Module Information"), infoPage);
    moduleGroup->setObjectName("MibLibraryModuleInformation");
    auto *moduleForm = new QFormLayout(moduleGroup);
    addFields(moduleForm, {{"module", tr("Module")}, {"moduleOid", tr("Module OID")},
        {"lastUpdated", tr("Last Updated")}, {"organization", tr("Organization")},
        {"description", tr("Description")}, {"contact", tr("Contact Information")},
        {"reference", tr("Reference")}, {"revisionHistory", tr("Revision History")}});
    QLabel *oldContact = infoValues.take("contact");
    QWidget *contactLabel = infoLabels.value("contact");
    moduleForm->removeWidget(oldContact); oldContact->deleteLater();
    contactInfo = new QTextEdit(infoPage);
    contactInfo->setObjectName("MibLibraryInfo_contact");
    contactInfo->setReadOnly(true);
    contactInfo->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    contactInfo->setLineWrapMode(QTextEdit::WidgetWidth);
    contactInfo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    contactInfo->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    contactInfo->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    const int contactHeight = contactInfo->fontMetrics().lineSpacing() * 10 +
        contactInfo->frameWidth() * 2 + 8;
    contactInfo->setMaximumHeight(contactHeight);
    contactInfo->setMinimumHeight(contactInfo->fontMetrics().lineSpacing() * 3);
    moduleForm->addRow(contactLabel, contactInfo);
    auto *fileGroup = new QGroupBox(tr("File Information"), infoPage);
    fileGroup->setObjectName("MibLibraryFileInformation");
    auto *fileForm = new QFormLayout(fileGroup);
    addFields(fileForm, {{"origin", tr("Origin")}, {"revision", tr("Catalog revision")},
        {"filename", tr("Filename")}, {"path", tr("Local path")},
        {"provider", tr("Source / provider")}, {"url", tr("Source URL")},
        {"timestamp", tr("Installed / downloaded")}, {"sha", tr("SHA-256")},
        {"state", tr("State")}});
    moduleGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    fileGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto *moduleScroll = new QScrollArea(infoPage);
    moduleScroll->setObjectName("MibLibraryModuleInformationScroll");
    moduleScroll->setWidgetResizable(true);
    moduleScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    moduleScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    moduleScroll->setWidget(moduleGroup);
    auto *fileScroll = new QScrollArea(infoPage);
    fileScroll->setObjectName("MibLibraryFileInformationScroll");
    fileScroll->setWidgetResizable(true);
    fileScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    fileScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    fileScroll->setWidget(fileGroup);
    auto *informationSplitter = new QSplitter(Qt::Horizontal, infoPage);
    informationSplitter->setObjectName("MibLibraryInformationSplitter");
    informationSplitter->setChildrenCollapsible(false);
    informationSplitter->addWidget(moduleScroll);
    informationSplitter->addWidget(fileScroll);
    informationSplitter->setStretchFactor(0, 13);
    informationSplitter->setStretchFactor(1, 7);
    QTimer::singleShot(0, informationSplitter, [informationSplitter]() {
        const int width = informationSplitter->width();
        if (width > 0) informationSplitter->setSizes({width * 13, width * 7});
    });
    infoLayout->addWidget(informationSplitter, 1);
    openEditorButton = new QPushButton(tr("Open in Editor"), infoPage);
    openEditorButton->setObjectName("MibLibraryOpenInEditor");
    infoLayout->addWidget(openEditorButton, 0, Qt::AlignLeft); infoLayout->addStretch();
    dependencies = new QTreeWidget(this); dependencies->setObjectName("MibDependencyTree");
    dependencies->setHeaderLabels({tr("Dependency"), tr("Status")});
    auto *dependencyHeader = dependencies->header();
    dependencyHeader->setObjectName("MibDependencyHeader");
    dependencyHeader->setStretchLastSection(false);
    dependencyHeader->setMinimumSectionSize(120);
    dependencyHeader->setSectionResizeMode(0, QHeaderView::Stretch);
    dependencyHeader->setSectionResizeMode(1, QHeaderView::Interactive);
    const QString dependencyHeaderKey = QStringLiteral("mib-library/dependencies-status-width");
    const QVariant savedDependencyWidth = QSettings().value(dependencyHeaderKey);
    const int restoredDependencyWidth = savedDependencyWidth.toInt();
    connect(dependencyHeader, &QHeaderView::sectionResized, this,
            [dependencyHeaderKey](int logicalIndex, int, int newSize) {
        if (logicalIndex == 1) QSettings().setValue(dependencyHeaderKey, newSize);
    });
    detailsTabs->addTab(infoPage, tr("Info")); detailsTabs->addTab(dependencies, tr("Dependencies"));
    const auto applyDependencyWidth = [this, dependencyHeader, restoredDependencyWidth]() {
        if (dependencyHeader->property("statusWidthApplied").toBool()) return;
        const int width = dependencies->viewport()->width();
        if (width > 0) {
            dependencyHeader->setProperty("statusWidthApplied", true);
            dependencyHeader->resizeSection(
                1, restoredDependencyWidth >= 120 ? restoredDependencyWidth : qMax(120, width / 4));
        }
    };
    connect(detailsTabs, &QTabWidget::currentChanged, this,
            [applyDependencyWidth](int index) {
        if (index == 1) QTimer::singleShot(0, applyDependencyWidth);
    });
    auto *inventorySplitter = new QSplitter(Qt::Vertical, inventoryPage);
    inventorySplitter->setObjectName("MibLibraryInventoryDetailsSplitter");
    inventorySplitter->setChildrenCollapsible(false);
    tablePane->setMinimumHeight(140);
    detailsTabs->setMinimumHeight(160);
    inventorySplitter->addWidget(tablePane);
    inventorySplitter->addWidget(detailsTabs);
    inventorySplitter->setStretchFactor(0, 11);
    inventorySplitter->setStretchFactor(1, 9);
    const QString inventorySplitterKey = QStringLiteral("mib-library/inventory-details-splitter");
    const QByteArray inventorySplitterState = QSettings().value(inventorySplitterKey).toByteArray();
    const bool inventorySplitterRestored = !inventorySplitterState.isEmpty() &&
        inventorySplitter->restoreState(inventorySplitterState);
    connect(inventorySplitter, &QSplitter::splitterMoved, this,
            [inventorySplitter, inventorySplitterKey]() {
        QSettings().setValue(inventorySplitterKey, inventorySplitter->saveState());
    });
    if (!inventorySplitterRestored) {
        QTimer::singleShot(0, inventorySplitter, [inventorySplitter]() {
            const int height = inventorySplitter->height();
            if (height > 0) inventorySplitter->setSizes({height * 11, height * 9});
        });
    }
    layout->addWidget(inventorySplitter, 1);
    status = new QLabel(tr("Ready"), this); status->setWordWrap(true); layout->addWidget(status);

    auto *profilePage = new QWidget(workspaceTabs);
    auto *profileLayout = new QVBoxLayout(profilePage);
    auto *profileActions = new QHBoxLayout;
    profileCombo = new QComboBox(profilePage); profileCombo->setObjectName("MibProfileEditorSelector");
    auto *newProfile = new QPushButton(tr("New"), profilePage);
    auto *duplicateProfileButton = new QPushButton(tr("Duplicate"), profilePage);
    renameProfileButton = new QPushButton(tr("Rename"), profilePage);
    deleteProfileButton = new QPushButton(tr("Delete"), profilePage);
    profileActions->addWidget(new QLabel(tr("Profile:"), profilePage)); profileActions->addWidget(profileCombo, 1);
    for (QPushButton *button : {newProfile, duplicateProfileButton, renameProfileButton, deleteProfileButton})
        profileActions->addWidget(button);
    profileLayout->addLayout(profileActions);
    auto *profileSplitter = new QSplitter(profilePage);
    auto *availablePane = new QWidget(profileSplitter); auto *availableLayout = new QVBoxLayout(availablePane);
    availableLayout->addWidget(new QLabel(tr("Available MIBs"), availablePane));
    availableSearch = new QLineEdit(availablePane); availableSearch->setObjectName("MibProfileAvailableFilter");
    availableSearch->setPlaceholderText(tr("Filter available"));
    availableList = new QListWidget(availablePane); availableList->setObjectName("MibProfileAvailableList");
    availableList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    availableLayout->addWidget(availableSearch); availableLayout->addWidget(availableList);
    auto *movePane = new QWidget(profileSplitter); auto *moveLayout = new QVBoxLayout(movePane);
    moveLayout->addStretch(); addMemberButton = new QPushButton(tr("Add >"), movePane);
    removeMemberButton = new QPushButton(tr("< Remove"), movePane);
    moveLayout->addWidget(addMemberButton); moveLayout->addWidget(removeMemberButton); moveLayout->addStretch();
    auto *memberPane = new QWidget(profileSplitter); auto *memberLayout = new QVBoxLayout(memberPane);
    memberLayout->addWidget(new QLabel(tr("Profile MIBs"), memberPane));
    memberSearch = new QLineEdit(memberPane); memberSearch->setObjectName("MibProfileMemberFilter");
    memberSearch->setPlaceholderText(tr("Filter profile"));
    memberList = new QListWidget(memberPane); memberList->setObjectName("MibProfileMemberList");
    memberList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    includeStandards = new QCheckBox(tr("Include standard SNMP / MIB-II base"), memberPane);
    includeStandards->setObjectName("MibProfileIncludeStandards");
    memberLayout->addWidget(memberSearch); memberLayout->addWidget(memberList); memberLayout->addWidget(includeStandards);
    memberLayout->addWidget(new QLabel(tr("Required Dependencies"), memberPane));
    requiredList = new QListWidget(memberPane); requiredList->setObjectName("MibProfileRequiredDependencies");
    requiredList->setSelectionMode(QAbstractItemView::NoSelection);
    memberLayout->addWidget(requiredList);
    dependencyCheckSummary = new QLabel(tr("Dependencies need checking"), memberPane);
    dependencyCheckSummary->setObjectName("MibProfileDependencyCheckSummary");
    dependencyCheckSummary->setWordWrap(true); memberLayout->addWidget(dependencyCheckSummary);
    checkDependenciesButton = new QPushButton(tr("Check Dependencies"), memberPane);
    checkDependenciesButton->setObjectName("MibProfileCheckDependencies");
    memberLayout->addWidget(checkDependenciesButton, 0, Qt::AlignLeft);
    downloadMissingButton = new QPushButton(tr("Download Missing"), memberPane);
    downloadMissingButton->setObjectName("MibProfileDownloadMissing");
    downloadMissingButton->setEnabled(false); memberLayout->addWidget(downloadMissingButton, 0, Qt::AlignLeft);
    profileSplitter->addWidget(availablePane); profileSplitter->addWidget(movePane); profileSplitter->addWidget(memberPane);
    profileSplitter->setStretchFactor(0, 1); profileSplitter->setStretchFactor(2, 1);
    profileLayout->addWidget(profileSplitter);
    workspaceTabs->addTab(profilePage, tr("MIB Profiles"));
    connect(search, &QLineEdit::textChanged, this, &MibLibraryWidget::applyFilter);
    connect(sourceFilter, &QComboBox::currentTextChanged, this, &MibLibraryWidget::applyFilter);
    connect(statusFilter, &QComboBox::currentTextChanged, this, &MibLibraryWidget::applyFilter);
    checkHeader->clicked = [this, checkHeader]() {
        setFilteredSelection(checkHeader->checkState() != Qt::Checked);
    };
    connect(refreshButton, &QPushButton::clicked, this, &MibLibraryWidget::refreshCatalog);
    connect(cancelButton, &QPushButton::clicked, transport, &MibDownloadTransport::cancel);
    connect(downloadButton, &QPushButton::clicked, this, [this]() { downloadSelected(false); });
    connect(checkUpdates, &QPushButton::clicked, this, [this]() {
        status->setText(tr("No reliably comparable installed revisions are available"));
    });
    connect(installDependenciesButton, &QPushButton::clicked, this, [this]() { downloadSelected(true); });
    connect(table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item && item->column() == 0) { updateSelectionUi(); resolveSelected(); }
    });
    connect(openEditorButton, &QPushButton::clicked, this, &MibLibraryWidget::openCurrentModule);
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { openCurrentModule(); });
    connect(profileCombo, &QComboBox::currentIndexChanged, this, &MibLibraryWidget::profileChanged);
    connect(newProfile, &QPushButton::clicked, this, &MibLibraryWidget::createProfile);
    connect(duplicateProfileButton, &QPushButton::clicked, this, &MibLibraryWidget::duplicateProfile);
    connect(renameProfileButton, &QPushButton::clicked, this, &MibLibraryWidget::renameProfile);
    connect(deleteProfileButton, &QPushButton::clicked, this, &MibLibraryWidget::deleteProfile);
    connect(addMemberButton, &QPushButton::clicked, this, &MibLibraryWidget::addProfileMembers);
    connect(removeMemberButton, &QPushButton::clicked, this, &MibLibraryWidget::removeProfileMembers);
    connect(downloadMissingButton, &QPushButton::clicked, this, &MibLibraryWidget::downloadProfileMissing);
    connect(checkDependenciesButton, &QPushButton::clicked, this, &MibLibraryWidget::checkProfileDependencies);
    connect(includeStandards, &QCheckBox::toggled, this, [this]() { saveCurrentProfile(); });
    connect(availableSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i=0;i<availableList->count();++i) availableList->item(i)->setHidden(!availableList->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(memberSearch, &QLineEdit::textChanged, this, [this](const QString &text) {
        for (int i=0;i<memberList->count();++i) memberList->item(i)->setHidden(!memberList->item(i)->text().contains(text, Qt::CaseInsensitive));
    });
    connect(table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int) {
        Q_UNUSED(row); showCurrentInfo();
    });
    connect(transport, &MibDownloadTransport::finished, this, [this](const MibDownloadResult &result) {
        if (catalogRefreshInProgress) {
            catalogRefreshInProgress = false;
            setOperationActive(false);
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
                callbacks.validate)) {
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
            setOperationActive(false); refresh();
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
    loadCachedCatalog(); refresh(); refreshProfiles();
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
    setOperationActive(true);
    status->setText(tr("Refreshing authoritative IANA catalog…"));
    DiagnosticLogger::log("MIB", status->text());
    transport->get(IanaMibSourceProvider::indexUrl());
}

void MibLibraryWidget::refresh()
{
    const QStringList previouslySelected = selectedModules();
    const QSet<QString> selectedBefore(previouslySelected.cbegin(), previouslySelected.cend());
    const QString currentModule = table->currentRow() >= 0
        ? table->item(table->currentRow(), 1)->text() : QString();
    const int verticalPosition = table->verticalScrollBar()->value();
    const QSignalBlocker tableBlocker(table);
    records = library.inventory(bundledPaths, catalog,
        callbacks.localInventory ? callbacks.localInventory() : QList<MibModuleRecord>{});
    table->setRowCount(records.size());
    QStringList sources{tr("All origins")};
    for (int row = 0; row < records.size(); ++row) {
        const auto &record = records[row];
        auto *checkItem = new QTableWidgetItem;
        checkItem->setCheckState(selectedBefore.contains(record.moduleName)
            ? Qt::Checked : Qt::Unchecked);
        checkItem->setFlags((checkItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        auto *moduleItem = new QTableWidgetItem(record.moduleName);
        const QString failure = sessionState.failureReason(record.moduleName);
        if (!failure.isEmpty()) {
            moduleItem->setIcon(style()->standardIcon(QStyle::SP_MessageBoxCritical));
            moduleItem->setToolTip(tr("Failed: %1").arg(failure));
        }
        auto *revisionItem = new QTableWidgetItem(record.revision);
        auto *originItem = new QTableWidgetItem(originText(record));
        for (QTableWidgetItem *item : {moduleItem, revisionItem, originItem})
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(row, 0, checkItem);
        table->setItem(row, 1, moduleItem);
        table->setItem(row, 2, revisionItem);
        table->setItem(row, 3, originItem);
        if (!sources.contains(originText(record))) sources.append(originText(record));
    }
    const QString oldSource = sourceFilter->currentText();
    sourceFilter->clear(); sourceFilter->addItems(sources);
    sourceFilter->setCurrentText(oldSource); applyFilter();
    if (!currentModule.isEmpty()) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 1)->text() == currentModule) {
                table->setCurrentCell(row, 1); break;
            }
    }
    table->verticalScrollBar()->setValue(verticalPosition);
    status->setText(sourceStatusText.isEmpty()
        ? tr("%n modules in library", nullptr, records.size())
        : tr("%n modules in library — %1", nullptr, records.size()).arg(sourceStatusText));
    refreshProfileLists();
    showCurrentInfo();
    updateSelectionUi();
}

void MibLibraryWidget::applyFilter()
{
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool matchSearch = table->item(row, 1)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 2)->text().contains(search->text(), Qt::CaseInsensitive) ||
            table->item(row, 3)->text().contains(search->text(), Qt::CaseInsensitive);
        const bool matchSource = sourceFilter->currentIndex() <= 0 || table->item(row, 3)->text() == sourceFilter->currentText();
        table->setRowHidden(row, !(matchSearch && matchSource));
    }
    updateSelectionUi();
}
void MibLibraryWidget::setFilteredSelection(bool selected)
{
    const QSignalBlocker blocker(table);
    for (int row = 0; row < table->rowCount(); ++row)
        if (!table->isRowHidden(row))
            table->item(row, 0)->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
    updateSelectionUi(); resolveSelected();
}

void MibLibraryWidget::updateSelectionUi()
{
    int visible = 0, visibleSelected = 0, totalSelected = 0;
    for (int row = 0; row < table->rowCount(); ++row) {
        const bool selected = table->item(row, 0)->checkState() == Qt::Checked;
        totalSelected += selected ? 1 : 0;
        if (!table->isRowHidden(row)) { ++visible; visibleSelected += selected ? 1 : 0; }
    }
    auto *header = static_cast<CheckableHeaderView *>(table->horizontalHeader());
    if (header) header->setCheckState(visibleSelected == 0 ? Qt::Unchecked
        : visibleSelected == visible ? Qt::Checked : Qt::PartiallyChecked);
    selectedCount->setText(tr("%n selected", nullptr, totalSelected));
    const bool enabled = totalSelected > 0 && downloadQueue.isEmpty();
    downloadButton->setEnabled(enabled); installDependenciesButton->setEnabled(enabled);
}

void MibLibraryWidget::setOperationActive(bool active)
{
    cancelButton->setVisible(active);
    refreshButton->setEnabled(!active);
    if (active) { downloadButton->setEnabled(false); installDependenciesButton->setEnabled(false); }
    else updateSelectionUi();
}

QStringList MibLibraryWidget::selectedModules() const
{
    QStringList result;
    for (int row = 0; row < table->rowCount(); ++row)
        if (table->item(row, 0)->checkState() == Qt::Checked)
            result.append(table->item(row, 1)->text());
    return result;
}

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
    QStringList targets = selectedModules();
    if (targets.isEmpty() && table->currentRow() >= 0)
        targets.append(table->item(table->currentRow(), 1)->text());
    const MibDependencyPlan plan = MibDependencyResolver().resolve(targets, known, working);
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
    setOperationActive(true); downloadNext();
}

void MibLibraryWidget::downloadNext()
{
    if (downloadQueue.isEmpty()) {
        setOperationActive(false); refresh();
        if (callbacks.downloadsCompleted) callbacks.downloadsCompleted(requestedTargets, loadAfterDownload);
        status->setText(tr("Download and validation complete")); return;
    }
    const MibCatalogEntry *entry = catalog.find(downloadQueue.first());
    if (!entry) { status->setText(tr("No catalog entry for %1").arg(downloadQueue.first())); downloadQueue.clear(); setOperationActive(false); return; }
    status->setText(tr("Downloading %1 from %2").arg(entry->moduleName, entry->sourceName));
    DiagnosticLogger::log("MIB", tr("Downloading module=%1 initial=%2 source=%3")
        .arg(entry->moduleName, QUrl(entry->url).toString(QUrl::FullyEncoded), entry->sourceName));
    transport->get(QUrl(entry->url));
}

QString MibLibraryWidget::originText(const MibLibraryRecord &record) const
{
    return MibLibraryOriginText(record);
}

QStringList MibLibraryWidget::availableModuleNames() const
{
    QStringList result;
    for (const MibLibraryRecord &record : records)
        if (!record.localPath.isEmpty() && record.status != MibLibraryStatus::Invalid &&
            record.status != MibLibraryStatus::Conflict) result.append(record.moduleName);
    result.removeDuplicates(); result.sort(Qt::CaseInsensitive); return result;
}

MibCatalog MibLibraryWidget::dependencyCatalog() const
{
    MibCatalog result = catalog;
    QList<MibCatalogEntry> entries = result.entries();
    for (const MibLibraryRecord &record : records) {
        if (record.localPath.isEmpty()) continue;
        QFile file(record.localPath); if (!file.open(QIODevice::ReadOnly)) continue;
        const auto scan = MibImportScanner::scan(file.readAll());
        MibCatalogEntry entry;
        if (const MibCatalogEntry *existing = result.find(record.moduleName)) entry = *existing;
        entry.moduleName = record.moduleName; entry.imports = scan.imports;
        bool replaced = false;
        for (MibCatalogEntry &candidate : entries) if (candidate.moduleName == entry.moduleName) {
            candidate = entry; replaced = true; break;
        }
        if (!replaced) entries.append(entry);
    }
    result.setEntries(entries); return result;
}

void MibLibraryWidget::showCurrentInfo()
{
    const int row = table->currentRow();
    const bool valid = row >= 0 && row < records.size();
    openEditorButton->setEnabled(valid && !records[row].localPath.isEmpty());
    if (!valid) { for (QLabel *label : infoValues) label->clear(); return; }
    const MibLibraryRecord &record = records[row];
    const MibModuleRecord metadata = callbacks.metadata
        ? callbacks.metadata(record.moduleName, record.localPath) : MibModuleRecord{};
    const QString unavailable = QStringLiteral("—");
    infoValues["module"]->setText(metadata.name.isEmpty() ? record.moduleName : metadata.name);
    infoValues["moduleOid"]->setText(metadata.rootOid.isEmpty() ? unavailable : metadata.rootOid);
    infoValues["lastUpdated"]->setText(metadata.lastRevision.isValid()
        ? metadata.lastRevision.toUTC().toString(QStringLiteral("yyyy-MM-dd HH:mm 'UTC'")) : unavailable);
    infoValues["organization"]->setText(metadata.organization.isEmpty()
        ? unavailable : reflowMibDisplayText(metadata.organization));
    infoValues["description"]->setText(metadata.description.isEmpty()
        ? unavailable : reflowMibDisplayText(metadata.description));
    contactInfo->setPlainText(metadata.contactInfo.isEmpty()
        ? unavailable : normalizeContactDisplayText(metadata.contactInfo));
    infoValues["reference"]->setText(metadata.reference.isEmpty()
        ? unavailable : reflowMibDisplayText(metadata.reference));
    QStringList history;
    for (const MibRevisionRecord &revision : metadata.revisions) {
        QString item = revision.date.toUTC().toString(QStringLiteral("yyyy-MM-dd"));
        if (!revision.description.isEmpty())
            item += QStringLiteral("\n") + reflowMibDisplayText(revision.description);
        history.append(item);
    }
    infoValues["revisionHistory"]->setText(history.isEmpty() ? unavailable : history.join(QStringLiteral("\n\n")));
    const QString reason = sessionState.failureReason(record.moduleName);
    const MibLibraryFileInfo fileInfo = MibLibraryFileInformation(record, reason);
    const auto setFileField = [this](const QString &key, const QString &value, bool visible = true) {
        infoValues[key]->setText(value);
        infoValues[key]->setVisible(visible);
        infoLabels[key]->setVisible(visible);
    };
    setFileField("origin", fileInfo.origin);
    setFileField("revision", fileInfo.revision);
    setFileField("filename", fileInfo.filename);
    setFileField("path", fileInfo.localPath);
    setFileField("provider", fileInfo.provider, fileInfo.showProvider);
    setFileField("url", fileInfo.sourceUrl, fileInfo.showSourceUrl);
    setFileField("timestamp", fileInfo.timestamp, fileInfo.showTimestamp);
    setFileField("sha", fileInfo.sha256, fileInfo.showSha256);
    setFileField("state", fileInfo.state, fileInfo.showState);
    QSignalBlocker blocker(table);
    table->selectRow(row);
    resolveSelected();
}

void MibLibraryWidget::openCurrentModule()
{
    const int row = table->currentRow();
    if (row >= 0 && row < records.size() && !records[row].localPath.isEmpty())
        emit openModuleRequested(records[row].localPath,
                                 records[row].status == MibLibraryStatus::Bundled);
}

void MibLibraryWidget::refreshProfiles(const QString &selectId)
{
    QString id = selectId;
    if (id.isEmpty()) id = QSettings().value("mib-library/current-profile",
                                             MibProfileDefinitions::allId()).toString();
    id = MibProfileDefinitions::validCurrentId(id, profiles.profiles());
    const QSignalBlocker blocker(profileCombo);
    profileCombo->clear();
    for (const MibProfileRecord &profile : profiles.profiles())
        profileCombo->addItem(profile.name, profile.id);
    int index = profileCombo->findData(id);
    if (index < 0) index = profileCombo->findData(MibProfileDefinitions::allId());
    profileCombo->setCurrentIndex(index);
    profileChanged();
}

void MibLibraryWidget::selectProfile(const QString &id)
{
    const int index = profileCombo->findData(id);
    if (index >= 0 && index != profileCombo->currentIndex()) profileCombo->setCurrentIndex(index);
    else if (index >= 0) profileChanged();
}

void MibLibraryWidget::profileChanged()
{
    const QString id = profileCombo->currentData().toString();
    const MibProfileRecord *profile = profiles.find(id);
    if (!profile) return;
    const MibProfileRecord selectedProfile = *profile;
    QSettings().setValue("mib-library/current-profile", id);
    const bool editable = selectedProfile.type == MibProfileType::Custom;
    renameProfileButton->setEnabled(editable); deleteProfileButton->setEnabled(editable);
    addMemberButton->setEnabled(editable); removeMemberButton->setEnabled(editable);
    includeStandards->setEnabled(editable);
    refreshProfileLists();
    const QStringList signatureModules = selectedProfile.type == MibProfileType::All
        ? availableModuleNames() : selectedProfile.explicitModules;
    const QString signature = MibDependencyIndex::profileSignature(signatureModules,
                                                                    selectedProfile.includeStandardBase);
    const MibProfileDependencyCheck cached = callbacks.cachedDependencies
        ? callbacks.cachedDependencies(id, signature) : MibProfileDependencyCheck{};
    if (!cached.checkedUtc.isNull()) emit profileSelectionChanged(id, cached.effectiveModules);
    else {
        const auto effective = MibProfileResolver().resolve(selectedProfile, availableModuleNames(), dependencyCatalog());
        emit profileSelectionChanged(id, effective.effectiveModules);
    }
}

void MibLibraryWidget::refreshProfileLists()
{
    if (!profileCombo || profileCombo->currentIndex() < 0) return;
    const MibProfileRecord *profile = profiles.find(profileCombo->currentData().toString());
    if (!profile) return;
    const MibProfileEffectiveSet effective = MibProfileResolver().resolve(
        *profile, availableModuleNames(), dependencyCatalog());
    QStringList selectedAvailable, selectedMembers;
    for (QListWidgetItem *item : availableList->selectedItems()) selectedAvailable.append(item->text());
    for (QListWidgetItem *item : memberList->selectedItems()) selectedMembers.append(item->text());
    const int availableScroll = availableList->verticalScrollBar()->value();
    const int memberScroll = memberList->verticalScrollBar()->value();
    const QSignalBlocker blocker(includeStandards);
    includeStandards->setChecked(profile->includeStandardBase);
    memberList->clear(); memberList->addItems(profile->type == MibProfileType::All
        ? availableModuleNames() : profile->explicitModules);
    QStringList available = availableModuleNames();
    for (const QString &member : profile->explicitModules) available.removeAll(member);
    availableList->clear(); availableList->addItems(available);
    for (int row = 0; row < availableList->count(); ++row) {
        QListWidgetItem *item = availableList->item(row);
        item->setHidden(!item->text().contains(availableSearch->text(), Qt::CaseInsensitive));
        item->setSelected(selectedAvailable.contains(item->text()));
    }
    for (int row = 0; row < memberList->count(); ++row) {
        QListWidgetItem *item = memberList->item(row);
        item->setHidden(!item->text().contains(memberSearch->text(), Qt::CaseInsensitive));
        item->setSelected(selectedMembers.contains(item->text()));
    }
    availableList->verticalScrollBar()->setValue(availableScroll);
    memberList->verticalScrollBar()->setValue(memberScroll);
    requiredList->clear();
    bool downloadableMissing = false;
    for (const MibProfileEffectiveSet::Requirement &requirement : effective.requirements) {
        const QString state = requirement.missing ? tr("Missing") : tr("Available");
        auto *item = new QListWidgetItem(tr("%1 — %2 — %3")
            .arg(requirement.moduleName, state, requirement.reason), requiredList);
        item->setData(Qt::UserRole, requirement.moduleName);
        item->setData(Qt::UserRole + 1, requirement.missing);
        if (requirement.missing) {
            item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            downloadableMissing = downloadableMissing || catalog.find(requirement.moduleName);
        }
    }
    downloadMissingButton->setEnabled(downloadableMissing && downloadQueue.isEmpty());
    const QStringList signatureModules = profile->type == MibProfileType::All
        ? availableModuleNames() : profile->explicitModules;
    const QString signature = MibDependencyIndex::profileSignature(signatureModules,
                                                                    profile->includeStandardBase);
    const MibProfileDependencyCheck cached = callbacks.cachedDependencies
        ? callbacks.cachedDependencies(profile->id, signature) : MibProfileDependencyCheck{};
    if (cached.checkedUtc.isNull()) dependencyCheckSummary->setText(tr("Dependencies need checking"));
    else dependencyCheckSummary->setText(tr("%1 profile MIBs\n%2 dependencies\n%3 unresolved\nLast checked: %4")
        .arg(signatureModules.size()).arg(cached.dependencies.size()).arg(cached.unresolved.size())
        .arg(cached.checkedUtc.toLocalTime().toString(Qt::ISODate)));
}

void MibLibraryWidget::checkProfileDependencies()
{
    const MibProfileRecord *profile = profiles.find(profileCombo->currentData().toString());
    if (!profile || !callbacks.checkDependencies) return;
    checkDependenciesButton->setEnabled(false); dependencyCheckSummary->setText(tr("Checking dependencies…"));
    const QStringList seeds = profile->type == MibProfileType::All
        ? availableModuleNames() : profile->explicitModules;
    QString error; const MibProfileDependencyCheck check = callbacks.checkDependencies(
        profile->id, seeds, profile->includeStandardBase, &error);
    checkDependenciesButton->setEnabled(true);
    if (!error.isEmpty()) { dependencyCheckSummary->setText(error); return; }
    refresh(); refreshProfileLists();
    status->setText(check.unresolved.isEmpty() ? tr("Dependencies checked")
        : tr("%n unresolved dependencies", nullptr, check.unresolved.size()));
    profileChanged();
}

void MibLibraryWidget::createProfile()
{
    bool ok = false; const QString name = QInputDialog::getText(this, tr("New MIB Profile"), tr("Name:"), QLineEdit::Normal, {}, &ok);
    if (!ok) return; QString error; const QString id = profiles.create(name, &error);
    if (id.isEmpty()) { status->setText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::duplicateProfile()
{
    const QString current = profileCombo->currentData().toString();
    bool ok = false; const QString name = QInputDialog::getText(this, tr("Duplicate MIB Profile"), tr("Name:"), QLineEdit::Normal, profileCombo->currentText() + tr(" Copy"), &ok);
    if (!ok) return; QString error; const QString id = profiles.duplicate(current, name, &error);
    if (id.isEmpty()) { status->setText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::renameProfile()
{
    bool ok = false; const QString name = QInputDialog::getText(this, tr("Rename MIB Profile"), tr("Name:"), QLineEdit::Normal, profileCombo->currentText(), &ok);
    if (!ok) return; const QString id = profileCombo->currentData().toString(); QString error;
    if (!profiles.rename(id, name, &error)) { status->setText(error); return; }
    refreshProfiles(id); emit profilesChanged();
}

void MibLibraryWidget::deleteProfile()
{
    const QString id = profileCombo->currentData().toString(); QString error;
    if (!profiles.remove(id, &error)) { status->setText(error); return; }
    refreshProfiles(MibProfileDefinitions::allId()); emit profilesChanged();
}

void MibLibraryWidget::saveCurrentProfile()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    MibProfileRecord changed = *current; changed.includeStandardBase = includeStandards->isChecked();
    QString error; if (!profiles.update(changed, &error)) status->setText(error);
    refreshProfileLists(); emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::addProfileMembers()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    MibProfileRecord changed = *current;
    for (QListWidgetItem *item : availableList->selectedItems()) changed.explicitModules.append(item->text());
    changed.explicitModules.removeDuplicates(); QString error;
    if (!profiles.update(changed, &error)) status->setText(error);
    refreshProfileLists(); emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::removeProfileMembers()
{
    const MibProfileRecord *current = profiles.find(profileCombo->currentData().toString());
    if (!current || current->type != MibProfileType::Custom) return;
    MibProfileRecord changed = *current;
    for (QListWidgetItem *item : memberList->selectedItems()) changed.explicitModules.removeAll(item->text());
    QString error; if (!profiles.update(changed, &error)) status->setText(error);
    refreshProfileLists(); emit profilesChanged(); profileChanged();
}

void MibLibraryWidget::downloadProfileMissing()
{
    requestedTargets.clear();
    for (int row = 0; row < requiredList->count(); ++row) {
        QListWidgetItem *item = requiredList->item(row);
        const QString module = item->data(Qt::UserRole).toString();
        if (item->data(Qt::UserRole + 1).toBool() && catalog.find(module))
            requestedTargets.append(module);
    }
    requestedTargets.removeDuplicates();
    if (requestedTargets.isEmpty()) return;
    for (const QString &module : requestedTargets) sessionState.begin(module);
    QMap<QString, MibLibraryStatus> known;
    for (const MibLibraryRecord &record : records) known.insert(record.moduleName, record.status);
    downloadQueue = MibDependencyResolver().resolve(requestedTargets, known, catalog).orderedDownloads;
    loadAfterDownload = false;
    setOperationActive(true); downloadMissingButton->setEnabled(false);
    downloadNext();
}
