#ifndef MIBENVIRONMENT_H
#define MIBENVIRONMENT_H

#include <QDateTime>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>

inline constexpr int MIB_ENVIRONMENT_SCHEMA_VERSION = 2;
inline constexpr int MIB_ENVIRONMENT_BUILDER_VERSION = 2;

enum class MibEnvironmentStatus { Complete, Partial, Unusable };
enum class MibEnvironmentFindingKind {
    PlannedModuleFailed, ProviderMismatch, UnexpectedRecursiveModule,
    ParserIdentityMismatch, ImportMismatch, MalformedModule, PartialLoad,
    MissingMetadata
};
enum class MibEnvironmentLanguage { Unknown, SmiV1, SmiV2, SmiNg, Sppi, Yang };
enum class MibEnvironmentBaseType {
    Unknown, Integer32, OctetString, ObjectIdentifier, Unsigned32, Integer64,
    Unsigned64, Float32, Float64, Float128, Enumeration, Bits, Pointer
};
enum class MibEnvironmentNodeKind {
    Unknown, Node, Scalar, Table, Row, Column, Notification, Group,
    Compliance, Capabilities
};
enum class MibEnvironmentIndexKind { None, Index, Augment, Reorder, Sparse, Expand };
enum class MibEnvironmentAccess {
    Unknown, NotImplemented, NotAccessible, Notify, ReadOnly, ReadWrite,
    Install, InstallNotify, ReportOnly, EventOnly
};
enum class MibEnvironmentStatusCode { Unknown, Current, Deprecated, Mandatory, Optional, Obsolete };

struct MibEnvironmentFinding {
    MibEnvironmentFindingKind kind = MibEnvironmentFindingKind::MissingMetadata;
    QString moduleIdentity;
    QString plannedProviderPath;
    QString actualProviderPath;
    QString detail;
};

struct MibEnvironmentValue {
    MibEnvironmentBaseType baseType = MibEnvironmentBaseType::Unknown;
    bool isSigned = false;
    qint64 signedValue = 0;
    quint64 unsignedValue = 0;
    QByteArray bytes;
    QList<quint32> oid;
    QString canonicalText;
};

struct MibEnvironmentConstraint {
    MibEnvironmentValue minimum;
    MibEnvironmentValue maximum;
    bool isSizeConstraint = false;
};

struct MibEnvironmentNamedValue {
    QString name;
    MibEnvironmentValue value;
};

struct MibEnvironmentTypeRecord {
    QString id;                 // MODULE::type, or a stable anonymous node type id
    QString moduleIdentity;
    QString name;
    MibEnvironmentBaseType baseType = MibEnvironmentBaseType::Unknown;
    int declaration = 0;
    QString parentTypeId;
    QStringList ancestry;       // immediate parent first
    QString displayHint;
    QString units;
    QString description;
    QString reference;
    int status = 0;
    QList<MibEnvironmentConstraint> constraints;
    QList<MibEnvironmentNamedValue> namedValues;
    MibEnvironmentValue defaultValue;
};

struct MibEnvironmentRevisionRecord {
    QDateTime date;
    QString description;
};

struct MibEnvironmentImportRecord {
    QString moduleIdentity;
    QString symbol;
};

struct MibEnvironmentModuleRecord {
    QString identity;
    MibEnvironmentLanguage language = MibEnvironmentLanguage::Unknown;
    QString plannedProviderPath;
    QString actualProviderPath;
    QString rawProviderSha256;
    QList<MibEnvironmentImportRecord> imports;
    QList<MibEnvironmentRevisionRecord> revisions;
    QDateTime lastUpdated;
    QString organization;
    QString contactInfo;
    QString description;
    QString reference;
    QString rootOid;
    QString rootName;
    QList<MibEnvironmentFinding> findings;
};

struct MibEnvironmentIndexObject {
    QString oid;
    QString qualifiedName;
    QString typeId;
    MibEnvironmentBaseType baseType = MibEnvironmentBaseType::Unknown;
};

