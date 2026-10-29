#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QThread>
#include <bitshell/core/HostMonitor.h>
#include <bitshell/core/PasswordStore.h>
#include <bitshell/core/SftpController.h>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

using namespace bitshell::core;

namespace {
const QByteArray fixturePassword("8bit-auxiliary-password-fixture-$-only");
constexpr auto fixtureMode = "BIT8SHELL_PASSWORD_AUX_FIXTURE";
constexpr auto fixtureRecord = "BIT8SHELL_PASSWORD_AUX_RECORD";

// The test's transport executable is this program itself. It never invokes SSH
// or opens a remote socket; it speaks only to the packaged askpass helper.
int runFixture() {
#ifdef Q_OS_WIN
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    QFile record(qEnvironmentVariable(fixtureRecord));
    if (!record.open(QIODevice::WriteOnly | QIODevice::Append)) return 21;
    record.write(qgetenv("BIT8SHELL_ASKPASS_SOCKET") + '\n');
    record.close();
    const auto environment = QProcessEnvironment::systemEnvironment();
    if (environment.toStringList().join(u'\n').contains(QString::fromUtf8(fixturePassword))) return 22;
    if (QCoreApplication::arguments().join(u'\n').contains(QString::fromUtf8(fixturePassword))) return 23;
    QProcess helper;
    helper.setProcessEnvironment(environment);
    helper.start(environment.value(QStringLiteral("SSH_ASKPASS")),
                 {QStringLiteral("operator@files.example.test's password: ")});
    if (!helper.waitForStarted(3000) || !helper.waitForFinished(10000)) return 24;
    QByteArray answer = helper.readAllStandardOutput();
    const bool correct = helper.exitCode() == 0 && answer == fixturePassword + '\n';
    answer.fill('\0');
    if (!correct) return 25;
    QByteArray input;
    char buffer[2048];
    while (const auto size = std::fread(buffer, 1, sizeof(buffer), stdin)) input.append(buffer, qsizetype(size));
    if (input.contains(fixturePassword)) return 26;
    if (qgetenv(fixtureMode) == "reject") {
        std::fputs("Permission denied (password).\n", stderr);
        return 255;
    }
    if (qgetenv(fixtureMode) == "monitor") {
        const QByteArray frame("__BIT8SHELL_MONITOR_BEGIN__\nhost files.example.test\ncpu 100 20\nmemory 1024 512\nuptime 10\n__BIT8SHELL_MONITOR_END__\n");
        std::fwrite(frame.constData(), 1, size_t(frame.size()), stdout);
        std::fflush(stdout);
        QThread::msleep(20000); // Parent explicitly stops this fixture after the first sample.
    } else {
        const QByteArray listing("Remote working directory: /srv/data\n-rw-r--r-- 1 user group 12 Oct 29 10:00 fixture.txt\n");
        std::fwrite(listing.constData(), 1, size_t(listing.size()), stdout);
        std::fflush(stdout);
    }
    return 0;
}
}

class PasswordAuxiliaryTest final : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void missingPasswordNeverStartsAuxiliaryTransport();
    void savedPasswordReachesOnlyAskpassAndBatchRetiresBroker();
    void monitorUsesOneAuthenticationAndStopsCleanly();
    void failedMonitoringDoesNotRetryThePassword();
private:
    QVariantMap profile() const {
        return {{QStringLiteral("id"), QStringLiteral("password-aux-fixture")},
                {QStringLiteral("host"), QStringLiteral("files.example.test")},
                {QStringLiteral("user"), QStringLiteral("operator")},
                {QStringLiteral("port"), 22},
                {QStringLiteral("identityKey"), QStringLiteral("none")},
                {QStringLiteral("authenticationMode"), QStringLiteral("password")},
                {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("direct")}}}};
    }
    QString recordPath() const { return m_directory.filePath(QStringLiteral("transport-starts")); }
    QByteArray records() const {
        QFile record(recordPath());
        return record.open(QIODevice::ReadOnly) ? record.readAll() : QByteArray{};
    }
    bool socketRetired(const QString& name) const {
        if (name.isEmpty()) return false;
        QLocalSocket probe;
        probe.connectToServer(name);
        return !probe.waitForConnected(100);
    }
    QTemporaryDir m_directory;
};

void PasswordAuxiliaryTest::init() {
    QVERIFY(m_directory.isValid());
    const auto path = m_directory.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(path));
    qputenv("BIT8SHELL_PROFILE", path.toUtf8());
    qputenv("XDG_CONFIG_HOME", path.toUtf8());
    qputenv(fixtureMode, "sftp");
    qputenv(fixtureRecord, recordPath().toUtf8());
    QFile::remove(recordPath());
}
void PasswordAuxiliaryTest::cleanup() {
    const auto removed = PasswordStore::remove(profile());
    if (PasswordStore::has(profile())) QVERIFY2(removed.has_value(), qPrintable(removed.error()));
    qunsetenv(fixtureMode);
    qunsetenv(fixtureRecord);
}

