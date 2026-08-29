/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <memory>
#include <stdio.h>
#include <string.h>
#include <qfileinfo.h>
#include <qdir.h>
#include <qmessagebox.h> 
#include <qtextstream.h>
#include <QStringBuilder>
#include <QElapsedTimer>
#include <algorithm>

#include "mibmodule.h"
#include "mibenvironmentextractor.h"
#include "mibenvironmentregistry.h"
#include "mibcollection.h"
#include "miblibrary.h"
#include "diagnosticlogger.h"
#include "agent.h"
#include "preferences.h"
#include "preferredmibresolver.h"
#include "mibservice.h"
#include "mibservice_internal.h"
#include "mibengine.h"
#include "mibprofile.h"
#include "mibcandidatefilter.h"

LoadedMibModule::LoadedMibModule(MibModuleRecord moduleRecord)
    : record(std::move(moduleRecord))
{
    name = record.name;
    path = record.path;
}

void LoadedMibModule::PrintProperties(QString& text)
{
    // Create a table and add elements ...
    text = MibModule::tr("<table border=\"1\" cellpadding=\"0\" cellspacing=\"0\" align=\"left\">");
    
    // Add the name
    text += MibModule::tr("<tr><td><b>Name:</b></td><td><font color=#009000><b>%1</b></font></td>")
            .arg(record.name);
    
    // Add last revision
    if(record.lastRevision.isValid())
        text += MibModule::tr("<tr><td><b>Last revision:</b></td><td>%1</td></tr>")
                .arg(record.lastRevision.toString(Qt::ISODate));
    
    // Add the description
    text += MibModule::tr("<tr><td><b>Description:</b></td><td><font face=fixed color=blue>");
    text += Qt::convertFromPlainText (record.description);
    text += MibModule::tr("</font></td></tr>");
    
    // Add root node name
    if (!record.rootName.isEmpty())
        text += MibModule::tr("<tr><td><b>Root node:</b></td><td>%1</td>")
                .arg(record.rootName);
    
    // Add required modules
    text += MibModule::tr("<tr><td><b>Requires:</b></td><td><font color=red>");
    text += record.imports.join(QStringLiteral("<br>"));
    text += MibModule::tr("</font></td></tr>");
    
    // Add organization
    text += MibModule::tr("<tr><td><b>Organization:</b></td><td>");
    text += Qt::convertFromPlainText (record.organization);
    text += MibModule::tr("</td></tr>");
    
    // Add contact info
    text += MibModule::tr("<tr><td><b>Contact Info:</b></td><td><font face=fixed>");
    text += Qt::convertFromPlainText (record.contactInfo);
    text += MibModule::tr("</font></td></tr>");
             
    text += QString("</table>");
}

QString LoadedMibModule::GetMibLanguage() const
{
    return record.language;
}

static MibModule *CurrentModuleObject = NULL;

