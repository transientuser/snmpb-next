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
#include <qapplication.h>
#include <qmainwindow.h>
#include <QTranslator>
#include <QLibraryInfo>
#include <QDialog>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QTimer>
#include <QElapsedTimer>
#include "snmpb.h"
#include "agentprofile.h"
#include "mibeditor.h"
#include "mibview.h"
#include "mibmodule.h"
#include "preferences.h"
#include "snmpbapp.h"
#include "productidentity.h"
#include "diagnosticlogger.h"

QString file_to_open;

int main( int argc, char ** argv )
{
    bool launch_smoke_test = false;
    QString smoke_config_dir;
    QString smoke_mib_file;
    for (int i = 1; i < argc; ++i)
    {
        const QString argument = QString::fromLocal8Bit(argv[i]);
        if (argument == "--launch-smoke-test" && i + 1 < argc)
        {
            launch_smoke_test = true;
            smoke_config_dir = QString::fromLocal8Bit(argv[++i]);
        }
        else if (argument == "--smoke-mib" && i + 1 < argc)
        {
            smoke_mib_file = QString::fromLocal8Bit(argv[++i]);
        }
    }

    if (launch_smoke_test)
    {
        QDir().mkpath(smoke_config_dir);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           smoke_config_dir);
    }

    SnmpBApplication app( argc, argv );
    QCoreApplication::setOrganizationDomain(ProductIdentity::LegacySettingsDomain);
    QCoreApplication::setApplicationName(ProductIdentity::LegacySettingsApplication);
    // Keep applicationName="SnmpB" for QSettings compatibility; only the
    // user-visible display identity is rebranded.
    app.setApplicationDisplayName(QString::fromLatin1(ProductIdentity::Name));
    DiagnosticLogger::initialize(QStringLiteral(SNMPB_VERSION_STRING),
                                 QCoreApplication::arguments());
    DiagnosticLogger::log("Startup", "QApplication creation complete");
    DiagnosticLogger::log("Startup", "application identity setup complete");

    Snmpb snmpb(launch_smoke_test);

    // Qt translations
    QTranslator l10n_qt;
    const bool qt_translation_loaded =
        l10n_qt.load("qt_" + QLocale::system().name(),
                     QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    Q_UNUSED(qt_translation_loaded);
    app.installTranslator(&l10n_qt);

    // SnmpB translations
    QTranslator l10n_app;
    const bool app_translation_loaded =
        l10n_app.load(":/i18n/snmpb." + QLocale::system().name());
    Q_UNUSED(app_translation_loaded);
    app.installTranslator(&l10n_app);

    QElapsedTimer mainWindowTimer; mainWindowTimer.start();
    QMainWindow mw;
    DiagnosticLogger::installMainWindowLifecycle(&mw);
    DiagnosticLogger::log("Startup", "main window construction begin");
    const auto close_visible_dialogs = []() {
        const auto top_level_widgets = QApplication::topLevelWidgets();
        for (QWidget *widget : top_level_widgets)
        {
            if (QDialog *dialog = qobject_cast<QDialog *>(widget);
                dialog && dialog->isVisible())
                dialog->accept();
        }
    };
    QTimer startup_dialog_closer;
    if (launch_smoke_test)
    {
        QObject::connect(&startup_dialog_closer, &QTimer::timeout,
                         close_visible_dialogs);
        startup_dialog_closer.start(10);
    }
    snmpb.BindToGUI(&mw);
    DiagnosticLogger::log("Startup", QStringLiteral("main window construction end elapsed_ms=%1").arg(mainWindowTimer.elapsed()));
    startup_dialog_closer.stop();
    mw.show();
    DiagnosticLogger::log("Startup", QStringLiteral("main window show request elapsed_ms=%1").arg(mainWindowTimer.elapsed()));
    app.connect(&app, SIGNAL( lastWindowClosed() ), &app, SLOT( quit() ));
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, [] {
        DiagnosticLogger::log("Shutdown", "QApplication lastWindowClosed", false);
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [&snmpb] {
        DiagnosticLogger::log("Shutdown", "QApplication aboutToQuit", false);
        snmpb.Shutdown();
    });

    // Load a file specified as argument in the Mib Editor
    if (!launch_smoke_test &&
        (!file_to_open.isEmpty() || QCoreApplication::arguments().count() > 1))
    {
        snmpb.MibEditorObj()->MibFileOpen(file_to_open.isEmpty()?QCoreApplication::arguments().at(1):file_to_open);
        snmpb.MainUI()->TabW->setCurrentIndex(2); // Select the Editor Tab
    }

    if (launch_smoke_test)
    {
        const auto runSmoke = [&]() {
            bool passed = true;
            QFile smoke_log(QDir(smoke_config_dir).filePath("launch-smoke.log"));
            if (!smoke_log.open(QIODevice::WriteOnly | QIODevice::Text))
            {
                app.exit(1);
                return;
            }
            QTextStream smoke_output(&smoke_log);
            const auto check = [&](bool condition, const char *description) {
                smoke_output << "launch-smoke: "
                             << (condition ? "PASS" : "FAIL") << ": "
                             << description << Qt::endl;
                passed = passed && condition;
            };

            QSettings settings;
            check(QFileInfo(settings.fileName()).absoluteFilePath().startsWith(
                      QFileInfo(smoke_config_dir).absoluteFilePath()),
                  "QSettings uses the isolated directory");
            check(mw.isVisible(), "main window is visible");
            QDockWidget *devicesDock = mw.findChild<QDockWidget *>("DevicesDock");
            check(devicesDock && devicesDock->widget(),
                  "Devices dock created and integrated");
            check(snmpb.MibModuleObj()->CurrentEnvironment() &&
                  !snmpb.MibModuleObj()->CurrentEnvironment()->modules().isEmpty(),
                  "Profile Environment materialized bundled MIB modules");
            check(snmpb.MainUI()->MIBTree->model()->rowCount() > 0,
                  "MIB tree populated");

            snmpb.MainUI()->MIBTree->SelectFromOid("1.3.6.1.2.1.1");
            check(snmpb.MainUI()->MIBTree->currentIndex().isValid(),
                  "MIB OID selection works");

            if (!smoke_mib_file.isEmpty())
            {
                snmpb.MibEditorObj()->MibFileOpen(smoke_mib_file);
                snmpb.MibEditorObj()->VerifyMIB();
                check(!snmpb.MainUI()->MIBFile->toPlainText().isEmpty(),
                      "MIB editor opened and verified a local module");
                check(snmpb.MainUI()->MIBLogL->text().startsWith("Verification complete."),
                      "MIB editor parsing completed");
            }

            const QString smoke_profile = "ctest-local-profile";
            snmpb.APManagerObj()->Add(smoke_profile, "127.0.0.1", "161",
                                     true, false, false, "localhost");
            check(snmpb.APManagerObj()->GetAgentProfileRecordByName(smoke_profile) != nullptr,
                  "Agent Profile load/save works");

            QTimer dialog_closer;
            QObject::connect(&dialog_closer, &QTimer::timeout,
                             close_visible_dialogs);
            dialog_closer.start(10);
            snmpb.ManageAgentProfiles(false);
            dialog_closer.stop();
            check(QFileInfo::exists(snmpb.GetAgentsConfigFile()),
                  "Agent Profile Manager opened and persisted configuration");

            snmpb.PreferencesObj()->Save();
            settings.sync();
            check(settings.contains("network/enableipv4") &&
                  settings.contains("mibpaths/size"),
                  "preferences saved to isolated configuration");

            QTranslator translation_probe;
            check(translation_probe.load(":/i18n/snmpb.uk_UA"),
                  "bundled application translation loads");
            check(QFileInfo::exists(snmpb.GetSmiConfigFile()),
                  "loaded-MIB state file initialized");
            check(QFileInfo::exists(snmpb.GetBootCounterConfigFile()),
                  "SNMPv3 boot counter initialized locally");

            mw.close();
            smoke_log.close();
            app.exit(passed ? 0 : 1);
        };
        if (snmpb.MibModuleObj()->CurrentEnvironment())
            QTimer::singleShot(0, &app, runSmoke);
        else
            QObject::connect(snmpb.MibModuleObj(), &MibModule::profileRuntimeReady, &app,
                [runSmoke](const QString &,const MibEffectivePlan &,MibEnvironmentPtr,
                           const QStringList &,bool,bool){runSmoke();},Qt::SingleShotConnection);
    }

    DiagnosticLogger::log("Startup", "event-loop entry");
    const int result = app.exec();
    DiagnosticLogger::log("Shutdown",
                          QStringLiteral("event loop returned code=%1").arg(result), false);
    DiagnosticLogger::shutdown();
    return result;
}
