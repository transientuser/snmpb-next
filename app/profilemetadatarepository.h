#ifndef PROFILEMETADATAREPOSITORY_H
#define PROFILEMETADATAREPOSITORY_H

#include <QList>
#include <QString>
#include <QStringList>

struct ProfileMetadataRecord
{
    QString profileId;
    QString notes;
    QStringList tags;
    QStringList preferredMibs;
    bool hasActiveProtocol = false;
    int activeProtocol = 0;
    QString usmCredentialId;
    bool hasRequestSettingsMode = false;
    int requestSettingsMode = 0; // 0 legacy, 1 inherit, 2 override
    int overrideTimeout = 3;
    int overrideRetries = 1;
    int overrideBulkNonRepeaters = 0;
    int overrideBulkMaxRepetitions = 10;
};

class ProfileMetadataRepository
{
public:
    static constexpr int CurrentVersion = 3;

    explicit ProfileMetadataRepository(const QString &fileName);
    QList<ProfileMetadataRecord> load() const;
    bool save(const QList<ProfileMetadataRecord> &records) const;
    QString fileName() const;

    static QStringList normalizeTags(const QStringList &tags);
    static QStringList normalizeMibs(const QStringList &mibs);

private:
    QString path;
};

#endif