MibModule::MibModule(Snmpb *snmpb)
    : s(snmpb)
    , Policy(MIBLOAD_DEFAULT)
{
    QElapsedTimer phase; phase.start();
    DiagnosticLogger::log("MIB", tr("dependency index path=\"%1\"").arg(
        QDir::toNativeSeparators(dependencyIndex.path())));
    QString dependencyIndexError;
    const bool dependencyIndexLoaded = dependencyIndex.load(&dependencyIndexError);
    DiagnosticLogger::log("MIB", tr("dependency index load status=%1 records=%2 generation=%3%4")
        .arg(MibDependencyIndexLoadStatusText(dependencyIndex.loadStatus()))
        .arg(dependencyIndex.files().size()).arg(dependencyIndex.generation())
        .arg(dependencyIndex.loadDiagnostic().isEmpty() ? QString()
            : tr(" reason=\"%1\"").arg(dependencyIndex.loadDiagnostic())));
    DiagnosticLogger::log("Startup", tr("dependency index load complete elapsed_ms=%1 records=%2")
        .arg(phase.elapsed()).arg(dependencyIndex.files().size()));
    Q_UNUSED(dependencyIndexLoaded);
    // Must be connected before call to InitLib ...
    connect(this, SIGNAL ( LogError(QString) ),
            s->MainUI()->LogOutput, SLOT ( append (QString) ));

    CurrentModuleObject = this;
    phase.restart();
    InitLib(0);
    DiagnosticLogger::log("Startup", tr("libsmi initialization complete elapsed_ms=%1").arg(phase.elapsed()));
    phase.restart(); ReadMibPaths(); RebuildCandidateList();
    DiagnosticLogger::log("Startup", tr("MIB candidate enumeration complete elapsed_ms=%1 candidates=%2 stale=%3")
        .arg(phase.elapsed()).arg(Total.size()).arg(dependencyIndexStale ? QStringLiteral("yes") : QStringLiteral("no")));
    phase.restart(); RegenerateSmiConf();
    RebuildLoadedList(); RebuildUnloadedList();
    DiagnosticLogger::log("Startup", tr("requested MIB load complete elapsed_ms=%1 requested=%2 loaded=%3")
        .arg(phase.elapsed()).arg(0).arg(Loaded.size()));

    // Connect some signals
    connect( s->MainUI()->UnloadedModules, 
             SIGNAL(itemDoubleClicked ( QTreeWidgetItem *, int )),
             this, SLOT( AddModule() ) );
    connect( s->MainUI()->LoadedModules, 
             SIGNAL(itemDoubleClicked ( QTreeWidgetItem *, int )),
             this, SLOT( RemoveModule() ) );
    connect( s->MainUI()->LoadedModules, SIGNAL(itemSelectionChanged ()),
             this, SLOT( ShowModuleInfo() ) );
    connect( this, SIGNAL(ModuleProperties(const QString&)),
             (QObject*)s->MainUI()->ModuleInfo, 
             SLOT(setHtml(const QString&)) );
    connect( s->MainUI()->ModuleAdd, 
             SIGNAL( clicked() ), this, SLOT( AddModule() ));
    connect( s->MainUI()->ModuleDelete, 
             SIGNAL( clicked() ), this, SLOT( RemoveModule() ));
    connect( this, SIGNAL( StopAgentTimer() ), 
             s->AgentObj(), SLOT( StopTimer() ));
    environmentManager=std::make_unique<MibEnvironmentManager>(
        [this](const MibEffectivePlan &plan){return BuildEnvironment(plan);},this,
        MibEnvironmentCache::DefaultByteBudget,QStringLiteral("patched-libsmi-%1/engine-policy-1")
            .arg(MibEngine::instance().libraryVersion()));
    connect(environmentManager.get(),&MibEnvironmentManager::buildStarted,this,
        [this](quint64,const QString &name){emit profileRuntimeBuildStarted(name);});
    connect(environmentManager.get(),&MibEnvironmentManager::buildCompleted,this,
        [this](quint64,const QString &profileId,MibEnvironmentPtr environment,
               QStringList loadedModules,bool cacheHit,bool partial){
            const auto plan=requestedPlans.value(profileId);
            currentEnvironment=environment;activeProfilePlan=plan;hasActiveProfilePlan=true;
            s->MibLoaderObj()->SetEnvironment(environment,loadedModules);
            emit profileRuntimeReady(profileId,plan,environment,loadedModules,cacheHit,partial);
        });
    connect(environmentManager.get(),&MibEnvironmentManager::buildFailed,this,
        [this](quint64,const QString &profileId,const QString &error){emit profileRuntimeFailed(profileId,error);});
}

void MibModule::ShowModuleInfo()
{
    QTreeWidgetItem *item;
    QList<QTreeWidgetItem *> item_list = 
                             s->MainUI()->LoadedModules->selectedItems();

    if ((item_list.count() == 1) && ((item = item_list.first()) != 0))
    {
        QString text;
        LoadedMibModule *lmodule;
        for(int i = 0; i < Loaded.count(); i++)
        { 
            lmodule = &Loaded[i];
            if (lmodule->name == item->text(0))
            {
                lmodule->PrintProperties(text);
                emit ModuleProperties(text);
                break;
            }
        }
    }    
}

// For sorting total module list based on name
bool compareModule(QStringList s1, QStringList s2)
{
    return s1[0] < s2[0];
}

static void NormalErrorHdlr(char *path, int line, int severity, 
                            char *msg, char *tag)
{
    (void)line; (void)tag;

    if (severity <= 1)
        CurrentModuleObject->SendLogError(MibModule::tr("ERROR(%1) loading %2: %3")
                                          .arg(severity).arg(path).arg(msg));
}

static bool MibFilenameFilter(const QString& filename)
{
    // This is all futile. FIXME
    // To machine code, file extension says literally nothing about its content.
    // To a human, it *might* vaguely identify the content-type, possibly fooling the human.
    // But code like this function should not exist.
    // Instead, try to load every readable file as MIB, and back off gracefully.

    return MibCandidateFilter::accepts(filename);
}

void MibModule::RebuildTotalList()
{
    RebuildCandidateList();
}

MibModule::~MibModule()
{
    environmentManager.reset();
    CurrentModuleObject=nullptr;
    MibEngine::instance().shutdown();
}

