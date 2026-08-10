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
};

class ProfileMetadataRepository
{
public:
    static constexpr int CurrentVersion = 2;

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
