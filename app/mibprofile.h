#ifndef MIBPROFILE_H
#define MIBPROFILE_H

#include "miblibrary.h"

#include <QList>
#include <QMap>
#include <QStringList>

enum class MibProfileType { All, Standards, Custom, Folder };

struct MibProviderPin {
    QString canonicalPath;
    QString sha256;
};

struct MibProfileRecord {
    QString id;
    QString name;
    MibProfileType type = MibProfileType::Custom;
    QStringList explicitModules;
    bool includeStandardBase = false;
    QString directory;
    QMap<QString, MibProviderPin> providerPins;
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
    bool refreshAutomaticProfiles(const QString &mibRoot, QString *error = nullptr);
private:
    static bool isBuiltIn(const QString &id);
    MibProfileRepository repository;
    QList<MibProfileRecord> builtInProfiles = MibProfileDefinitions::builtIns();
    QList<MibProfileRecord> customProfiles;
    QList<MibProfileRecord> folderProfiles;
};

#endif
