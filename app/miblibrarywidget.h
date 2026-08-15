#ifndef MIBLIBRARYWIDGET_H
#define MIBLIBRARYWIDGET_H

#include "miblibrary.h"
#include "mibprofile.h"
#include "mibrecords.h"
#include "mibdependencyindex.h"
#include <QWidget>
#include <functional>

class MibDownloadTransport;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTextEdit;
class QTreeWidget;

class MibLibraryWidget : public QWidget
{
    Q_OBJECT
public:
    struct DependencySummary {
        bool stale = true;
        int knownModules = 0;
        int relationships = 0;
        int unresolved = 0;
        int ambiguous = 0;
        QDateTime lastCheckedUtc;
    };
    struct Callbacks {
        std::function<bool(const QString &, QString *)> validate;
        std::function<void(const QStringList &, bool)> downloadsCompleted;
        std::function<MibModuleRecord(const QString &, const QString &)> metadata;
        std::function<QList<MibModuleRecord>()> localInventory;
        std::function<DependencySummary()> libraryDependencySummary;
        std::function<MibProfileDependencyCheck(const QString &, const QString &)> cachedDependencies;
    };
    explicit MibLibraryWidget(const QStringList &bundledPaths,
                              QWidget *parent = nullptr,
                              MibDownloadTransport *transport = nullptr,
                              Callbacks callbacks = {});
    void refresh();
    MibProfileService *profileService() { return &profiles; }
    QStringList availableModuleNames() const;
    MibCatalog dependencyCatalog() const;
    void selectProfile(const QString &id);
signals:
    void openModuleRequested(const QString &path, bool readOnly);
    void profilesChanged();
    void profileSelectionChanged(const QString &id, const QStringList &effectiveModules);
private slots:
    void applyFilter();
    void resolveSelected();
    void downloadSelected(bool loadAfter = false);
    void downloadNext();
    void refreshCatalog();
    void showCurrentInfo();
    void openCurrentModule();
    void profileChanged();
    void createProfile();
    void duplicateProfile();
    void renameProfile();
    void deleteProfile();
    void addProfileMembers();
    void removeProfileMembers();
    void downloadProfileMissing();
private:
    void loadCachedCatalog();
    QString catalogCachePath() const;
    QStringList selectedModules() const;
    void setFilteredSelection(bool selected);
    void updateSelectionUi();
    void setOperationActive(bool active);
    QString originText(const MibLibraryRecord &record) const;
    void refreshProfiles(const QString &selectId = {});
    void refreshProfileLists();
    void saveCurrentProfile();
    Callbacks callbacks;
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
    MibProfileService profiles;
    QLineEdit *search;
    QComboBox *sourceFilter;
    QComboBox *statusFilter;
    QTableWidget *table;
    QTreeWidget *dependencies;
    QLabel *status;
    QTabWidget *detailsTabs;
    QMap<QString, QLabel *> infoValues;
    QMap<QString, QWidget *> infoLabels;
    QTextEdit *contactInfo;
    QLabel *selectedCount;
    QPushButton *openEditorButton;
    QComboBox *profileCombo;
    QLineEdit *availableSearch;
    QLineEdit *memberSearch;
    QListWidget *availableList;
    QListWidget *memberList;
    QListWidget *requiredList;
    QCheckBox *includeStandards;
    QPushButton *renameProfileButton;
    QPushButton *deleteProfileButton;
    QPushButton *addMemberButton;
    QPushButton *removeMemberButton;
    QPushButton *downloadMissingButton;
    QLabel *dependencyCheckSummary;
    QPushButton *downloadButton;
    QPushButton *installDependenciesButton;
    QPushButton *refreshButton;
    QPushButton *cancelButton;
};

#endif