#if 0 // Historical compile-all implementation retained only as migration reference.
void MibModule::RebuildTotalListLegacyCompileAll()
{
    DiagnosticLogger::log("MIB", "MIB search-path resolution and MIB/PIB loading begin");
    /* Enable error reporting */
    // Inventory metadata is user-visible; retain DESCRIPTION, ORGANIZATION,
    // CONTACT-INFO, and REFERENCE text while scanning configured modules.
    smiSetFlags((smiGetFlags() | SMI_FLAG_ERRORS) & ~SMI_FLAG_NODESCR);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(3);

    std::unique_ptr<char, decltype(&std::free)>
        smipath{ smiGetPath(), &std::free };
    QStringList
        smipaths = QString(smipath.get()).split(SMI_PATH_SEPARATOR);
   
    Total.clear();
    KnownModuleNames.clear();
    AvailableRecords.clear();
    QStringList errored_files;
    for (int i = 0; i < smipaths.size(); ++i)
    {
        QDir d(smipaths[i], "", QDir::Unsorted, QDir::Files | QDir::Readable);
        QStringList list = d.entryList();
        for (QStringListIterator it(list); it.hasNext(); )
        {
            const QString& fn = it.next();
            if (MibFilenameFilter(fn))
            {
                // Load each module and build a list of possible root oids
                // This is used for module auto-loading on mib walk
                QStringList module;

                // If a module has a fatal error, ignore it
                ErrorWhileLoading = false;
                char *mod = smiLoadModule(fn.toLatin1());
                SmiModule *smiModule = mod?smiGetModule(mod):NULL;
                if (ErrorWhileLoading == true)
                {
                    errored_files << d.absoluteFilePath(fn);
                    continue;
                }

                module += QFileInfo(fn.toLatin1()).fileName();

                if (smiModule)
                {
                    const MibService service;
                    for (const MibModuleRecord &record :
                         service.modulesFromFile(d.absoluteFilePath(fn))) {
                        if (!KnownModuleNames.contains(record.name))
                            KnownModuleNames.append(record.name);
                        const auto duplicate = std::find_if(AvailableRecords.cbegin(),
                            AvailableRecords.cend(), [&record](const MibModuleRecord &item) {
                                return item.name == record.name && item.path == record.path;
                            });
                        if (duplicate == AvailableRecords.cend()) AvailableRecords.append(record);
                    }
                    SmiNode *node = smiGetModuleIdentityNode(smiModule);
                    if (node)
                        module += smiRenderOID(node->oidlen, 
                                               node->oid, SMI_RENDER_NUMERIC);

                    for(node = smiGetFirstNode(smiModule, SMI_NODEKIND_NODE); 
                        node; node = smiGetNextNode(node, SMI_NODEKIND_NODE))
                    {
                        if (node->decl == SMI_DECL_VALUEASSIGNMENT)
                            module += smiRenderOID(node->oidlen, 
                                                   node->oid, SMI_RENDER_NUMERIC);
                    }
                }
                Total.append(module);
            }
        }
    }

    /* warn if there're MIBs which failed to load */
    if (!errored_files.empty())
    {
        std::sort(errored_files.begin(), errored_files.end());

        QMessageBox::warning (s->MainUI()->MIBTree, tr("MIB loading errors"),
                              tr("%n MIB files failed to load. See Log for details.",
                                 nullptr, errored_files.size()),
                              QMessageBox::Ok, Qt::NoButton);
    }

    std::sort(Total.begin(), Total.end(), compareModule);
}
#endif

void MibModule::RebuildCandidateList()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("candidate-list"));
    std::unique_ptr<char, decltype(&std::free)> rawPath{smiGetPath(), std::free};
    const QStringList paths = rawPath ? QString::fromLocal8Bit(rawPath.get()).split(
        SMI_PATH_SEPARATOR, Qt::SkipEmptyParts) : QStringList{};
    const MibDependencyInspection inspection = dependencyIndex.inspect(paths);
    dependencyIndexStale = inspection.stale(); Total.clear(); KnownModuleNames.clear(); AvailableRecords.clear();
    QSet<QString> candidatePaths;
    for (const MibPhysicalCandidate &candidate : inspection.candidates) {
        Total.append(QStringList{candidate.filename});
        candidatePaths.insert(QFileInfo(candidate.canonicalPath).canonicalFilePath().toLower());
    }
    for (const MibDependencyFileRecord &file : dependencyIndex.files()) {
        const QString canonicalPath = QFileInfo(file.canonicalPath).canonicalFilePath().toLower();
        if (!candidatePaths.contains(canonicalPath) || file.checkState != QStringLiteral("verified")) continue;
        for (auto it = file.importsByModule.begin(); it != file.importsByModule.end(); ++it) {
            MibModuleRecord record; record.name = it.key(); record.path = file.canonicalPath;
            record.imports = it.value(); AvailableRecords.append(record);
            if (!KnownModuleNames.contains(record.name)) KnownModuleNames.append(record.name);
        }
    }
    std::sort(Total.begin(), Total.end(), compareModule);
}

