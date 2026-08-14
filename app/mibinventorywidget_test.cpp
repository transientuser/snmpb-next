#include "mibdownloadtransport.h"
#include "miblibrarywidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QGroupBox>
#include <QComboBox>
#include <QSplitter>
#include <QTextEdit>
#include <iostream>

namespace {
class IdleTransport : public MibDownloadTransport
{
public:
    void get(const QUrl &url) override { requested = url; }
    void cancel() override {}
    void finishWithError() { MibDownloadResult result; result.error = "scripted"; emit finished(result); }
    QUrl requested;
};
bool check(bool value, const char *message) { if (!value) std::cerr << message << '\n'; return value; }
void writeMib(const QString &path, const QByteArray &content)
{
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(content);
}
void clickHeaderCheckbox(QHeaderView *header)
{
    header->findChild<QCheckBox *>("MibLibraryHeaderSelectAll")->click();
}
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("SnmpBNextTests");
    QCoreApplication::setApplicationName("MibInventoryWidgetTest");
    QSettings().remove("mib-library/dependencies-status-width");
    QSettings().remove("mib-library/inventory-details-splitter");
    QTemporaryDir temp;
    writeMib(temp.filePath("A-MIB"), "A-MIB DEFINITIONS ::= BEGIN\nIMPORTS x FROM DEP-MIB;\nEND\n");
    writeMib(temp.filePath("DEP-MIB"), "DEP-MIB DEFINITIONS ::= BEGIN\nEND\n");
    writeMib(temp.filePath("IANA-ONE-MIB"), "IANA-ONE-MIB DEFINITIONS ::= BEGIN\nEND\n");
    writeMib(temp.filePath("IANA-TWO-MIB"), "IANA-TWO-MIB DEFINITIONS ::= BEGIN\nEND\n");
    IdleTransport transport;
    MibModuleRecord sourceMetadata;
    sourceMetadata.name = "A-MIB"; sourceMetadata.rootOid = "1.3.6.1.2.1.60";
    sourceMetadata.lastRevision = QDateTime(QDate(1998, 9, 28), QTime(10, 0), Qt::UTC);
    sourceMetadata.organization = "  Org\r\nGroup  ";
    sourceMetadata.contactInfo =
        "Name  \r\nCo\r\n\r\n\r\nAddr\nPhone\nMail  \r\nLine 7\nLine 8\nLine 9\n"
        "Line 10\nLine 11\nLine 12\nLine 13\nLine 14\r\n\r\n";
    sourceMetadata.description =
        "  First\r\nline.  \r\n\r\n\r\nNext\npart.  ";
    sourceMetadata.reference = " RFC\nsection ";
    sourceMetadata.revisions.append({sourceMetadata.lastRevision,
        " Initial\r\ndetail.\r\n\r\n\r\nNext\nparagraph. "});
    MibModuleRecord layoutMetadata;
    layoutMetadata.name = "A-MIB"; layoutMetadata.rootOid = "1.3.6.1.2.1.60";
    layoutMetadata.lastRevision = sourceMetadata.lastRevision;
    layoutMetadata.organization = "Organization\nWorking Group";
    layoutMetadata.contactInfo = "Maintainer\nmaintainer@example.invalid";
    layoutMetadata.description = "First line\nSecond line";
    layoutMetadata.reference = "RFC fixture\nSection 1";
    layoutMetadata.revisions.append({layoutMetadata.lastRevision, "Initial revision\nDetails"});
    MibLibraryWidget::Callbacks callbacks;
    callbacks.metadata = [&layoutMetadata](const QString &, const QString &) {
        return layoutMetadata;
    };
    int dependencyChecks = 0; MibProfileDependencyCheck cachedDependencyCheck;
    callbacks.checkDependencies = [&dependencyChecks, &cachedDependencyCheck](const QString &, const QStringList &members,
                                                       bool, QString *) {
        ++dependencyChecks; MibProfileDependencyCheck result;
        result.checkedUtc = QDateTime::currentDateTimeUtc(); result.effectiveModules = members;
        result.dependencies = {"DEP-MIB"}; cachedDependencyCheck = result; return result;
    };
    callbacks.cachedDependencies = [&cachedDependencyCheck](const QString &, const QString &) {
        return cachedDependencyCheck;
    };
    MibLibraryWidget widget({temp.path()}, nullptr, &transport, callbacks);
    bool ok = true;
    auto *table = widget.findChild<QTableWidget *>("MibLibraryTable");
    ok &= check(table && table->columnCount() == 4, "Inventory has checkbox plus three data columns");
    ok &= check(table && table->horizontalHeaderItem(3)->text() == "Origin", "Source renamed Origin");
    ok &= check(table && table->editTriggers() == QAbstractItemView::NoEditTriggers,
                "Inventory table disables editing");
    for (int row=0; table && row<table->rowCount(); ++row)
        for (int column=0; column<table->columnCount(); ++column)
            ok &= check(!(table->item(row,column)->flags() & Qt::ItemIsEditable),
                        "Inventory item is read-only");
    ok &= check(table && table->item(0,3)->text() == "Built-in", "built-in origin displayed");
    QString opened; bool readOnly = false;
    QObject::connect(&widget, &MibLibraryWidget::openModuleRequested,
                     [&](const QString &path, bool immutable) { opened = path; readOnly = immutable; });
    table->setCurrentCell(0, 0);
    QMetaObject::invokeMethod(table, "cellDoubleClicked", Qt::DirectConnection,
                              Q_ARG(int, 0), Q_ARG(int, 0));
    ok &= check(opened == temp.filePath("A-MIB") && readOnly,
                "double click opens selected built-in file read-only");
    auto *moduleInfo = widget.findChild<QLabel *>("MibLibraryInfo_module");
    ok &= check(moduleInfo && moduleInfo->text() == "A-MIB", "Info reflects selected record");
    ok &= check(widget.findChild<QLabel *>("MibLibraryInfo_moduleOid")->text() ==
                "1.3.6.1.2.1.60", "Info displays numeric MODULE-IDENTITY OID");
    ok &= check(widget.findChild<QLabel *>("MibLibraryInfo_lastUpdated")->text() ==
                "1998-09-28 10:00 UTC", "Info displays human-readable latest revision");
    MibLibraryWidget::Callbacks normalizationCallbacks;
    normalizationCallbacks.metadata = [&sourceMetadata](const QString &, const QString &) {
        return sourceMetadata;
    };
    MibLibraryWidget normalizationWidget({temp.path()}, nullptr, &transport,
                                         normalizationCallbacks);
    normalizationWidget.findChild<QTableWidget *>("MibLibraryTable")->setCurrentCell(0, 0);
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_organization")->text() ==
                "Org Group",
                "Organization hard wraps are reflowed for display");
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_description")->text() ==
                "First line.\n\nNext part.",
                "Description reflows hard wraps and preserves one paragraph break");
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_reference")->text() ==
                "RFC section",
                "Reference hard wraps are reflowed for display");
    ok &= check(normalizationWidget.findChild<QLabel *>("MibLibraryInfo_revisionHistory")->text() ==
                "1998-09-28\nInitial detail.\n\nNext paragraph.",
                "Revision descriptions reflow without losing paragraph breaks");
    auto *contact = normalizationWidget.findChild<QTextEdit *>("MibLibraryInfo_contact");
    const QString expectedContact =
        "Name\nCo\n\nAddr\nPhone\nMail\nLine 7\nLine 8\nLine 9\nLine 10\nLine 11\n"
        "Line 12\nLine 13\nLine 14";
    ok &= check(contact && contact->toPlainText() == expectedContact,
                "Contact Information retains logical lines and collapses blank lines");
    ok &= check(contact && contact->isReadOnly() &&
                (contact->textInteractionFlags() & Qt::TextSelectableByMouse) &&
                contact->lineWrapMode() == QTextEdit::WidgetWidth,
                "Contact Information is read-only and selectable");
    normalizationWidget.resize(1100, 700); normalizationWidget.show(); application.processEvents();
    const int visibleContactLines = contact->maximumHeight() / contact->fontMetrics().lineSpacing();
    ok &= check(visibleContactLines >= 9 && visibleContactLines <= 11 &&
                contact->verticalScrollBar()->maximum() > 0 &&
                contact->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
                "Contact box shows about ten wrapped lines with vertical scrolling only as needed");
    ok &= check(sourceMetadata.organization == "  Org\r\nGroup  " &&
                sourceMetadata.description ==
                    "  First\r\nline.  \r\n\r\n\r\nNext\npart.  " &&
                sourceMetadata.contactInfo ==
                    "Name  \r\nCo\r\n\r\n\r\nAddr\nPhone\nMail  \r\nLine 7\nLine 8\nLine 9\n"
                    "Line 10\nLine 11\nLine 12\nLine 13\nLine 14\r\n\r\n" &&
                sourceMetadata.revisions.first().description ==
                    " Initial\r\ndetail.\r\n\r\n\r\nNext\nparagraph. ",
                "presentation formatting leaves source metadata unchanged");
    auto *moduleGroup = widget.findChild<QGroupBox *>("MibLibraryModuleInformation");
    auto *fileGroup = widget.findChild<QGroupBox *>("MibLibraryFileInformation");
    auto *infoSplitter = widget.findChild<QSplitter *>("MibLibraryInformationSplitter");
    auto *moduleScroll = widget.findChild<QScrollArea *>("MibLibraryModuleInformationScroll");
    auto *fileScroll = widget.findChild<QScrollArea *>("MibLibraryFileInformationScroll");
    ok &= check(moduleGroup && fileGroup && infoSplitter &&
                infoSplitter->orientation() == Qt::Horizontal &&
                moduleScroll && fileScroll &&
                infoSplitter->indexOf(moduleScroll) == 0 && infoSplitter->indexOf(fileScroll) == 1 &&
                moduleScroll->widget() == moduleGroup && fileScroll->widget() == fileGroup,
                "Module and File Information use side-by-side splitter");
    widget.resize(1100, 700); widget.show(); application.processEvents();
    auto *header = table->horizontalHeader();
    auto *search = widget.findChild<QLineEdit *>("MibLibrarySearch");
    auto *count = widget.findChild<QLabel *>("MibLibrarySelectedCount");
    auto *download = widget.findChild<QPushButton *>("MibLibraryDownload");
    auto *install = widget.findChild<QPushButton *>("MibLibraryInstallWithDependencies");
    auto *refresh = widget.findChild<QPushButton *>("MibLibraryRefreshCatalog");
    auto *updates = widget.findChild<QPushButton *>("MibLibraryCheckForUpdates");
    auto *cancel = widget.findChild<QPushButton *>("MibLibraryCancel");
    ok &= check(header->property("checkState").toInt() == Qt::Unchecked &&
                count->text() == "0 selected" && !download->isEnabled() && !install->isEnabled(),
                "empty checkbox selection has unchecked header, zero count, and disabled actions");
    auto *headerCheckbox = header->findChild<QCheckBox *>("MibLibraryHeaderSelectAll");
    ok &= check(headerCheckbox &&
                headerCheckbox->geometry().left() <= header->sectionViewportPosition(0) + 6 &&
                headerCheckbox->geometry().center().x() <=
                    header->sectionViewportPosition(0) + header->sectionSize(0) / 2,
                "Inventory header checkbox is left-aligned with row checkboxes");
    ok &= check(download->text() == "Download" &&
                install->text() == "Install with Dependencies" &&
                refresh->text() == "Refresh Catalog" && updates->text() == "Check for Updates",
                "Inventory actions use final grouped labels");
    table->item(0, 0)->setCheckState(Qt::Checked); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::PartiallyChecked &&
                count->text() == "1 selected" && download->isEnabled() && install->isEnabled(),
                "individual checkbox creates partial header and enables selected actions");
    search->setText("IANA"); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Unchecked,
                "filter change recalculates header against displayed rows");
    clickHeaderCheckbox(header); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Checked &&
                count->text() == "3 selected" && table->item(0, 0)->checkState() == Qt::Checked,
                "unchecked header selects filtered rows without clearing hidden selection");
    table->item(2, 0)->setCheckState(Qt::Unchecked); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::PartiallyChecked,
                "individual filtered checkbox updates header to partial");
    clickHeaderCheckbox(header); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Checked && count->text() == "3 selected",
                "partial header click selects all filtered rows");
    clickHeaderCheckbox(header); application.processEvents();
    ok &= check(header->property("checkState").toInt() == Qt::Unchecked &&
                count->text() == "1 selected" && table->item(0, 0)->checkState() == Qt::Checked,
                "checked header clears filtered rows while preserving hidden selection");
    ok &= check(refresh->isEnabled() && updates->isEnabled() && !cancel->isVisible(),
                "catalog actions are selection-independent and Cancel is hidden while idle");
    refresh->click(); application.processEvents();
    ok &= check(cancel->isVisible() && !refresh->isEnabled() && transport.requested.isValid(),
                "Cancel is visible during scripted catalog refresh");
    transport.finishWithError(); application.processEvents();
    ok &= check(!cancel->isVisible(), "Cancel hides after catalog operation finishes");
    bool resolveButtonFound = false;
    for (QPushButton *button : widget.findChildren<QPushButton *>())
        resolveButtonFound |= button->text() == "Resolve Dependencies";
    ok &= check(!resolveButtonFound, "standalone Resolve Dependencies button is absent");
    const QList<int> infoSizes = infoSplitter->sizes();
    const int infoWidth = infoSizes.size() == 2 ? infoSizes[0] + infoSizes[1] : 0;
    const double moduleShare = infoWidth > 0
        ? static_cast<double>(infoSizes[0]) / infoWidth : 0.0;
    if (moduleShare < 0.62 || moduleShare > 0.68)
        std::cerr << "Information splitter module share: " << moduleShare << '\n';
    ok &= check(infoSizes.size() == 2 && moduleShare >= 0.62 && moduleShare <= 0.68,
                "Module and File Information retain an approximately 65/35 responsive share");
    ok &= check(widget.findChild<QLabel *>("MibLibraryInfo_origin")->text() == "Built-in" &&
                !widget.findChild<QLabel *>("MibLibraryInfo_provider")->isVisible() &&
                !widget.findChild<QLabel *>("MibLibraryInfo_state")->isVisible(),
                "built-in UI omits Bundled provider and redundant state");
    auto *dependencyTree = widget.findChild<QTreeWidget *>("MibDependencyTree");
    ok &= check(dependencyTree && dependencyTree->topLevelItemCount() == 1,
                "Dependencies reflects selected module");
    auto *inventorySplitter = widget.findChild<QSplitter *>("MibLibraryInventoryDetailsSplitter");
    auto *tablePane = widget.findChild<QWidget *>("MibLibraryInventoryTablePane");
    ok &= check(inventorySplitter && inventorySplitter->orientation() == Qt::Vertical &&
                !inventorySplitter->childrenCollapsible() && tablePane &&
                inventorySplitter->indexOf(tablePane) == 0 &&
                inventorySplitter->indexOf(widget.findChild<QTabWidget *>("MibLibraryDetailsTabs")) == 1,
                "Inventory table and lower information area use a non-collapsible vertical splitter");
    const QList<int> inventorySizes = inventorySplitter->sizes();
    ok &= check(inventorySizes.size() == 2 && inventorySizes[0] >= 140 &&
                inventorySizes[1] >= 160,
                "Inventory splitter allocates meaningful default space to both areas");
    ok &= check(moduleScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                fileScroll->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded &&
                moduleScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                fileScroll->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff,
                "Module and File Information panes scroll vertically and wrap horizontally");
    auto *dependencyHeader = dependencyTree->header();
    widget.findChild<QTabWidget *>("MibLibraryDetailsTabs")->setCurrentWidget(dependencyTree);
    application.processEvents();
    const int dependencyWidth = dependencyHeader->sectionSize(0) + dependencyHeader->sectionSize(1);
    const double statusShare = dependencyWidth > 0
        ? static_cast<double>(dependencyHeader->sectionSize(1)) / dependencyWidth : 0.0;
    ok &= check(dependencyHeader->minimumSectionSize() >= 120 &&
                statusShare >= 0.20 && statusShare <= 0.30,
                "Dependency Status defaults near 25 percent with a sensible minimum");
    dependencyHeader->resizeSection(1, 180); application.processEvents();
    const int userStatusWidth = dependencyHeader->sectionSize(1);
    widget.refresh(); application.processEvents();
    ok &= check(dependencyHeader->sectionSize(1) == userStatusWidth,
                "Dependency Status user width survives Inventory refresh");
    MibLibraryWidget restoredWidget({temp.path()}, nullptr, &transport, callbacks);
    restoredWidget.resize(1100, 700); restoredWidget.show(); application.processEvents();
    auto *restoredDependencyTree = restoredWidget.findChild<QTreeWidget *>("MibDependencyTree");
    restoredWidget.findChild<QTabWidget *>("MibLibraryDetailsTabs")
        ->setCurrentWidget(restoredDependencyTree);
    application.processEvents();
    ok &= check(restoredDependencyTree->header()->sectionSize(1) == userStatusWidth,
                "Dependency Status width is restored across widget sessions");
    auto *tabs = widget.findChild<QTabWidget *>("MibLibraryTabs");
    ok &= check(tabs && tabs->count() == 2 && tabs->tabText(0) == "Inventory" &&
                tabs->tabText(1) == "MIB Profiles", "Inventory and Profiles are separate tabs");
    auto *profileSelector = widget.findChild<QComboBox *>("MibProfileEditorSelector");
    auto *checkDependencies = widget.findChild<QPushButton *>("MibProfileCheckDependencies");
    auto *checkSummary = widget.findChild<QLabel *>("MibProfileDependencyCheckSummary");
    ok &= check(profileSelector && checkDependencies && checkSummary &&
                checkSummary->text() == "Dependencies need checking",
                "profile starts with explicit stale dependency state");
    profileSelector->setCurrentIndex(1); checkDependencies->click(); application.processEvents();
    ok &= check(dependencyChecks == 1 && checkSummary->text().contains("1 dependencies") &&
                checkSummary->text().contains("0 unresolved"),
                "Check Dependencies invokes workflow and updates compact checked summary");

    QTemporaryDir customTemp;
    writeMib(customTemp.filePath("base.mib"),
             "SNMPv2-SMI DEFINITIONS ::= BEGIN\nIMPORTS x FROM NORTEL-nnSRNode-MIB;\nEND\n");
    writeMib(customTemp.filePath("nnsrnode.mib"),
             "NORTEL-nnSRNode-MIB DEFINITIONS ::= BEGIN\nEND\n");
    QList<MibModuleRecord> liveModules;
    MibModuleRecord baseModule; baseModule.name = "SNMPv2-SMI";
    baseModule.path = customTemp.filePath("base.mib"); liveModules.append(baseModule);
    MibProfileDependencyCheck liveCached;
    MibLibraryWidget::Callbacks synchronizationCallbacks;
    synchronizationCallbacks.localInventory = [&liveModules]() { return liveModules; };
    synchronizationCallbacks.checkDependencies = [&](const QString &, const QStringList &members,
                                                       bool, QString *) {
        MibModuleRecord learned; learned.name = "NORTEL-nnSRNode-MIB";
        learned.path = customTemp.filePath("nnsrnode.mib");
        liveModules.append(learned);
        liveCached.checkedUtc = QDateTime::currentDateTimeUtc();
        liveCached.effectiveModules = members;
        liveCached.dependencies = {"NORTEL-nnSRNode-MIB"};
        liveCached.unresolved.clear();
        return liveCached;
    };
    synchronizationCallbacks.cachedDependencies = [&](const QString &, const QString &) {
        return liveCached;
    };
    MibLibraryWidget synchronizationWidget({}, nullptr, &transport, synchronizationCallbacks);
    auto *syncSelector = synchronizationWidget.findChild<QComboBox *>("MibProfileEditorSelector");
    synchronizationWidget.selectProfile(MibProfileDefinitions::standardsId());
    auto *syncTable = synchronizationWidget.findChild<QTableWidget *>("MibLibraryTable");
    auto *syncAvailableFilter = synchronizationWidget.findChild<QLineEdit *>("MibProfileAvailableFilter");
    auto *syncMemberFilter = synchronizationWidget.findChild<QLineEdit *>("MibProfileMemberFilter");
    auto *syncAvailable = synchronizationWidget.findChild<QListWidget *>("MibProfileAvailableList");
    auto *syncRequired = synchronizationWidget.findChild<QListWidget *>("MibProfileRequiredDependencies");
    auto *syncIncludeStandards = synchronizationWidget.findChild<QCheckBox *>("MibProfileIncludeStandards");
    const bool standardsStateBefore = syncIncludeStandards->isChecked();
    auto containsListIdentity = [](QListWidget *list, const QString &identity) {
        return !list->findItems(identity, Qt::MatchExactly).isEmpty();
    };
    auto containsTableIdentity = [](QTableWidget *inventory, const QString &identity) {
        for (int row = 0; row < inventory->rowCount(); ++row)
            if (inventory->item(row, 1)->text() == identity) return true;
        return false;
    };
    ok &= check(!containsTableIdentity(syncTable, "NORTEL-nnSRNode-MIB") &&
                !containsListIdentity(syncAvailable, "NORTEL-nnSRNode-MIB"),
                "new custom identity is absent before dependency discovery");
    bool initiallyMissing = false;
    for (int row = 0; row < syncRequired->count(); ++row)
        initiallyMissing |= syncRequired->item(row)->text().contains("NORTEL-nnSRNode-MIB") &&
                            syncRequired->item(row)->text().contains("Missing");
    ok &= check(initiallyMissing, "required dependency begins with truthful missing state");
    syncAvailableFilter->setText("NORTEL"); syncMemberFilter->setText("BASE");
    synchronizationWidget.findChild<QPushButton *>("MibProfileCheckDependencies")->click();
    application.processEvents();
    ok &= check(containsTableIdentity(syncTable, "NORTEL-nnSRNode-MIB"),
                "Check Dependencies refreshes Inventory with learned identity immediately");
    ok &= check(containsListIdentity(syncAvailable, "NORTEL-nnSRNode-MIB"),
                "Check Dependencies refreshes Profile Available MIBs immediately");
    ok &= check(syncSelector->currentData().toString() == MibProfileDefinitions::standardsId() &&
                syncAvailableFilter->text() == "NORTEL" && syncMemberFilter->text() == "BASE" &&
                syncIncludeStandards->isChecked() == standardsStateBefore,
                "profile identity, filters, and standards-base state survive refresh");
    ok &= check(synchronizationWidget.profileService()->find(MibProfileDefinitions::standardsId())
                    ->explicitModules == MibProfileDefinitions::standardsModules(),
                "explicit profile membership survives synchronization refresh");
    int learnedRows = 0; bool staleMissing = false;
    for (int row = 0; row < syncRequired->count(); ++row) {
        const QString text = syncRequired->item(row)->text();
        if (text.contains("NORTEL-nnSRNode-MIB")) {
            ++learnedRows; staleMissing |= text.contains("Missing");
        }
    }
    ok &= check(learnedRows == 1 && !staleMissing,
                "required dependencies refresh to one available row without stale missing duplicate");
    return ok ? 0 : 1;
}
