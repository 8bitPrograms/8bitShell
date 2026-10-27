#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QVariantList>
#include <bitshell/core/YamlFile.h>

namespace bitshell::core {
// Local launchers are independent of the SSH catalog. Discovery never launches a shell.
class LocalProfileStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString defaultProfileId READ defaultProfileId NOTIFY changed)
    Q_PROPERTY(QVariantMap defaultProfile READ defaultProfile NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(QString discoveryMessage READ discoveryMessage NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
public:
    explicit LocalProfileStore(QObject* parent = nullptr);
    ~LocalProfileStore() override;
    QVariantList profiles() const;
    QString defaultProfileId() const { return m_default; }
    QVariantMap defaultProfile() const { return profile(m_default); }
    QString lastError() const { return m_error; }
    bool discovering() const { return m_discovering; }
    QString discoveryMessage() const { return m_discoveryMessage; }
    QString storagePath() const { return m_yaml.fileName(); }
    Q_INVOKABLE QVariantMap profile(const QString& id) const;
    Q_INVOKABLE QString saveProfile(const QVariantMap& profile);
    Q_INVOKABLE bool removeProfile(const QString& id);
    Q_INVOKABLE bool setDefaultProfile(const QString& id);
    Q_INVOKABLE bool reload();
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString localPath(const QString& url) const { return YamlFile::localPath(url); }
    static QStringList distributionNames(const QByteArray& output);
    static QVariantMap prepareLaunch(const QVariantMap& profile, QString& error);
signals:
    void changed();
private:
    static bool validateProfile(const QVariantMap& profile, QString& error);
    bool persist(const QVariantList& profiles, const QString& defaultId);
    void discoverNative();
    void finishDiscovery(bool success, const QString& message = {});
    QVariantList m_detected, m_saved;
    QString m_default = QStringLiteral("system");
    QString m_error, m_discoveryMessage;
    YamlFile m_yaml;
    QProcess m_wsl;
    QTimer m_timeout;
    QByteArray m_wslOutput;
    bool m_discovering = false;
};
}
