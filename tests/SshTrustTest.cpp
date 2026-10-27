#include <QtTest>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QProcess>
#include <QFileInfo>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>
#include <pwd.h>
#include <unistd.h>

using namespace bitshell::core;

// Real OpenSSH, ephemeral loopback listener and keys. No production hosts,
// ~/.ssh files, ssh-agent keys, passwords or system configuration are used.
class SshTrustTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory{QCoreApplication::applicationDirPath() + QStringLiteral("/ssh-trust-XXXXXX")};
    QProcess server;
    quint16 port = 0;
    QString user, serverProgram, hostKey, clientKey, wrongKey, knownHosts;
    struct Result { int code; QByteArray output; QByteArray error; };
    bool startServer(const QString& extra = {});
    Result connectClient(bool jump = false, SshSecurity::Transport transport = SshSecurity::Transport::Terminal);
    bool trust(const QString& publicKey);
private slots:
    void initTestCase();
    void unknownHostFailsClosed();
    void changedHostKeyFailsClosed();
    void verifiedHostAuthenticates();
    void sftpAuthenticatesWithVerifiedHost();
    void legacyCipherOnlyServerIsRejected();
    void jumpAlsoUsesManagedPolicy();
    void cleanupTestCase();
};

void SshTrustTest::initTestCase() {
    for (const auto& candidate : {QStringLiteral("/usr/bin/sshd"), QStringLiteral("/usr/sbin/sshd")})
        if (QFileInfo(candidate).isExecutable()) { serverProgram = candidate; break; }
    if (serverProgram.isEmpty()) QSKIP("Local integration prerequisite: OpenSSH server executable is not installed.");
    QVERIFY(directory.isValid());
    const auto* account = ::getpwuid(::getuid());
    QVERIFY(account != nullptr); user = QString::fromLocal8Bit(account->pw_name);
    hostKey = directory.filePath(QStringLiteral("host"));
    clientKey = directory.filePath(QStringLiteral("client"));
    wrongKey = directory.filePath(QStringLiteral("other-host"));
    knownHosts = directory.filePath(QStringLiteral("known_hosts"));
    for (const auto& path : {hostKey, clientKey, wrongKey}) {
        QProcess keygen;
        keygen.start(QStringLiteral("/usr/bin/ssh-keygen"), {QStringLiteral("-q"), QStringLiteral("-t"),
            QStringLiteral("ed25519"), QStringLiteral("-N"), QString{}, QStringLiteral("-f"), path});
        QVERIFY(keygen.waitForFinished(5000)); QCOMPARE(keygen.exitCode(), 0);
    }
    QByteArray publicKey; QString error;
    QVERIFY(PrivateFile::read(clientKey + QStringLiteral(".pub"), publicKey, error));
    QVERIFY(PrivateFile::write(directory.filePath(QStringLiteral("authorized_keys")), publicKey, error));
    QVERIFY(startServer());
}
bool SshTrustTest::startServer(const QString& extra) {
    if (server.state() != QProcess::NotRunning) { server.kill(); server.waitForFinished(2000); }
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) return false;
    port = reservation.serverPort(); reservation.close();
    const QString config = QStringLiteral(
        "ListenAddress 127.0.0.1\nPort %1\nHostKey %2\nPidFile %3\nAuthorizedKeysFile %4\n"
        "UsePAM no\nStrictModes yes\nPubkeyAuthentication yes\nPasswordAuthentication no\n"
        "KbdInteractiveAuthentication no\nPermitRootLogin no\nAllowUsers %5\nLogLevel VERBOSE\n"
        "AllowTcpForwarding yes\nX11Forwarding no\nSubsystem sftp internal-sftp\n")
        .arg(port).arg(hostKey, directory.filePath(QStringLiteral("pid")),
                      directory.filePath(QStringLiteral("authorized_keys")), user) + extra;
    QString error;
    const QString configPath = directory.filePath(QStringLiteral("sshd_config"));
    if (!PrivateFile::write(configPath, config.toUtf8(), error)) return false;
    server.start(serverProgram, {QStringLiteral("-D"), QStringLiteral("-e"), QStringLiteral("-f"), configPath});
    if (!server.waitForStarted(2000)) return false;
    QElapsedTimer timer; timer.start();
    while (timer.elapsed() < 3000) {
        QTcpSocket socket; socket.connectToHost(QHostAddress::LocalHost, port);
        if (socket.waitForConnected(100)) return true;
        if (server.state() == QProcess::NotRunning) {
            qWarning().noquote() << QString::fromUtf8(server.readAllStandardError()); return false;
        }
        QTest::qWait(10);
    }
    qWarning().noquote() << QString::fromUtf8(server.readAllStandardError());
    return false;
}
SshTrustTest::Result SshTrustTest::connectClient(bool jump, SshSecurity::Transport transport) {
    QVariantMap profile{{QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("user"), user}, {QStringLiteral("port"), port}, {QStringLiteral("identityFile"), clientKey}};
    if (jump) {
        profile[QStringLiteral("host")] = QStringLiteral("127.0.0.2");
        profile[QStringLiteral("proxyJump")] = user + QStringLiteral("@127.0.0.1:") + QString::number(port);
    }
    const auto command = SshSecurity::command(profile, transport);
    if (!command) return {-1, {}, command.error().toUtf8()};
    auto args = command->arguments;
    // Trust and identity paths are redirected only in this native test. These
    // options do not weaken host authentication, algorithms or the app policy.
    const QStringList isolation{QStringLiteral("-vv"), QStringLiteral("-oBatchMode=yes"),
        QStringLiteral("-oUserKnownHostsFile=") + knownHosts, QStringLiteral("-oGlobalKnownHostsFile=/dev/null"),
        QStringLiteral("-oIdentityAgent=none"), QStringLiteral("-oIdentitiesOnly=yes")};
    args = isolation + args;
    if (transport != SshSecurity::Transport::Sftp) args << QStringLiteral("printf BIT8SHELL_VERIFIED");
    QProcess client; client.setProcessEnvironment(SshSecurity::environment());
    client.start(command->program, args);
    if (transport == SshSecurity::Transport::Sftp) { client.write("pwd\nquit\n"); client.closeWriteChannel(); }
    if (!client.waitForFinished(8000)) { client.kill(); client.waitForFinished(1000); return {-1, {}, "Client timeout"}; }
    return {client.exitCode(), client.readAllStandardOutput(), client.readAllStandardError()};
}
bool SshTrustTest::trust(const QString& publicKey) {
    QByteArray bytes; QString error;
    return PrivateFile::read(publicKey, bytes, error)
        && PrivateFile::write(knownHosts, "[127.0.0.1]:" + QByteArray::number(port) + " " + bytes, error);
}
void SshTrustTest::unknownHostFailsClosed() {
    const auto result = connectClient();
    QCOMPARE(result.code, 255); QVERIFY(!result.output.contains("BIT8SHELL_VERIFIED"));
    QVERIFY2(result.error.contains("Host key verification failed"), result.error.constData());
    QVERIFY(!QFileInfo::exists(knownHosts));
    const auto sftp = connectClient(false, SshSecurity::Transport::Sftp);
    QVERIFY(sftp.code != 0);
    QVERIFY2(sftp.error.contains("Host key verification failed"), sftp.error.constData());
    QVERIFY(!QFileInfo::exists(knownHosts));
}
void SshTrustTest::changedHostKeyFailsClosed() {
    QVERIFY(trust(wrongKey + QStringLiteral(".pub")));
    const auto result = connectClient();
    QCOMPARE(result.code, 255); QVERIFY(!result.output.contains("BIT8SHELL_VERIFIED"));
    QVERIFY2(result.error.contains("REMOTE HOST IDENTIFICATION HAS CHANGED"), result.error.constData());
}
void SshTrustTest::verifiedHostAuthenticates() {
    QVERIFY(trust(hostKey + QStringLiteral(".pub")));
    const auto result = connectClient();
    QVERIFY2(result.code == 0, (result.error.right(3000) + "\nSERVER: " + server.readAllStandardError()).constData());
    QCOMPARE(result.output, QByteArray("BIT8SHELL_VERIFIED"));
    QVERIFY(result.error.contains("Authenticated to"));
}
void SshTrustTest::sftpAuthenticatesWithVerifiedHost() {
    QVERIFY(trust(hostKey + QStringLiteral(".pub")));
    const auto result = connectClient(false, SshSecurity::Transport::Sftp);
    QVERIFY2(result.code == 0, result.error.constData());
    QVERIFY2(result.output.contains("Remote working directory:"), result.output.constData());
}
void SshTrustTest::legacyCipherOnlyServerIsRejected() {
    QVERIFY(startServer(QStringLiteral("Ciphers aes128-cbc\n")));
    QVERIFY(trust(hostKey + QStringLiteral(".pub")));
    const auto result = connectClient();
    QCOMPARE(result.code, 255);
    QVERIFY2(result.error.contains("no matching cipher found"), result.error.constData());
    QVERIFY(startServer());
}
void SshTrustTest::jumpAlsoUsesManagedPolicy() {
    QVERIFY(trust(hostKey + QStringLiteral(".pub")));
    const auto result = connectClient(true);
    // The jump does not receive the test's destination-only trust override.
    // It must use -F, refuse this unknown ephemeral jump key and never prompt.
    QCOMPARE(result.code, 255);
    QVERIFY2(result.error.contains(SshSecurity::configurationPath().toUtf8()), result.error.constData());
    QVERIFY2(result.error.contains("Host key verification failed"), result.error.constData());
}
void SshTrustTest::cleanupTestCase() {
    if (server.state() != QProcess::NotRunning) { server.kill(); QVERIFY(server.waitForFinished(2000)); }
}
QTEST_GUILESS_MAIN(SshTrustTest)
#include "SshTrustTest.moc"