MibProfileDependencyCheck MibModule::CheckProfileDependencies(
    const QString &profileId, const QStringList &explicitModules,
    bool includeStandardBase, QString *error)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("dependency-check"));
    QElapsedTimer totalTimer;
    totalTimer.start();
    const MibDependencyScanResult scan = RefreshDependencyIndex(error);
    QStringList roots = explicitModules;
    if (includeStandardBase) roots.append(MibProfileDefinitions::standardsModules());
    roots.removeDuplicates(); roots.sort(Qt::CaseInsensitive);
    const QString signature = MibDependencyIndex::profileSignature(explicitModules, includeStandardBase);
    if (!scan.changed && dependencyIndex.profileCheckCurrent(profileId, signature)) {
        const MibProfileDependencyCheck cached = dependencyIndex.profileCheck(profileId);
        DiagnosticLogger::log("MIB", tr("Dependency check profile=%1 files-scanned=0 reused=%2 cache=reused inspection-ms=%3 graph-ms=0 semantic-ms=0 total-ms=%4")
            .arg(profileId).arg(scan.reused).arg(scan.elapsedMsecs).arg(totalTimer.elapsed()));
        emit inventoryChanged();
        return cached;
    }
    QElapsedTimer semanticTimer;
    semanticTimer.start();

    QSet<QString> loading;
    const bool libraryWide = profileId == QStringLiteral("mib-library");
    std::function<MibDependencyLoadAttempt(const QString &, const QString &)> loadIndexed;
    loadIndexed = [this, libraryWide, &loading, &loadIndexed](const QString &path, const QString &expected) {
        MibDependencyLoadAttempt attempt;
        if (libraryWide && dependencyIndex.semanticallyVerified(expected)) {
            attempt.success = true; attempt.loadedModuleNames = {expected}; return attempt;
        }
        if (smiIsLoaded(expected.toLocal8Bit().constData())) {
            attempt.success = true; attempt.loadedModuleNames = {expected}; return attempt;
        }
        if (loading.contains(expected)) { attempt.diagnostic = tr("Circular provider load"); return attempt; }
        loading.insert(expected);
        for (const QString &dependency : dependencyIndex.imports(expected)) {
            if (smiIsLoaded(dependency.toLocal8Bit().constData())) continue;
            const auto provider = dependencyIndex.provider(dependency);
            if (provider.status == MibProviderStatus::Found) loadIndexed(provider.path, dependency);
        }
        ErrorWhileLoading = false;
        char *canonicalName = smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
        const QList<MibModuleRecord> fromFile = MibService().modulesFromFile(path);
        for (const MibModuleRecord &record : fromFile) attempt.loadedModuleNames.append(record.name);
        attempt.success = canonicalName && attempt.loadedModuleNames.contains(expected) && !ErrorWhileLoading;
        if (!attempt.success) attempt.diagnostic = ErrorWhileLoading
            ? tr("libsmi reported a parser/semantic error")
            : tr("Provider did not produce the requested module identity");
        loading.remove(expected); return attempt;
    };
    const MibDependencyCheckResult result = MibBoundedDependencyLoader().load(roots, dependencyIndex, loadIndexed);
    MibProfileDependencyCheck check;
    check.profileSignature = signature;
    check.indexGeneration = dependencyIndex.generation(); check.effectiveModules = result.loaded;
    check.dependencies = result.dependencies; check.checkedUtc = QDateTime::currentDateTimeUtc();
    check.elapsedMsecs = scan.elapsedMsecs + result.elapsedMsecs;
    for (const MibDependencyFailure &failure : result.failures) {
        check.unresolved.append(failure.moduleName);
        check.failureSummaries.append(tr("%1: %2%3").arg(failure.moduleName,
            MibDependencyFailureText(failure.kind), failure.detail.isEmpty() ? QString() : tr(" — %1").arg(failure.detail)));
    }
    for (const QString &module : result.loaded) dependencyIndex.recordVerification(module, true);
    for (const MibDependencyFailure &failure : result.failures)
        dependencyIndex.recordVerification(failure.moduleName, false, failure.detail);
    dependencyIndex.setProfileCheck(profileId, check);
    if (!dependencyIndex.save(error)) {
        DiagnosticLogger::log("MIB", tr("dependency index save failed path=\"%1\" reason=\"%2\"")
            .arg(QDir::toNativeSeparators(dependencyIndex.path()), error ? *error : QString()));
        return check;
    }
    DiagnosticLogger::log("MIB", tr("dependency index save records=%1 generation=%2 path=\"%3\"")
        .arg(dependencyIndex.files().size()).arg(dependencyIndex.generation())
        .arg(QDir::toNativeSeparators(dependencyIndex.path())));
    // Verification changes which indexed identities are projected into the
    // current-session Inventory/Profile models. Rebuild that projection now;
    // otherwise newly learned aliases only appear after process restart.
    RebuildCandidateList();
    DiagnosticLogger::log("MIB", tr("Dependency check profile=%1 files-scanned=%2 reused=%3 deleted=%4 modules=%5 unresolved=%6 elapsed-ms=%7")
        .arg(profileId).arg(scan.scanned).arg(scan.reused).arg(scan.deleted)
        .arg(check.effectiveModules.size()).arg(check.unresolved.size()).arg(check.elapsedMsecs));
    DiagnosticLogger::log("MIB", tr("Dependency check phases profile=%1 inspection-ms=%2 semantic-ms=%3 total-ms=%4 cache=updated")
        .arg(profileId).arg(scan.elapsedMsecs).arg(semanticTimer.elapsed()).arg(totalTimer.elapsed()));
    if (!libraryWide && !result.loaded.isEmpty()) {
        s->MibLoaderObj()->EnsureLoaded(result.loaded); RebuildLoadedList(); RebuildUnloadedList();
    }
    emit inventoryChanged();
    return check;
}

