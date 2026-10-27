#pragma once

#include <QAbstractListModel>
#include <QProcess>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVector>
#include <QTimer>
#include <QTemporaryDir>
#include <memory>

namespace bitshell::core {

class SftpController : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentPathChanged)
    Q_PROPERTY(QString target READ target NOTIFY targetChanged)
    Q_PROPERTY(QString lastTransferText READ lastTransferText NOTIFY lastTransferTextChanged)
    Q_PROPERTY(QString downloadDirectory READ downloadDirectory CONSTANT)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentPathChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY currentPathChanged)

public:
    enum class State {
        Disconnected,
        Connecting,
        Ready,
        Busy,
        Error,
    };
    Q_ENUM(State)

    enum Role {
        NameRole = Qt::UserRole + 1,
        DirectoryRole,
        SizeRole,
        SizeTextRole,
        ModifiedRole,
        PermissionsRole,
    };
    Q_ENUM(Role)

    explicit SftpController(QObject* parent = nullptr);
    ~SftpController() override;
    // Native unit-test dependency injection only. Not a QML property/invokable,
    // command-line flag or environment variable in the shipping application.
    void setTransportExecutableForTesting(const QString& path) { if (!busy()) m_testTransport = path; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] State state() const;
    [[nodiscard]] bool connected() const;
    [[nodiscard]] bool busy() const;
    [[nodiscard]] QString statusText() const;
    [[nodiscard]] QString currentPath() const;
    [[nodiscard]] QString target() const;
    [[nodiscard]] QString lastTransferText() const;
    [[nodiscard]] QString downloadDirectory() const;
    [[nodiscard]] int count() const;
    bool canGoBack() const { return m_historyPosition > 0; }
    bool canGoForward() const { return m_historyPosition + 1 < m_history.size(); }

    Q_INVOKABLE void connectToProfile(const QVariantMap& profile);
    Q_INVOKABLE void connectToProfileAt(const QVariantMap& profile, const QString& initialPath);
    Q_INVOKABLE void disconnectFromHost();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void openEntry(int row);
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void changeDirectory(const QString& path);
    Q_INVOKABLE void download(int row);
    Q_INVOKABLE void upload(const QUrl& localFile);
    Q_INVOKABLE void uploadFiles(const QList<QUrl>& localFiles);
    Q_INVOKABLE void downloadTo(int row, const QUrl& directory);
    Q_INVOKABLE void startDrag(int row);
    Q_INVOKABLE QUrl preparedDragUrl() const { return QUrl::fromLocalFile(m_dragPath); }
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();

signals:
    void stateChanged();
    void connectedChanged();
    void statusTextChanged();
    void currentPathChanged();
    void targetChanged();
    void lastTransferTextChanged();
    void countChanged();
    void transferCompleted();

private:
    struct Entry {
        QString name;
        bool directory = false;
        qint64 size = 0;
        QString modified;
        QString permissions;
    };

    enum class Operation {
        None,
        List,
        Download,
        Upload,
    };

    void startListing(const QString& requestedPath, bool connecting);
    void startBatch(Operation operation,
                    const QStringList& commands,
                    const QString& activityText);
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    void finishWithError(const QString& output);
    void applyListing(const QByteArray& output);
    void setState(State state);
    void setStatusText(const QString& text);
    void setLastTransferText(const QString& text);
    void clearEntries();

    [[nodiscard]] QString workingDirectoryCommand() const;
    [[nodiscard]] QString availableDownloadPath(const QString& fileName) const;
    [[nodiscard]] static QString quoteSftpArgument(const QString& value);
    [[nodiscard]] static QString quoteGlobArgument(const QString& value);
    [[nodiscard]] static QString joinRemotePath(const QString& base,
                                                const QString& child);
    [[nodiscard]] static QString parentRemotePath(const QString& path);
    [[nodiscard]] static QString humanSize(qint64 bytes);
    [[nodiscard]] static QString friendlyError(const QString& output);

    QVector<Entry> m_entries;
    QProcess m_process;
    QVariantMap m_profile;
    State m_state = State::Disconnected;
    Operation m_operation = Operation::None;
    bool m_connected = false;
    bool m_processFailureHandled = false;
    QString m_statusText = QStringLiteral("Select an SSH session to browse remote files");
    QString m_currentPath;
    QString m_requestedPath;
    QString m_target;
    QString m_lastTransferText;
    QString m_transferName;
    QString m_transferLocalPath;
    QByteArray m_pendingBatch;
    QByteArray m_standardOutput;
    QByteArray m_standardError;
    QTimer m_timeout;
    QStringList m_history;
    int m_historyPosition = -1;
    int m_historyTarget = -1;
    QString m_dragKey, m_dragPath, m_pendingDragKey;
    bool m_preparingDrag = false;
    QString m_testTransport;
    QString m_downloadDestination;
    std::unique_ptr<QTemporaryDir> m_downloadStaging;
    std::unique_ptr<QTemporaryDir> m_dragStaging;
};

} // namespace bitshell::core
