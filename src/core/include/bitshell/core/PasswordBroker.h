#pragma once

#include <QObject>
#include <QByteArray>
#include <QLocalServer>
#include <QProcessEnvironment>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>
#include <expected>

class QLocalSocket;

namespace bitshell::core {

// One short-lived password exchange for one direct SSH process. The returned
// environment contains only an expiring capability, never the password itself.
class PasswordBroker final : public QObject {
public:
    explicit PasswordBroker(QObject* parent = nullptr);
    ~PasswordBroker() override;

    [[nodiscard]] std::expected<QProcessEnvironment, QString>
    start(const QVariantMap& profile, const QByteArray& password);
    void stop();
    [[nodiscard]] static QString helperPath();

private:
    void acceptConnections();
    void readRequest(QLocalSocket* socket);
    void reject(QLocalSocket* socket);

    QLocalServer m_server;
    QTimer m_expiry;
    QSet<QLocalSocket*> m_clients;
    QByteArray m_password;
    QByteArray m_token;
    QStringList m_expectedPrompts;
};

} // namespace bitshell::core