MibDependencyScanResult MibModule::RefreshDependencyIndex(QString *error)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("dependency-refresh"));
    ReadMibPaths(); std::unique_ptr<char, decltype(&std::free)> rawPath{smiGetPath(), std::free};
    const QStringList paths = rawPath ? QString::fromLocal8Bit(rawPath.get()).split(
        SMI_PATH_SEPARATOR, Qt::SkipEmptyParts) : QStringList{};
    const MibDependencyScanResult result = dependencyIndex.update(paths, error);
    RebuildCandidateList();
    return result;
}

MibProfileDependencyCheck MibModule::CachedProfileDependencies(
    const QString &profileId, const QString &signature) const
{
    return !dependencyIndexStale && dependencyIndex.profileCheckCurrent(profileId, signature)
        ? dependencyIndex.profileCheck(profileId) : MibProfileDependencyCheck{};
}

QStringList MibModule::AvailableModuleNames() const
{
    QStringList result = KnownModuleNames;
    for (const LoadedMibModule &module : Loaded)
        if (!result.contains(module.name)) result.append(module.name);
    result.sort(Qt::CaseInsensitive);
    return result;
}

QStringList MibModule::LoadedModuleNames() const
{
    QStringList result;
    for (const LoadedMibModule &module : Loaded) result.append(module.name);
    return result;
}

MibModuleRecord MibModule::ModuleMetadata(const QString &moduleName, const QString &localPath)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("module-metadata"));
    SmiModule *module = smiGetModule(moduleName.toLocal8Bit().constData());
    if (!module && !localPath.isEmpty()) {
        char *loaded = smiLoadModule(QFileInfo(localPath).fileName().toLocal8Bit().constData());
        module = loaded ? smiGetModule(loaded) : nullptr;
    }
    return SnapshotMibModule(module);
}

QStringList MibModule::LoadPreferredModules(const QStringList &modules)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("load-preferred"));
    PreferredMibResolution resolution;
    QStringList loadedNow; QSet<QString> loading;
    std::function<bool(const QString &)> loadIdentity;
    loadIdentity = [this, &loading, &loadIdentity, &loadedNow](const QString &identity) {
        if (smiIsLoaded(identity.toLocal8Bit().constData())) { if (!loadedNow.contains(identity)) loadedNow.append(identity); return true; }
        if (loading.contains(identity)) return false; loading.insert(identity);
        const auto provider = dependencyIndex.provider(identity);
        QString actualIdentity;
        if (provider.status == MibProviderStatus::Found) {
            for (const QString &dependency : dependencyIndex.imports(identity)) loadIdentity(dependency);
            ErrorWhileLoading = false;
            char *actual = smiLoadModule(QDir::toNativeSeparators(provider.path).toLocal8Bit().constData());
            if (actual) actualIdentity = QString::fromLocal8Bit(actual);
        } else {
            ErrorWhileLoading = false; char *actual = smiLoadModule(identity.toLocal8Bit().constData());
            if (actual) actualIdentity = QString::fromLocal8Bit(actual);
        }
        const bool success = !ErrorWhileLoading && !actualIdentity.isEmpty() &&
            (provider.status != MibProviderStatus::Found || smiIsLoaded(identity.toLocal8Bit().constData()));
        loading.remove(identity); if (success && !loadedNow.contains(actualIdentity)) loadedNow.append(actualIdentity); return success;
    };
    for (const QString &module : modules) {
        if (smiIsLoaded(module.toLocal8Bit().constData())) resolution.alreadyLoaded.append(module);
        else if (!loadIdentity(module)) resolution.unavailable.append(module);
        else resolution.toLoad.append(module);
    }
    if (!loadedNow.isEmpty())
    {
        s->MibLoaderObj()->EnsureLoaded(loadedNow);
        RebuildLoadedList();
        RebuildUnloadedList();
        s->TabSelected();
    }
    for (const QString &name : resolution.unavailable)
        emit LogError(tr("Preferred MIB module '%1' is unavailable").arg(name));
    return resolution.unavailable;
}

MibEffectivePlan MibModule::BuildEffectivePlan(const MibProfileRecord &profile) const
{
    return MibEffectivePlanResolver().resolve(profile, dependencyIndex);
}

