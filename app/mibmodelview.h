#ifndef MIBMODELVIEW_H
#define MIBMODELVIEW_H

#include <QTreeView>

class MibTreeFilterModel;
class MibTreeModel;
class MibViewLoader;

class MibModelView : public QTreeView
{
    Q_OBJECT
public:
    explicit MibModelView(QWidget *parent = nullptr);
    void RegisterToLoader(MibViewLoader *loader);
    void setTreeModel(MibTreeModel *model);
    void Populate();
    void SelectFromOid(const QString &oid);
    void SetCurrentAgentIsV1(bool value) { agentIsV1 = value; }
    QString selectedOid() const { return currentOid(); }

signals:
    void NodeProperties(const QString &text);
    void WalkFromOid(const QString &oid);
    void GetFromOid(const QString &oid, int operation);
    void GetFromOidPromptInstance(const QString &oid, int operation);
    void GetFromOidSelectInstance(const QString &oid, int operation);
    void SetFromOid(const QString &oid);
    void Stop();
    void TableViewFromOid(const QString &oid);
    void GetTableInstancesFromOid(const QString &oid);
    void VarbindsFromOid(const QString &oid);

public slots:
    void SetWalkInProgress(bool value) { walkInProgress = value; }
    void FindFromNode();
    void ExecuteFindNext();

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;
    void currentChanged(const QModelIndex &current, const QModelIndex &previous) override;

private:
    QString currentOid() const;
    int currentKind() const;
    QString detailsFor(const QModelIndex &proxyIndex) const;
    QModelIndex proxyIndexForOid(const QString &oid) const;
    void restoreSelection();
    MibTreeFilterModel *filterModel;
    QString retainedOid;
    bool walkInProgress = false;
    bool agentIsV1 = true;
    QString searchText;
};

#endif
