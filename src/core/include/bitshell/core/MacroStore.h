#pragma once
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <bitshell/core/YamlFile.h>

namespace bitshell::core {
class MacroStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString lastImportSummary READ lastImportSummary NOTIFY changed)
public:
    explicit MacroStore(QObject* parent = nullptr);
    QVariantList groups() const { return m_groups; }
    int revision() const { return m_revision; }
    QString lastError() const { return m_error; }
    QString storagePath() const { return m_path; }
    QString lastImportSummary() const { return m_summary; }
    Q_INVOKABLE bool reload();
    Q_INVOKABLE int importYaml(const QString& source, bool includeAssignments = false);
    Q_INVOKABLE bool exportYaml(const QString& destination, const QString& groupId = QString{});
    Q_INVOKABLE QVariantMap group(const QString& id) const;
    Q_INVOKABLE QString saveGroup(const QString& id, const QString& name, const QVariantList& buttons);
    Q_INVOKABLE bool removeGroup(const QString& id);
    Q_INVOKABLE bool assignFolder(const QString& path, const QString& groupId);
    Q_INVOKABLE QString assignedGroup(const QString& path) const;
    Q_INVOKABLE QVariantMap effectiveGroup(const QString& path) const;
    Q_INVOKABLE bool remapFolder(const QString& oldPath, const QString& newPath);
    Q_INVOKABLE bool removeFolder(const QString& path);
    Q_INVOKABLE bool removeFolders(const QStringList& paths);
signals:
    void changed();
private:
    bool persist(const QVariantList& groups, const QVariantMap& assignments);
    static bool validateYaml(const QVariantMap& document, QString& error);
    static QString normalizedPath(const QString& path);
    QVariantList m_groups;
    QVariantMap m_assignments;
    QString m_path;
    QString m_error;
    QString m_summary;
    YamlFile m_yaml;
    int m_revision = 0;
};
}