QStringList MibModule::LoadEffectivePlan(const MibEffectivePlan &plan)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("load-effective-plan"));
    const auto load = [this](const QString &path, const QString &identity) {
            MibDependencyLoadAttempt attempt;
            SmiModule *loaded = smiGetModule(identity.toLocal8Bit().constData());
            if (!loaded) {
                ErrorWhileLoading = false;
                smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
                loaded = smiGetModule(identity.toLocal8Bit().constData());
            } else ErrorWhileLoading = false;
            const QString actualPath = loaded && loaded->path
                ? QFileInfo(QString::fromLocal8Bit(loaded->path)).canonicalFilePath() : QString();
            const QString plannedPath = QFileInfo(path).canonicalFilePath();
            const bool providerMatches = !actualPath.isEmpty() &&
                actualPath.compare(plannedPath, Qt::CaseInsensitive) == 0;
            if (!ErrorWhileLoading && loaded && providerMatches) {
                attempt.success = true; attempt.loadedModuleNames = {identity};
            } else if (loaded && !providerMatches) {
                attempt.diagnostic = tr("planned=%1 actual=%2").arg(plannedPath, actualPath);
                DiagnosticLogger::log("MIB", tr("Effective Plan provider mismatch module=%1 %2")
                    .arg(identity, attempt.diagnostic));
            }
            return attempt;
    };
    const MibDependencyCheckResult result = MibBoundedDependencyLoader().load(plan, load);
    QStringList unavailable;
    for (const MibDependencyFailure &failure : result.failures)
        unavailable.append(failure.moduleName);
    unavailable.removeDuplicates();
    unavailable.sort(Qt::CaseSensitive);
    for (const QString &identity : std::as_const(unavailable))
        emit LogError(tr("Effective Plan MIB module '%1' is unavailable").arg(identity));
    return unavailable;
}

// Attempts to identify and load a mib module that resolves a specific oid
//
// Returns the mib module's filename if there is a match, otherwise
// returns an empty string
QString MibModule::LoadBestModule(QString oid)
{
    // If automatic loading is disabled, return
    if ((s->PreferencesObj()->GetAutomaticLoading() == 3) || 
        (Policy == MIBLOAD_NONE))
        return "";

    QString best_file = "";
    QString best_oid = "";

    // Loop though all mibs
    for (auto& mib: Total)
    {
        // Loop through all possible root oids for each mib
        for (auto& root_oid: mib)
        {
            // If we have a possible match better than the best so far ...
            if (root_oid != "" && oid.startsWith(root_oid) &&
                root_oid.size() > best_oid.size())
            {
                // ...and it is not loaded ...
                if (Unloaded.contains(mib[0]))
                {
                    // ... note it as best match so far and continue.
                    best_file = mib[0];
                    best_oid = root_oid;
                }
                break;
            }
        }
    }

    // We have a match, try to load it
    if (best_file != "")
    {
        // If automatic loading prompt is enabled and load policy is not set to all
        if ((s->PreferencesObj()->GetAutomaticLoading() == 2) &&
            (Policy != MIBLOAD_ALL))
        {
            emit StopAgentTimer();
            int ret = QMessageBox::question (
                        s->MainUI()->MIBTree,
                        tr("MIB Navigator automatic MIB loading"),
                        tr("Unknown OID %1\nAttempt to load MIB module where this OID is defined?").arg(oid),
                        QMessageBox::Yes | QMessageBox::No | 
                        QMessageBox::YesToAll | QMessageBox::NoToAll,
                        QMessageBox::Yes
                      );

            switch (ret)
            {
                case QMessageBox::NoToAll:
                    Policy = MIBLOAD_NONE;
                    // fallthrough
                case QMessageBox::No:
                    return "";
                case QMessageBox::YesToAll:
                    Policy = MIBLOAD_ALL;
                    break;
                case QMessageBox::Yes:
                default:
                    break;
            }
        }

        // Load the module
        DiagnosticLogger::log("MIB", tr("Legacy automatic preload ignored after Profile migration candidate=%1").arg(best_file));
    }

    return best_file;
}

bool lessThanLoadedMibModule(const LoadedMibModule &lm1,
                             const LoadedMibModule &lm2)
{
    return lm1.name < lm2.name;
}

void MibModule::RebuildLoadedList()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("loaded-list"));
    Loaded.clear();
    s->MainUI()->LoadedModules->clear();

    for (SmiModule *mod = smiGetFirstModule();
         mod;
         mod = smiGetNextModule(mod) )
    {
        LoadedMibModule lmodule(SnapshotMibModule(mod));
        Loaded.append(lmodule);

        QString required = hasActiveProfilePlan && activeProfilePlan.explicitModules.contains(lmodule.name)
            ? tr("yes") : tr("no");

        QStringList columns;
        columns << lmodule.name.toUtf8().data()
                << required
                << lmodule.GetMibLanguage()
                << lmodule.path;
        new QTreeWidgetItem(s->MainUI()->LoadedModules, columns);
    }
    
    std::sort(Loaded.begin(), Loaded.end(), lessThanLoadedMibModule);
}

