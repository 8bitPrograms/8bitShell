#include <bitshell/core/PasswordBroker.h>
#include <bitshell/core/SshSecurity.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QUuid>
#include <array>
#include <utility>

namespace bitshell::core {
namespace {
constexpr qsizetype maximumPasswordBytes = 4096;
constexpr qint64 maximumRequestBytes = 4096;
constexpr int sessionLifetimeMs = 60000;

void wipe(QByteArray& value) {
    value.fill('\0');
    value.clear();
}

bool sameToken(const QByteArray& left, const QByteArray& right) {
    if (left.size() != 64 || right.size() != 64) return false;
    unsigned difference = 0;
    for (qsizetype index = 0; index < left.size(); ++index)
        difference |= static_cast<unsigned char>(left[index]) ^ static_cast<unsigned char>(right[index]);
    return difference == 0;
}

QString unbracketed(QString host) {
    if (host.startsWith(u'[') && host.endsWith(u']')) return host.mid(1, host.size() - 2);
    return host;
}
} // namespace

PasswordBroker::PasswordBroker(QObject* parent)
    : QObject(parent), m_server(this), m_expiry(this) {
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(4);
    m_expiry.setObjectName(QStringLiteral("passwordBrokerExpiry"));
    m_expiry.setSingleShot(true);
    m_expiry.setInterval(sessionLifetimeMs);
    connect(&m_expiry, &QTimer::timeout, this, &PasswordBroker::stop);
    connect(&m_server, &QLocalServer::newConnection, this, &PasswordBroker::acceptConnections);
}

PasswordBroker::~PasswordBroker() { stop(); }

QString PasswordBroker::helperPath() {
#ifdef Q_OS_WIN
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("8bitShell-askpass.exe"));
#else
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("8bitShell-askpass"));
#endif
}

