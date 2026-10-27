#pragma once
#include <QAbstractListModel>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QUrl>
#include <QTimer>

namespace bitshell::core {
class LocalFileModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)
    Q_PROPERTY(int count READ rowCount NOTIFY changed)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY changed)
public:
    enum Role { NameRole = Qt::UserRole + 1, DirectoryRole, SizeTextRole };
    explicit LocalFileModel(QObject* parent = nullptr);
    ~LocalFileModel() override;
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int,QByteArray> roleNames() const override;
    QString currentPath() const { return m_path; }
    QString statusText() const { return m_status; }
    bool busy() const { return m_copying; }
    bool active() const { return m_active; }
    void setActive(bool active);
    bool canGoBack() const { return m_position > 0; }
    bool canGoForward() const { return m_position + 1 < m_history.size(); }
    Q_INVOKABLE void changeDirectory(const QString& path);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void openEntry(int row);
    Q_INVOKABLE QUrl entryUrl(int row) const;
    Q_INVOKABLE QUrl directoryUrl() const { return QUrl::fromLocalFile(m_path); }
    Q_INVOKABLE void copyFiles(const QList<QUrl>& urls);
    Q_INVOKABLE void startDrag(int row);
    static void dragUrls(const QList<QUrl>& urls);
    static QString availablePath(const QString& directory, const QString& name);
signals:
    void changed();
private:
    QFileInfoList m_entries;
    QString m_path, m_status, m_copyResult;
    QStringList m_history;
    int m_position = -1;
    QFileSystemWatcher m_watcher;
    QFutureWatcher<QString> m_copy;
    bool m_copying = false;
    bool m_active = true;
    QTimer m_refreshTimer;
};
}
