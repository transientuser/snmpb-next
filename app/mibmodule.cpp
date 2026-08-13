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
#include <algorithm>

#include "mibmodule.h"
#include "miblibrary.h"
#include "diagnosticlogger.h"
#include "agent.h"
#include "preferences.h"
#include "preferredmibresolver.h"
#include "mibservice.h"
#include "mibcandidatefilter.h"

LoadedMibModule::LoadedMibModule(SmiModule* mod)
{
    record = MibService::snapshotModule(mod);
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
    // Must be connected before call to InitLib ...
    connect(this, SIGNAL ( LogError(QString) ),
            s->MainUI()->LogOutput, SLOT ( append (QString) ));

    CurrentModuleObject = this;
    InitLib(0);
    RescanPath();

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
    DiagnosticLogger::log("MIB", "MIB search-path resolution and MIB/PIB loading begin");
    // This is all futile. FIXME
    // To machine code, file extension says literally nothing about its content.
    // To a human, it *might* vaguely identify the content-type, possibly fooling the human.
    // But code like this function should not exist.
    // Instead, try to load every readable file as MIB, and back off gracefully.

    return MibCandidateFilter::accepts(filename);
}

void MibModule::RebuildTotalList()
{
    /* Enable error reporting */
    smiSetFlags(smiGetFlags() | SMI_FLAG_ERRORS | SMI_FLAG_NODESCR);
    smiSetErrorHandler(NormalErrorHdlr);
    smiSetErrorLevel(3);

    std::unique_ptr<char, decltype(&std::free)>
        smipath{ smiGetPath(), &std::free };
    QStringList
        smipaths = QString(smipath.get()).split(SMI_PATH_SEPARATOR);
   
    Total.clear();
    KnownModuleNames.clear();
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
                    const QString canonicalName = QString::fromLatin1(smiModule->name);
                    if (!KnownModuleNames.contains(canonicalName))
                        KnownModuleNames.append(canonicalName);
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
                              tr("<p>The following files from configured MIB search paths were considered MIB candidates but failed to load. Check the Log tab for parser diagnostics.</p>")
                                  % "<code>\n"
                                  % errored_files.join('\n')
                                  % "\n</code>",
                              QMessageBox::Ok, Qt::NoButton);
    }

    std::sort(Total.begin(), Total.end(), compareModule);
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

QStringList MibModule::LoadPreferredModules(const QStringList &modules)
{
    PreferredMibResolution resolution = PreferredMibResolver::resolve(
        modules, AvailableModuleNames(), LoadedModuleNames());
    MibService mibService(NormalErrorHdlr, 3);
    const MibLoadResult loadResult = mibService.loadModules(resolution.toLoad, 3);
    const QStringList loadedNow = loadResult.loadedModules;
    resolution.unavailable.append(loadResult.unavailableModules);
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
        Wanted.append(best_file.toLatin1().data());
        s->PreferencesObj()->Save();
        Refresh();

        s->TabSelected();
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
    Loaded.clear();
    s->MainUI()->LoadedModules->clear();

    for (SmiModule *mod = smiGetFirstModule();
         mod;
         mod = smiGetNextModule(mod) )
    {
        LoadedMibModule lmodule(mod);
        Loaded.append(lmodule);

        QString required = Wanted.contains(lmodule.name) ? tr("yes") : tr("no");

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
        for(int j = 0; j < Loaded.count(); j++)
        { 
            if (QFileInfo(Loaded[j].path).fileName() == current)
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
    QList<QTreeWidgetItem *> item_list = 
                             s->MainUI()->UnloadedModules->selectedItems();

    for (int i = 0; i < item_list.size(); i++)
        Wanted.append(item_list[i]->text(0).toLatin1().data());

    if (!item_list.empty())
    {
        s->PreferencesObj()->Save();
    }
}

void MibModule::RemoveModule()
{
    QList<QTreeWidgetItem *> item_list = 
                             s->MainUI()->LoadedModules->selectedItems();

    for (int i = 0; i < item_list.size(); i++)
        Wanted.removeAll(QFileInfo(item_list[i]->text(3)).fileName());

    if (!item_list.empty())
    {
        s->PreferencesObj()->Save();
    }
}

void MibModule::ReadMibPaths()
{
    // read in MIB path from config
    QStringList paths;
    QSettings settings;
    int size = settings.beginReadArray("mibpaths");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        paths << settings.value("dir").toString();
    }

    const QString userLibrary = MibLibraryService().downloadedPath();
    if (!paths.contains(userLibrary))
        paths.append(userLibrary);

    smiSetPath(paths.join(SMI_PATH_SEPARATOR).toLocal8Bit().data());
}

void MibModule::ReadMibPreloads()
{
    // read in MIB preload list from preferences
    Wanted.clear();
    QSettings settings;
    int size = settings.beginReadArray("mibpreloads");
    for (int i = 0; i < size; ++i) {
        settings.setArrayIndex(i);
        Wanted << settings.value("mib").toString();
    }
}

bool MibModule::ValidateModuleFile(const QString &path, QString *error,
                                   MibValidationLevel level)
{
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
    std::unique_ptr<char, decltype(&std::free)> old_smipath{ smiGetPath(), std::free };
    ReadMibPaths();
    std::unique_ptr<char, decltype(&std::free)> new_smipath{ smiGetPath(), std::free };

    if (QString(old_smipath.get()) != QString(new_smipath.get())) {
        // trigger MIB rescan
    RescanPath();
    DiagnosticLogger::log("MIB", QStringLiteral(
        "MIB search-path resolution and MIB/PIB loading end loaded=%1 unloaded=%2")
        .arg(Loaded.size()).arg(Unloaded.size()));
    }

    ReadMibPreloads();

    RegenerateSmiConf();

    InitLib(1);

    s->MibLoaderObj()->Load(Wanted);
    RebuildLoadedList();
    RebuildUnloadedList();
    s->MainUI()->LoadedModules->resizeColumnToContents(0);
    s->MainUI()->UnloadedModules->resizeColumnToContents(0);
    s->MainUI()->LoadedModules->sortByColumn(0, Qt::AscendingOrder);
    s->MainUI()->UnloadedModules->sortByColumn(0, Qt::AscendingOrder);
}

void MibModule::RescanPath()
{
    ReadMibPaths();
    RebuildTotalList(); // this guy is slow...
    Refresh();
}

void MibModule::InitLib(int restart)
{
    if (restart)
    {
        std::unique_ptr<char, decltype(&std::free)>
            smipath{ smiGetPath(), std::free };
        smiExit();
        smiInit(NULL);
        smiSetPath(smipath.get());
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
    }
    else
    {
        smiInit(NULL);
        smiSetFlags(smiGetFlags() | SMI_FLAG_ERRORS);
        smiSetErrorHandler(NormalErrorHdlr);
        smiSetErrorLevel(0);
        // Read in the libsmi rc script -- shouldn't be necessary anymore
        //smiReadConfig(s->GetSmiConfigFile().toLocal8Bit().data(), NULL);
    }
}

void MibModule::RegenerateSmiConf()
{
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

    // write out mib preload list
    for (int i = 0; i < Wanted.size(); ++i)
        out << "load " << Wanted[i] << Qt::endl;
}
