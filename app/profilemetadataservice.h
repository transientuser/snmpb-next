#ifndef PROFILEMETADATASERVICE_H
#define PROFILEMETADATASERVICE_H

#include "profilemetadatarepository.h"

#include <QObject>

class ProfileMetadataService : public QObject
{
    Q_OBJECT

public:
    explicit ProfileMetadataService(const QString &fileName,
                                    QObject *parent = nullptr);
    ProfileMetadataRecord metadataForProfile(const QString &profileId) const;
    const QList<ProfileMetadataRecord> &allMetadata() const;
    bool update(const ProfileMetadataRecord &record);
    bool setNotes(const QString &profileId, const QString &notes);
    bool setTags(const QString &profileId, const QStringList &tags);
    bool remove(const QString &profileId);
    bool copy(const QString &sourceId, const QString &destinationId);
    bool reconcile(const QStringList &existingProfileIds, bool persistChanges);
    QStringList allTags() const;
    void reload();

signals:
    void metadataChanged(const QString &profileId);

private:
    int indexOf(const QString &profileId) const;
    bool save();

    ProfileMetadataRepository repository;
    QList<ProfileMetadataRecord> records;
};

#endif