void MibModule::RebuildUnloadedList()
{
    Unloaded.clear();
    s->MainUI()->UnloadedModules->clear();
    
    for(int i = 0; i < Total.count(); ++i)
    {
        QString current = Total[i][0];

        bool found = false;
        const QStringList declarations = MibDeclaredIdentitiesForCandidate(current, dependencyIndex);
        for(int j = 0; j < Loaded.count(); j++)
        { 
            if (QFileInfo(Loaded[j].path).fileName().compare(current, Qt::CaseInsensitive) == 0 ||
                declarations.contains(Loaded[j].name))
            {
                found = true;
                break;
            }
        }
        if (!found) {
            Unloaded.append(current);
            new QTreeWidgetItem(s->MainUI()->UnloadedModules, 
                                QStringList(current));
        }
    }
}

void MibModule::AddModule()
{
    DiagnosticLogger::log("MIB", QStringLiteral("Legacy module editor ignored; use a Custom Profile"));
}

void MibModule::RemoveModule()
{
    DiagnosticLogger::log("MIB", QStringLiteral("Legacy module editor ignored; use a Custom Profile"));
}

void MibModule::ReadMibPaths()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("read-search-paths"));
    // read in MIB path from config
    QStringList paths;
    QSettings settings;
    int size = settings.beginReadArray("mibpaths");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        paths << settings.value("dir").toString();
    }

    QSettings rootSettings;
    const MibCollection collection(MibCollection::configuredRoot(rootSettings));
    for (const QString &path : collection.runtimeSearchPaths())
        if (!paths.contains(path)) paths.append(path);

    smiSetPath(paths.join(SMI_PATH_SEPARATOR).toLocal8Bit().data());
}

bool MibModule::ReconstructRuntime(const MibEffectivePlan &plan, QString *error)
{
    QElapsedTimer totalTimer; totalTimer.start();
    RegenerateSmiConf();
    InitLib(1);
    Loaded.clear();
    const QStringList unavailable = LoadEffectivePlan(plan);
    RebuildLoadedList();
    RebuildUnloadedList();
    QStringList runtimeModules = LoadedModuleNames();
    QStringList missing;
    for (const QString &identity : plan.effectiveModules)
        if (!smiIsLoaded(identity.toLocal8Bit().constData())) missing.append(identity);
    missing.append(unavailable);
    missing.removeDuplicates();
    MibEnvironmentPtr extracted = MibEnvironmentExtractor().extract(plan, missing);
    const MibEnvironmentTelemetry telemetry = extracted->telemetry();
    DiagnosticLogger::log("MIB", tr("Environment extraction ms=%1 plan=%2 status=%3 modules=%4 nodes=%5 types=%6 utf16-chars=%7 approximate-bytes=%8 findings=%9")
        .arg(telemetry.extractionMilliseconds).arg(plan.sha256)
        .arg(extracted->status() == MibEnvironmentStatus::Complete ? QStringLiteral("complete") : QStringLiteral("partial"))
        .arg(telemetry.moduleCount).arg(telemetry.nodeCount).arg(telemetry.typeCount)
        .arg(telemetry.ownedUtf16Characters).arg(telemetry.approximateOwnedBytes)
        .arg(extracted->findings().size()));
    DiagnosticLogger::log("MIB", tr("Effective Plan runtime reconstruction total-ms=%1 plan=%2 requested=%3 loaded=%4 missing=%5")
        .arg(totalTimer.elapsed()).arg(plan.sha256).arg(plan.effectiveModules.size())
        .arg(runtimeModules.size()).arg(missing.size()));
    if (!MibEnvironmentRegistry::isUsableMaterialization(extracted)) {
        if (error) *error = tr("No planned MIB modules could be materialized");
        return false;
    }
    s->MibLoaderObj()->SetEnvironment(extracted, runtimeModules);
    currentEnvironment = extracted;
    MibEnvironmentRegistry::publishMaterialization(std::move(extracted));
    if (!missing.isEmpty() && error)
        *error = tr("%n planned module(s) did not load; the usable partial environment was activated",
                    nullptr, missing.size());
    return true;
}

bool MibModule::ApplyProfileRuntime(const MibEffectivePlan &plan, QString *error)
{
    if(error)error->clear();
    if(!environmentManager){if(error)*error=tr("MIB Environment worker is unavailable");return false;}
    requestedPlans.insert(plan.profileId,plan);latestRequestedPlan=plan;
    environmentManager->request(plan);
    return true;
}

void MibModule::RestoreRuntimeAfterEditorValidation()
{
    const MibEffectivePlan restorePlan=hasActiveProfilePlan?activeProfilePlan:latestRequestedPlan;
    if(environmentManager&&!restorePlan.sha256.isEmpty()){requestedPlans.insert(restorePlan.profileId,restorePlan);
        environmentManager->request(restorePlan,true);return;}
    Refresh();
}

