#ifndef COMMUNITYBINDINGREPOSITORY_H
#define COMMUNITYBINDINGREPOSITORY_H

#include <QHash>
#include <QString>

class CommunityBindingRepository
{
public:
    static constexpr int CurrentVersion = 1;
    explicit CommunityBindingRepository(const QString &fileName);
    QHash<QString, QString> load() const;
    bool save(const QHash<QString, QString> &bindings) const;
private:
    QString fileName;
};

#endif