void PasswordAuxiliaryTest::missingPasswordNeverStartsAuxiliaryTransport() {
    QVERIFY(!PasswordStore::has(profile()));
    SftpController files;
    files.setTransportExecutableForTesting(QCoreApplication::applicationFilePath());
    files.connectToProfile(profile());
    QCOMPARE(files.state(), SftpController::State::Error);
    QVERIFY(!files.connected());
    QVERIFY(!files.statusText().isEmpty());
    HostMonitor monitor;
    monitor.setTransportExecutableForTesting(QCoreApplication::applicationFilePath());
    monitor.setProfile(profile());
    monitor.setEnabled(true);
    QVERIFY(!monitor.status().isEmpty());
    QTest::qWait(100);
    QVERIFY(!QFileInfo::exists(recordPath()));
    monitor.setEnabled(false);
}

void PasswordAuxiliaryTest::savedPasswordReachesOnlyAskpassAndBatchRetiresBroker() {
    const auto saved = PasswordStore::write(profile(), fixturePassword);
    if (!saved) QSKIP(qPrintable(saved.error()));
    SftpController files;
    files.setTransportExecutableForTesting(QCoreApplication::applicationFilePath());
    files.connectToProfile(profile());
    const bool settled = QTest::qWaitFor([&files] {
        return files.state() == SftpController::State::Ready || files.state() == SftpController::State::Error;
    }, 15000);
    QVERIFY2(settled && files.state() == SftpController::State::Ready,
             qPrintable(QStringLiteral("state %1: %2").arg(int(files.state())).arg(files.statusText())));
    QVERIFY(files.connected());
    QCOMPARE(files.count(), 1);
    const auto starts = records().split('\n');
    QCOMPARE(starts.size(), 2);
    QVERIFY(socketRetired(QString::fromUtf8(starts.first())));
    QVERIFY(!files.statusText().contains(QString::fromUtf8(fixturePassword)));
    files.disconnectFromHost();
    QVERIFY(!files.connected());
}

void PasswordAuxiliaryTest::monitorUsesOneAuthenticationAndStopsCleanly() {
    const auto saved = PasswordStore::write(profile(), fixturePassword);
    if (!saved) QSKIP(qPrintable(saved.error()));
    qputenv(fixtureMode, "monitor");
    HostMonitor monitor;
    monitor.setTransportExecutableForTesting(QCoreApplication::applicationFilePath());
    monitor.setProfile(profile());
    monitor.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(monitor.metrics().contains(QStringLiteral("memoryPercent")), 15000);
    QCOMPARE(monitor.metrics().value(QStringLiteral("memoryPercent")).toDouble(), 50.0);
    QCOMPARE(records().split('\n').size(), 2);
    QVERIFY(!monitor.status().contains(QString::fromUtf8(fixturePassword)));
    monitor.setEnabled(false);
    QTRY_VERIFY(socketRetired(QString::fromUtf8(records().split('\n').first())));
    QTest::qWait(100);
    QCOMPARE(records().split('\n').size(), 2); // No automatic authentication retry.
}

void PasswordAuxiliaryTest::failedMonitoringDoesNotRetryThePassword() {
    const auto saved = PasswordStore::write(profile(), fixturePassword);
    if (!saved) QSKIP(qPrintable(saved.error()));
    qputenv(fixtureMode, "reject");
    HostMonitor monitor;
    monitor.setTransportExecutableForTesting(QCoreApplication::applicationFilePath());
    monitor.setProfile(profile());
    monitor.setEnabled(true);
    QTRY_VERIFY_WITH_TIMEOUT(monitor.status().contains(QStringLiteral("Monitoring stopped")), 15000);
    QVERIFY(monitor.metrics().isEmpty());
    QVERIFY(!monitor.status().contains(QString::fromUtf8(fixturePassword)));
    QTest::qWait(250);
    QCOMPARE(records().split('\n').size(), 2);
    QVERIFY(socketRetired(QString::fromUtf8(records().split('\n').first())));
    monitor.setEnabled(false);
}

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // A missing Qt DLL otherwise opens a modal loader dialog and holds the test until ctest kills it.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariableIsSet(fixtureMode)) return runFixture();
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("password-auxiliary-test"));
    PasswordAuxiliaryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "PasswordAuxiliaryTest.moc"