std::expected<QProcessEnvironment, QString>
PasswordBroker::start(const QVariantMap& profile, const QByteArray& password) {
    stop();
    const auto host = unbracketed(profile.value(QStringLiteral("host")).toString());
    const auto user = profile.value(QStringLiteral("user")).toString();
    const auto connection = profile.value(QStringLiteral("connection")).toMap();
    if (profile.contains(QStringLiteral("psmp"))
        || !profile.value(QStringLiteral("proxyJump")).toString().isEmpty()
        || (!connection.isEmpty() && connection.value(QStringLiteral("mode")) != QStringLiteral("direct")))
        return std::unexpected(QStringLiteral("Saved passwords are available only for direct SSH connections. Enter the gateway password interactively."));
    if (!SshSecurity::safeHost(host) || user.isEmpty() || !SshSecurity::safeUser(user))
        return std::unexpected(QStringLiteral("A saved password requires a valid host and an explicit SSH username."));
    const auto validation = SshSecurity::validateProfile(profile);
    if (!validation.isEmpty()) return std::unexpected(validation);
    if (password.isEmpty() || password.size() > maximumPasswordBytes
        || password.contains('\0') || password.contains('\r') || password.contains('\n'))
        return std::unexpected(QStringLiteral("The saved password is empty, too long or contains unsupported line breaks."));
    if (!QFileInfo(helperPath()).isExecutable())
        return std::unexpected(QStringLiteral("The packaged SSH password helper is missing. Reinstall 8bitShell before using a saved password."));

    std::array<quint32, 8> random{};
    QRandomGenerator::system()->generate(random.begin(), random.end());
    m_token = QByteArray(reinterpret_cast<const char*>(random.data()), sizeof(random)).toHex();
    random.fill(0);
    const auto socketName = QStringLiteral("8bitshell-password-") + QUuid::createUuid().toString(QUuid::Id128);
    if (!m_server.listen(socketName)) {
        const auto reason = m_server.errorString();
        stop();
        return std::unexpected(QStringLiteral("Cannot create the private SSH password channel: %1").arg(reason));
    }
    // Copy into broker-owned storage so wiping it never mutates the caller's
    // input. OpenSSH creates this prompt itself for password authentication;
    // keyboard-interactive/MFA and key-passphrase prompts are not accepted.
    m_password = QByteArray(password.constData(), password.size());
    const auto addPrompt = [this, &user](const QString& name) {
        const auto prompt = user + u'@' + name + QStringLiteral("'s password: ");
        if (!m_expectedPrompts.contains(prompt)) m_expectedPrompts.append(prompt);
    };
    addPrompt(host);
    addPrompt(host.toLower()); // OpenSSH folds DNS names before authentication.
    // Every session gets a full minute, even if a test or caller shortened the timer.
    m_expiry.setInterval(sessionLifetimeMs);
    m_expiry.start();

    QProcessEnvironment environment;
    environment.insert(QStringLiteral("SSH_ASKPASS"), helperPath());
    environment.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    environment.insert(QStringLiteral("SSH_ASKPASS_PROMPT"), QString{});
    environment.insert(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET"), socketName);
    environment.insert(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"), QString::fromLatin1(m_token));
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    return environment;
}

void PasswordBroker::stop() {
    m_expiry.stop();
    m_server.close();
    wipe(m_password);
    wipe(m_token);
    m_expectedPrompts.clear();
    const auto clients = std::exchange(m_clients, {});
    for (auto* socket : clients) {
        disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
}

void PasswordBroker::acceptConnections() {
    while (m_server.hasPendingConnections()) {
        auto* socket = m_server.nextPendingConnection();
        if (!socket) break;
        if (m_password.isEmpty() || m_clients.size() >= 4) {
            socket->abort(); socket->deleteLater(); continue;
        }
        socket->setReadBufferSize(maximumRequestBytes + 1);
        m_clients.insert(socket);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readRequest(socket); });
        connect(socket, &QLocalSocket::disconnected, this, [this, socket] {
            m_clients.remove(socket);
            socket->deleteLater();
        });
        // A client that never completes its bounded request cannot hold a
        // channel open indefinitely or keep the secret alive past expiry.
        QTimer::singleShot(5000, socket, [this, socket] { if (m_clients.contains(socket)) reject(socket); });
        readRequest(socket);
    }
}

void PasswordBroker::reject(QLocalSocket* socket) {
    m_clients.remove(socket);
    disconnect(socket, nullptr, this, nullptr);
    socket->abort();
    socket->deleteLater();
}

void PasswordBroker::readRequest(QLocalSocket* socket) {
    if (!m_clients.contains(socket)) return;
    if (m_password.isEmpty() || socket->bytesAvailable() > maximumRequestBytes) { reject(socket); return; }
    if (!socket->canReadLine()) return;
    auto bytes = socket->readLine(maximumRequestBytes + 1);
    if (socket->bytesAvailable() != 0 || !bytes.endsWith('\n')) { wipe(bytes); reject(socket); return; }
    QJsonParseError parseError{};
    const auto document = QJsonDocument::fromJson(bytes, &parseError);
    wipe(bytes);
    const auto object = document.object();
    const auto token = object.value(QStringLiteral("token")).toString().toLatin1();
    const auto prompt = object.value(QStringLiteral("prompt")).toString();
    if (parseError.error != QJsonParseError::NoError || !document.isObject() || object.size() != 2
        || !sameToken(token, m_token) || !m_expectedPrompts.contains(prompt)) {
        reject(socket); return;
    }

    auto answer = QByteArray(m_password.constData(), m_password.size());
    answer.append('\n');
    // Retire the capability before writing. Concurrent and replaying clients
    // cannot receive a second response, including during asynchronous flush.
    m_clients.remove(socket);
    disconnect(socket, nullptr, this, nullptr);
    stop();
    connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    socket->write(answer);
    wipe(answer);
    socket->disconnectFromServer();
    QTimer::singleShot(5000, socket, [socket] { socket->abort(); socket->deleteLater(); });
}

} // namespace bitshell::core
