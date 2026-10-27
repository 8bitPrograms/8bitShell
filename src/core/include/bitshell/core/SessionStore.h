#pragma once

#include <QAbstractListModel>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QSet>
#include <bitshell/core/YamlFile.h>

namespace bitshell::core {

class SessionStore : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY folderCountChanged)
    Q_PROPERTY(int revision READ revision NOTIFY revisionChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(QString lastImportSummary READ lastImportSummary NOTIFY lastImportSummaryChanged)
    Q_PROPERTY(QVariantMap routes READ routes NOTIFY revisionChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        HostRole,
        UserRole,
        PortRole,
        GroupRole,
        TagsRole,
        IdentityKeyRole,
        IdentityFileRole,
        ProxyJumpRole,
        ForwardX11Role,
        AgentForwardingRole,
        CompressionRole,
        BackspaceModeRole,
    };
    Q_ENUM(Role)

    explicit SessionStore(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] int folderCount() const;
    [[nodiscard]] int revision() const;
    [[nodiscard]] QString storagePath() const;
    [[nodiscard]] QString lastError() const;
    [[nodiscard]] QString lastImportSummary() const;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap getById(const QString& id) const;
    Q_INVOKABLE int find(const QString& query) const;
    Q_INVOKABLE QVariantMap search(const QString& query, int limit = 80) const;
    Q_INVOKABLE QVariantList treeRows(const QString& filter,
                                      const QStringList& expandedFolders) const;
    Q_INVOKABLE QStringList folderPaths() const;
    Q_INVOKABLE int addSession(const QVariantMap& values);
    Q_INVOKABLE bool updateSession(int row, const QVariantMap& values);
    Q_INVOKABLE bool removeSession(int row);
    Q_INVOKABLE QVariantMap deletionPreview(const QStringList& sessionIds,
                                             const QStringList& folders, bool all) const;
    Q_INVOKABLE bool deleteEntries(const QStringList& sessionIds,
                                   const QStringList& folders, bool all,
                                   int expectedRevision);
    Q_INVOKABLE int duplicateSession(int row);
    Q_INVOKABLE bool moveSession(int row, const QString& folderPath);
    Q_INVOKABLE bool addFolder(const QString& parentPath, const QString& name);
    Q_INVOKABLE bool renameFolder(const QString& path, const QString& newName);
    Q_INVOKABLE bool moveFolder(const QString& path,
                                const QString& destinationParentPath);
    Q_INVOKABLE bool removeFolder(const QString& path);
    Q_INVOKABLE int importOpenSshConfig();
    Q_INVOKABLE int importMRemoteNg(const QString& source);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE int importYaml(const QString& source);
    Q_INVOKABLE bool exportYaml(const QString& destination);
    QVariantMap routes() const { return m_routes; }
    Q_INVOKABLE bool saveRoute(const QString& id, const QVariantMap& route);
    Q_INVOKABLE bool removeRoute(const QString& id);
    Q_INVOKABLE bool assignFolderConnection(const QString& folder, const QVariantMap& selection);
    Q_INVOKABLE QVariantMap folderConnection(const QString& folder) const;
    Q_INVOKABLE QVariantMap prepareProfile(const QVariantMap& profile, const QString& username) const;

signals:
    void folderMoved(const QString& oldPath, const QString& newPath);
    void folderRemoved(const QString& path);
    void foldersRemoved(const QStringList& paths);
    void countChanged();
    void folderCountChanged();
    void revisionChanged();
    void lastErrorChanged();
    void lastImportSummaryChanged();

private:
    struct Session {
        QString id;
        QString name;
        QString host;
        QString user;
        int port = 22;
        QString group;
        QStringList tags;
        QString identityKey;
        QString identityFile;
        QString proxyJump;
        bool forwardX11 = false;
        bool agentForwarding = false;
        bool compression = false;
        QString backspaceMode = QStringLiteral("default");
        QVariantMap connection{{QStringLiteral("mode"), QStringLiteral("direct")}};
    };

    [[nodiscard]] static Session fromMap(const QVariantMap& values,
                                         const Session* existing = nullptr);
    [[nodiscard]] static QVariantMap toMap(const Session& session);
    [[nodiscard]] static QString normalizeFolderPath(const QString& path);
    void ensureFolderAncestors(const QString& path);
    void advanceRevision();
    void prepareTreeIndex() const;
    bool collectDeletion(const QStringList& sessionIds, const QStringList& folders,
                         bool all, QSet<QString>& removedIds,
                         QSet<QString>& removedFolders, QString& error) const;
    [[nodiscard]] bool save();
    QVariantMap yamlDocument() const;
    static bool validateYaml(const QVariantMap& document, QString& error);
    void setLastError(const QString& message);
    void setLastImportSummary(const QString& message);
    [[nodiscard]] bool isValidRow(int row) const;

    QVector<Session> m_sessions;
    QStringList m_folders;
    QVariantMap m_routes;
    QVariantMap m_folderConnections;
    int m_revision = 0;
    QString m_storagePath;
    YamlFile m_yaml;
    QString m_lastError;
    QString m_lastImportSummary;
    // One bounded index per catalog revision, not a cache of every search result.
    mutable int m_treeIndexRevision = -1;
    mutable QSet<QString> m_treeFolders;
    mutable QHash<QString, QStringList> m_treeChildren;
    mutable QHash<QString, QVector<int>> m_treeSessions;
    mutable QHash<QString, int> m_treeCounts;
    mutable QStringList m_treeSearchText;
};

} // namespace bitshell::core