MibEnvironmentBuildResult MibModule::BuildEnvironment(const MibEffectivePlan &plan)
{
    auto operation=MibEngine::instance().beginOperation(QStringLiteral("async-plan-build"));
    MibEnvironmentBuildResult result;
    InitLib(1);
    const QStringList unavailable=LoadEffectivePlan(plan);
    QStringList missing;
    for(const QString &identity:plan.effectiveModules)
        if(!smiIsLoaded(identity.toLocal8Bit().constData()))missing.append(identity);
    missing.append(unavailable);missing.removeDuplicates();
    result.environment=MibEnvironmentExtractor().extract(plan,missing);
    for(SmiModule *module=smiGetFirstModule();module;module=smiGetNextModule(module))
        if(module->name)result.loadedModules.append(QString::fromLocal8Bit(module->name));
    result.loadedModules.removeDuplicates();result.loadedModules.sort(Qt::CaseSensitive);
    if(!MibEnvironmentRegistry::isUsableMaterialization(result.environment))
        result.error=tr("No planned MIB modules could be materialized");
    else if(!missing.isEmpty())
        result.error=tr("%n planned module(s) did not load; a usable partial environment was activated",nullptr,missing.size());
    return result;
}

bool MibModule::ValidateModuleFile(const QString &path, QString *error,
                                   MibValidationLevel level)
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("module-validation"));
    DiagnosticLogger::log("MIB", QStringLiteral("Downloaded MIB validation begin file=%1")
                          .arg(path));
    const int savedFlags = smiGetFlags();
    int validationFlags = savedFlags | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR;
    if (MibValidationRecursive(level)) validationFlags |= SMI_FLAG_RECURSIVE;
    else validationFlags &= ~SMI_FLAG_RECURSIVE;
    smiSetFlags(validationFlags);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(MibValidationErrorLevel(level));
    char *module = smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData());
    smiSetFlags(savedFlags);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(3);
    if (!module) {
        if (error) *error = tr("libsmi validation failed; see MIB diagnostics and Log");
        DiagnosticLogger::log("MIB", QStringLiteral("Downloaded MIB validation failed file=%1")
                              .arg(path));
        return false;
    }
    DiagnosticLogger::log("MIB", QStringLiteral("Downloaded MIB validation succeeded module=%1")
                          .arg(QString::fromLocal8Bit(module)));
    return true;
}

void MibModule::Refresh()
{
    std::unique_ptr<char, decltype(&std::free)> old_smipath{nullptr,std::free};
    {auto operation=MibEngine::instance().beginOperation(QStringLiteral("refresh-old-path"));old_smipath.reset(smiGetPath());}
    ReadMibPaths();
    std::unique_ptr<char, decltype(&std::free)> new_smipath{nullptr,std::free};
    {auto operation=MibEngine::instance().beginOperation(QStringLiteral("refresh-new-path"));new_smipath.reset(smiGetPath());}

    if (QString(old_smipath.get()) != QString(new_smipath.get())) {
        RebuildCandidateList();
    }

    if (hasActiveProfilePlan) ApplyProfileRuntime(activeProfilePlan);
    s->MainUI()->LoadedModules->resizeColumnToContents(0);
    s->MainUI()->UnloadedModules->resizeColumnToContents(0);
    s->MainUI()->LoadedModules->sortByColumn(0, Qt::AscendingOrder);
    s->MainUI()->UnloadedModules->sortByColumn(0, Qt::AscendingOrder);
}

void MibModule::RescanPath()
{
    ReadMibPaths();
    RebuildCandidateList();
    RebuildLoadedList();
    RebuildUnloadedList();
    emit inventoryChanged();
}

void MibModule::InitLib(int restart)
{
    if (restart)
    {
        std::unique_ptr<char, decltype(&std::free)> smipath{nullptr,std::free};
        {auto operation=MibEngine::instance().beginOperation(QStringLiteral("parser-path-before-restart"));
         smipath.reset(smiGetPath());}
        MibEngine::instance().initialize(QString::fromLocal8Bit(smipath.get()),true);
        auto operation=MibEngine::instance().beginOperation(QStringLiteral("parser-lifecycle-configuration"));
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
    }
    else
    {
        MibEngine::instance().initialize();
        auto operation=MibEngine::instance().beginOperation(QStringLiteral("parser-lifecycle-configuration"));
        smiSetFlags(smiGetFlags() | SMI_FLAG_ERRORS);
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
        // Read in the libsmi rc script -- shouldn't be necessary anymore
        //smiReadConfig(s->GetSmiConfigFile().toLocal8Bit().data(), NULL);
    }
}

void MibModule::RegenerateSmiConf()
{
    auto engineOperation=MibEngine::instance().beginOperation(QStringLiteral("parser-configuration"));
    QFile smiconf(s->GetSmiConfigFile());
    if (!smiconf.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QString err = tr("Unable to regenerate smi.conf!\nError opening file %1")
                .arg(smiconf.fileName());
        QMessageBox::critical(NULL, tr("MIB Navigator error"), err, QMessageBox::Ok);
        return;
    }

    // write out mibpaths
    QTextStream out(&smiconf);

    std::unique_ptr<char, decltype(&std::free)> smipath{ smiGetPath(), &std::free };
    QStringList mibpaths = QString(smipath.get()).split(SMI_PATH_SEPARATOR);
    out << "path " << mibpaths.join(SMI_PATH_SEPARATOR) << Qt::endl;

}
