#ifndef MIBLIBRARYWIDGET_H
#define MIBLIBRARYWIDGET_H

#include "miblibrary.h"
#include <QWidget>

class MibDownloadTransport;
class MibModule;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTreeWidget;

class MibLibraryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MibLibraryWidget(MibModule *modules, const QStringList &bundledPaths,
                              QWidget *parent = nullptr,
                              MibDownloadTransport *transport = nullptr);
    void refresh();
private slots:
    void applyFilter();
    void selectVisible();
    void clearSelection();
    void resolveSelected();
    void downloadSelected(bool loadAfter = false);
    void downloadNext();
    void refreshCatalog();
private:
    void loadCachedCatalog();
    QString catalogCachePath() const;
    QStringList selectedModules() const;
    MibModule *modules;
    QStringList bundledPaths;
    MibLibraryService library;
    MibCatalog catalog;
    MibDownloadTransport *transport;
    QList<MibLibraryRecord> records;
    QStringList downloadQueue;
    QStringList requestedTargets;
    bool loadAfterDownload = false;
    bool catalogRefreshInProgress = false;
    QString sourceStatusText;
    MibDownloadSessionState sessionState;
    QLineEdit *search;
    QComboBox *sourceFilter;
    QComboBox *statusFilter;
    QTableWidget *table;
    QTreeWidget *dependencies;
    QLabel *status;
    QPushButton *downloadButton;
    QPushButton *refreshButton;
    QPushButton *cancelButton;
};

#endif
