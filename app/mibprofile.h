#ifndef MIBPROFILE_H
#define MIBPROFILE_H

#include "miblibrary.h"

#include <QList>
#include <QMap>
#include <QStringList>

enum class MibProfileType { All, Standards, Custom };

struct MibProfileRecord {
    QString id;
    QString name;
    MibProfileType type = MibProfileType::Custom;
    QStringList explicitModules;
    bool includeStandardBase = false;
};

class MibProfileDefinitions
{
public:
    static QString allId();
    static QString standardsId();
    static QStringList standardsModules();
    static QList<MibProfileRecord> builtIns();
    static QString validCurrentId(const QString &savedId,
                                  const QList<MibProfileRecord> &profiles);
};

class MibProfileRepository
{
public:
    explicit MibProfileRepository(QString path);
    QList<MibProfileRecord> load(QString *error = nullptr) const;
    bool save(const QList<MibProfileRecord> &profiles, QString *error = nullptr) const;
    QString path() const { return filePath; }
private:
    QString filePath;
};

class MibProfileService
{
public:
    explicit MibProfileService(MibProfileRepository repository);
    bool reload(QString *error = nullptr);
    bool persist(QString *error = nullptr) const;
    QList<MibProfileRecord> profiles() const;
    const MibProfileRecord *find(const QString &id) const;
    QString create(const QString &name, QString *error = nullptr);
    QString duplicate(const QString &id, const QString &name, QString *error = nullptr);
    bool rename(const QString &id, const QString &name, QString *error = nullptr);
    bool remove(const QString &id, QString *error = nullptr);
    bool update(const MibProfileRecord &profile, QString *error = nullptr);
private:
    static bool isBuiltIn(const QString &id);
    MibProfileRepository repository;
    QList<MibProfileRecord> builtInProfiles = MibProfileDefinitions::builtIns();
    QList<MibProfileRecord> customProfiles;
};

struct MibProfileEffectiveSet {
    struct Requirement {
        QString moduleName;
        bool missing = false;
        QString reason;
    };
    QStringList explicitModules;
    QStringList automaticDependencies;
    QStringList effectiveModules;
    QStringList missingModules;
    QList<Requirement> requirements;
};

class MibProfileResolver
{
public:
    MibProfileEffectiveSet resolve(const MibProfileRecord &profile,
        const QStringList &availableModules, const MibCatalog &catalog) const;
};

#endif
