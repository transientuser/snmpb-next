#include "mibengine.h"
#include "mibdiagnosticcollector.h"
#include "miblibrary.h"
#include "smi.h"
#include <QElapsedTimer>
#include <QDir>

QString MibEngine::libraryVersion() const { return QStringLiteral(SMI_VERSION_STRING); }
void MibEngine::initialize(const QString &searchPath,bool restart)
{
    auto operation=beginOperation(QStringLiteral("initialize"));
    if(restart) smiExit();
    smiInit(nullptr);
    if(!searchPath.isEmpty())smiSetPath(searchPath.toLocal8Bit().constData());
}
void MibEngine::shutdown(){auto operation=beginOperation(QStringLiteral("shutdown"));smiExit();}

MibEngineValidationResult MibEngine::validateSource(const QByteArray &content,
    const QString &directory,int errorLevel,bool recursive)
{
    auto operation=beginOperation(QStringLiteral("editor-validation"));
    QElapsedTimer timer;timer.start();MibEngineValidationResult result;
    const int savedFlags=smiGetFlags();int flags=savedFlags|SMI_FLAG_ERRORS|SMI_FLAG_NODESCR;
    if(recursive)flags|=SMI_FLAG_RECURSIVE;else flags&=~SMI_FLAG_RECURSIVE;
    smiSetFlags(flags);static std::atomic<quint64> operationId{1};
    MibDiagnosticCollector collector(operationId.fetch_add(1),QStringLiteral("editor"));
    collector.install(errorLevel);
    result.success=MibValidationStaging::validate(content,directory,[](const QString &path){
        return smiLoadModule(QDir::toNativeSeparators(path).toLocal8Bit().constData())!=nullptr;
    },&result.stagingError);
    collector.finish(nullptr,0);result.diagnostics=collector.diagnostics();smiSetFlags(savedFlags);
    if(!result.stagingError.isEmpty()){MibDiagnosticRecord diagnostic;diagnostic.severity=2;
        diagnostic.tag=QStringLiteral("validation-staging");diagnostic.message=result.stagingError;
        diagnostic.rawText=MibDiagnosticCollector::rawText(diagnostic);result.diagnostics.append(diagnostic);}
    result.elapsedMilliseconds=timer.elapsed();return result;
}
