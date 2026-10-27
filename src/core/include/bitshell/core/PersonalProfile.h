#pragma once
#include <QObject>
#include <QUrl>
#include <bitshell/core/YamlFile.h>

namespace bitshell::core {
class PersonalProfile : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    Q_PROPERTY(QString employeeId READ employeeId NOTIFY changed)
    Q_PROPERTY(QString username READ username NOTIFY changed)
    Q_PROPERTY(QString routingIdentity READ routingIdentity NOTIFY changed)
    Q_PROPERTY(bool cyberArkEnabled READ cyberArkEnabled WRITE setCyberArkEnabled NOTIFY changed)
    Q_PROPERTY(QString initial READ initial NOTIFY changed)
    Q_PROPERTY(bool avatarEnabled READ avatarEnabled WRITE setAvatarEnabled NOTIFY changed)
    Q_PROPERTY(QUrl avatarUrl READ avatarUrl NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
public:
    explicit PersonalProfile(QObject* parent = nullptr);
    QString displayName() const;
    QString employeeId() const;
    // Compatibility name used by the route resolver and older personal.yaml files.
    QString username() const { return employeeId(); }
    QString routingIdentity() const { return cyberArkEnabled() ? employeeId() : QString{}; }
    bool cyberArkEnabled() const { return m_data.value(QStringLiteral("cyberArkEnabled")).toBool(); }
    QString initial() const;
    bool avatarEnabled() const { return m_data.value(QStringLiteral("avatarEnabled")).toBool(); }
    QUrl avatarUrl() const;
    QString lastError() const { return m_error; }
    QString storagePath() const { return m_file.fileName(); }
    Q_INVOKABLE bool setUsername(const QString& value);
    Q_INVOKABLE bool setProfile(const QString& displayName, const QString& employeeId,
                                bool cyberArkEnabled);
    Q_INVOKABLE bool setDisplayName(const QString& value);
    Q_INVOKABLE bool setEmployeeId(const QString& value);
    void setCyberArkEnabled(bool enabled);
    void setAvatarEnabled(bool enabled);
    Q_INVOKABLE bool chooseAvatar(const QUrl& source);
    Q_INVOKABLE bool removeAvatar();
    Q_INVOKABLE bool approveRoute(const QString& fingerprint);
    Q_INVOKABLE bool clearRouteApprovals();
    Q_INVOKABLE bool routeApproved(const QString& fingerprint) const;
    Q_INVOKABLE bool reload();
    static bool isRouteApproved(const QString& fingerprint);
signals:
    void changed();
private:
    static QString path();
    static bool validate(const QVariantMap& data, QString& error);
    QString avatarPath() const;
    bool save(QVariantMap data);
    YamlFile m_file;
    QVariantMap m_data;
    QString m_error;
    QString m_avatarDigest;
};
}