struct MibEnvironmentNodeRecord {
    QString oid;
    QList<quint32> oidParts;
    QString name;
    QString moduleIdentity;
    QString qualifiedName;
    MibEnvironmentNodeKind kind = MibEnvironmentNodeKind::Unknown;
    int declaration = 0;
    QString parentOid;
    QStringList childOids;
    MibEnvironmentAccess access = MibEnvironmentAccess::Unknown;
    MibEnvironmentStatusCode status = MibEnvironmentStatusCode::Unknown;
    QString syntaxName;
    QString typeId;
    MibEnvironmentBaseType baseType = MibEnvironmentBaseType::Unknown;
    QString textualConventionId;
    QStringList textualConventionAncestry;
    QString displayHint;
    QString units;
    QString description;
    QString reference;
    QList<MibEnvironmentConstraint> constraints;
    QList<MibEnvironmentNamedValue> namedValues;
    MibEnvironmentValue defaultValue;
    QString tableOid;
    QString rowOid;
    QStringList columnOids;
    MibEnvironmentIndexKind indexKind = MibEnvironmentIndexKind::None;
    QList<MibEnvironmentIndexObject> indexObjects;
    bool implied = false;
    bool creatable = false;
    QString augmentsRowOid;
    QStringList notificationObjectOids;
    QString trapEnterpriseOid;
    int trapGeneric = -1;
    int trapSpecific = -1;
};

struct MibEnvironmentTelemetry {
    qsizetype moduleCount = 0;
    qsizetype nodeCount = 0;
    qsizetype typeCount = 0;
    quint64 ownedUtf16Characters = 0;
    quint64 enumRecordCount = 0;
    quint64 constraintCount = 0;
    quint64 tableIndexRecordCount = 0;
    quint64 notificationCount = 0;
    quint64 approximateOwnedBytes = 0;
    qint64 extractionMilliseconds = 0;
};

class MibEnvironmentExtractor;

class MibEnvironment final
{
public:
    int schemaVersion() const { return MIB_ENVIRONMENT_SCHEMA_VERSION; }
    int builderVersion() const { return MIB_ENVIRONMENT_BUILDER_VERSION; }
    const QString &planHash() const { return planSha256; }
    const QString &parserIdentity() const { return parserId; }
    MibEnvironmentStatus status() const { return constructionStatus; }
    qsizetype plannedCount() const { return plannedModuleCount; }
    qsizetype loadedCount() const { return loadedModuleCount; }
    qsizetype failedCount() const { return failedModuleCount; }
    const QList<MibEnvironmentFinding> &findings() const { return materializationFindings; }
    const QList<MibEnvironmentModuleRecord> &modules() const { return moduleRecords; }
    const QList<MibEnvironmentNodeRecord> &nodes() const { return nodeRecords; }
    const QList<MibEnvironmentTypeRecord> &types() const { return typeRecords; }
    const MibEnvironmentTelemetry &telemetry() const { return metrics; }

    const MibEnvironmentModuleRecord *module(const QString &identity) const;
    const MibEnvironmentNodeRecord *nodeByOid(const QString &numericOid) const;
    const MibEnvironmentNodeRecord *longestPrefixNode(const QString &numericOid,
                                                       QStringList *suffix = nullptr) const;
    QList<const MibEnvironmentNodeRecord *> nodesByOid(const QString &numericOid) const;
    const MibEnvironmentNodeRecord *nodeByQualifiedName(const QString &name) const;
    QList<const MibEnvironmentNodeRecord *> nodesByName(const QString &name) const;
    const MibEnvironmentTypeRecord *type(const QString &id) const;
    QStringList rootOids() const { return roots; }

private:
    friend class MibEnvironmentExtractor;
    QString planSha256;
    QString parserId;
    MibEnvironmentStatus constructionStatus = MibEnvironmentStatus::Unusable;
    qsizetype plannedModuleCount = 0;
    qsizetype loadedModuleCount = 0;
    qsizetype failedModuleCount = 0;
    QList<MibEnvironmentFinding> materializationFindings;
    QList<MibEnvironmentModuleRecord> moduleRecords;
    QList<MibEnvironmentNodeRecord> nodeRecords;
    QList<MibEnvironmentTypeRecord> typeRecords;
    MibEnvironmentTelemetry metrics;
    QStringList roots;
    QHash<QString, qsizetype> moduleIndex;
    QHash<QString, QList<qsizetype>> oidIndex;
    QHash<QString, qsizetype> qualifiedNameIndex;
    QHash<QString, QList<qsizetype>> unqualifiedNameIndex;
    QHash<QString, qsizetype> typeIndex;
};

using MibEnvironmentPtr = std::shared_ptr<const MibEnvironment>;

#endif
