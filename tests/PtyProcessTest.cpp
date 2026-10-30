#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <fcntl.h>
#include <unistd.h>

#include <bitshell/core/SecureMemory.h>
#include <bitshell/terminal/PtyProcess.h>

namespace bitshell::terminal {
// Exposes the PTY write queue: every byte of its block, spare capacity included.
class PtyProcessTestAccess {
public:
    static QByteArray queueBlock(const PtyProcess& p) { return core::allocatedBytes(p.m_writeBuffer).toByteArray(); }
    static qsizetype queued(const PtyProcess& p) { return p.m_writeBuffer.size(); }
};
}

using bitshell::terminal::PtyProcess;
using bitshell::terminal::PtyProcessTestAccess;

class PtyProcessTest final : public QObject {
    Q_OBJECT

private slots:
    void exchangesInteractiveDataAndResizes();
    void doesNotInheritApplicationDescriptors();
    void rejectsInvalidArgumentsAndBoundsInput();
    void interactiveBashFlushesHistoryAtEveryPrompt();
    void writtenSecretLeavesNoBytesInTheQueue();
    void stoppedSecretLeavesNoBytesInTheQueue();
    void startingDirectoryIsPerProcess() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto original = QDir::currentPath();
        PtyProcess process; QByteArray output; QString error;
        connect(&process, &PtyProcess::dataReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY2(process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), QStringLiteral("pwd")},
            120, 24, &error, directory.path()), qPrintable(error));
        QTRY_VERIFY(output.contains(directory.path().toUtf8()));
        QCOMPARE(QDir::currentPath(), original);
        QVERIFY(!process.start(QStringLiteral("/bin/sh"), {}, 80, 24, &error, directory.filePath(QStringLiteral("missing"))));
    }
};

void PtyProcessTest::exchangesInteractiveDataAndResizes() {
    PtyProcess process;
    process.setEraseCharacter('\x08');
    QCOMPARE(process.eraseCharacter(), '\x08');
    QSignalSpy dataSpy(&process, &PtyProcess::dataReceived);
    QSignalSpy finishedSpy(&process, &PtyProcess::finished);

    QString error;
    QVERIFY2(process.start(QStringLiteral("/bin/sh"),
                           {QStringLiteral("-c"),
                            QStringLiteral("read value; printf 'reply:%s\\n' \"$value\"")},
                           80,
                           24,
                           &error),
             qPrintable(error));
    QVERIFY(process.isRunning());

    process.resize(132, 43);
    process.write(QByteArrayLiteral("hello-from-pty\n"));

    QByteArray output;
    QElapsedTimer timer;
    timer.start();
    while (!output.contains(QByteArrayLiteral("reply:hello-from-pty"))
           && timer.elapsed() < 2000) {
        if (dataSpy.isEmpty()) {
            dataSpy.wait(100);
        }
        while (!dataSpy.isEmpty()) {
            output += dataSpy.takeFirst().at(0).toByteArray();
        }
    }
    QVERIFY2(output.contains(QByteArrayLiteral("reply:hello-from-pty")), output.constData());
    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty(), 2000);
    QVERIFY(!process.isRunning());
}

void PtyProcessTest::doesNotInheritApplicationDescriptors() {
    const int raw = ::open("/dev/null", O_RDONLY);
    QVERIFY(raw >= 0);
    const int sentinel = ::fcntl(raw, F_DUPFD, 50); // deliberately not CLOEXEC
    ::close(raw);
    QVERIFY(sentinel >= 50);
    PtyProcess process;
    QByteArray output;
    connect(&process, &PtyProcess::dataReceived, this, [&output](const QByteArray& bytes) { output += bytes; });
    QSignalSpy finished(&process, &PtyProcess::finished);
#ifdef Q_OS_DARWIN
    const QString descriptorRoot = QStringLiteral("/dev/fd");
#else
    const QString descriptorRoot = QStringLiteral("/proc/self/fd");
#endif
    const QString probe = QStringLiteral("if [ -e %1/%2 ]; then printf LEAKED; else printf CLOSED; fi; exit 23")
                              .arg(descriptorRoot).arg(sentinel);
    QString error;
    const bool started = process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), probe}, 80, 24, &error);
    ::close(sentinel);
    QVERIFY2(started, qPrintable(error));
    QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty(), 3000);
    QVERIFY(output.contains("CLOSED")); QVERIFY(!output.contains("LEAKED"));
    QCOMPARE(finished.first().first().toInt(), 23);
}
void PtyProcessTest::rejectsInvalidArgumentsAndBoundsInput() {
    PtyProcess process;
    QString error;
    QVERIFY(!process.start(QStringLiteral("sh"), {}, 80, 24, &error));
    QVERIFY(!process.start(QStringLiteral("/bin/sh"), {QStringLiteral("safe") + QChar::Null + QStringLiteral("hidden")}, 80, 24, &error));
    QVERIFY(process.start(QStringLiteral("/bin/sleep"), {QStringLiteral("1")}, 80, 24, &error));
    QSignalSpy errors(&process, &PtyProcess::errorOccurred);
    process.write(QByteArray(2 * 1024 * 1024, 'a'));
    QCOMPARE(errors.size(), 1);
    QVERIFY(errors.first().first().toString().contains(QStringLiteral("queue limit")));
}

namespace {
// Bash before 4.4 does not let "history -a" append while every history line
// came from the current session, so a fresh HISTFILE stays empty until exit.
// macOS ships bash 3.2 as /bin/bash; Homebrew installs a current one.
QString bashWithPromptHistoryAppend(QStringList& rejected) {
    const QStringList candidates{QStandardPaths::findExecutable(QStringLiteral("bash")),
                                 QStringLiteral("/opt/homebrew/bin/bash"),
                                 QStringLiteral("/usr/local/bin/bash"),
                                 QStringLiteral("/bin/bash")};
    for (const QString& candidate : candidates) {
        if (candidate.isEmpty() || !QFileInfo(candidate).isExecutable()) continue;
        QProcess probe;
        probe.start(candidate, {QStringLiteral("-c"),
                                QStringLiteral("printf '%s %s' \"${BASH_VERSINFO[0]}\" \"${BASH_VERSINFO[1]}\"")});
        if (!probe.waitForFinished(3000)) continue;
        const QList<QByteArray> version = probe.readAllStandardOutput().split(' ');
        if (version.size() != 2) continue;
        const int major = version[0].toInt();
        const int minor = version[1].toInt();
        if (major > 4 || (major == 4 && minor >= 4)) return candidate;
        rejected << QStringLiteral("%1 is %2.%3").arg(candidate).arg(major).arg(minor);
    }
    return {};
}
}  // namespace

void PtyProcessTest::interactiveBashFlushesHistoryAtEveryPrompt() {
    QStringList rejected;
    const QString bash = bashWithPromptHistoryAppend(rejected);
    if (bash.isEmpty()) {
        const QByteArray reason = "Needs bash 4.4 or newer (PATH, /opt/homebrew/bin, /usr/local/bin, /bin); "
            "older bash does not append history at the prompt for a new history file. Found: "
            + rejected.join(QStringLiteral(", ")).toUtf8();
        QSKIP(reason.constData());
    }
    qInfo("Using %s", qPrintable(bash));
    QTemporaryDir home;
    QVERIFY(home.isValid());
    const QByteArray oldHome = qgetenv("HOME");
    const QByteArray oldHistory = qgetenv("HISTFILE");
    const auto restore = qScopeGuard([&] {
        if (oldHome.isNull()) qunsetenv("HOME"); else qputenv("HOME", oldHome);
        if (oldHistory.isNull()) qunsetenv("HISTFILE"); else qputenv("HISTFILE", oldHistory);
    });
    const QString historyPath = home.filePath(QStringLiteral("shared-history"));
    qputenv("HOME", home.path().toUtf8());
    qputenv("HISTFILE", historyPath.toUtf8());
    qputenv("HISTSIZE", "1000");
    qputenv("HISTFILESIZE", "1000");

    PtyProcess process;
    process.setInteractiveShell(true);
    QString error;
    QVERIFY2(process.start(bash,
                           {QStringLiteral("--noprofile"), QStringLiteral("--norc"),
                            QStringLiteral("-i")},
                           80, 24, &error),
             qPrintable(error));
    process.write(QByteArrayLiteral("printf BIT8SHELL_HISTORY_SYNC_OK\\n\n"));

    QByteArray history;
    QElapsedTimer timer;
    timer.start();
    while (!history.contains("BIT8SHELL_HISTORY_SYNC_OK") && timer.elapsed() < 3000) {
        QTest::qWait(40);
        QFile file(historyPath);
        if (file.open(QIODevice::ReadOnly)) history = file.readAll();
    }
    QVERIFY2(history.contains("BIT8SHELL_HISTORY_SYNC_OK"), history.constData());
    process.stop();
}

namespace {
const QByteArray ptySecret = "TOPSECRET-xyz\r";
bool holdsSecret(const QByteArray& block) { return block.contains("TOPSECRET") || block.contains("SECRET-xyz"); }
// A child that reads nothing for a moment, so input queues (EAGAIN), then
// drains everything.
bool startStalledReader(PtyProcess& process, QString& error) {
    return process.start(QStringLiteral("/bin/sh"),
        {QStringLiteral("-c"), QStringLiteral("stty raw -echo; sleep 1; exec cat >/dev/null")}, 80, 24, &error);
}
}

// QC B3: a secret that waits in the queue behind a paste and drains in
// partial writes leaves none of its bytes in the queue's block.
void PtyProcessTest::writtenSecretLeavesNoBytesInTheQueue() {
    PtyProcess process; QString error;
    QVERIFY2(startStalledReader(process, error), qPrintable(error));
    process.write(QByteArray(512 * 1024, 'a'));
    QVERIFY(PtyProcessTestAccess::queued(process) > 0); // the kernel buffer filled: EAGAIN
    process.writeSecret(ptySecret);
    process.write(QByteArray(64 * 1024, 'b')); // typed after it, so the secret drains mid-queue
    QVERIFY(holdsSecret(PtyProcessTestAccess::queueBlock(process))); // the hook sees it while queued
    QTRY_COMPARE_WITH_TIMEOUT(PtyProcessTestAccess::queued(process), 0, 10000);
    const QByteArray block = PtyProcessTestAccess::queueBlock(process);
    QVERIFY(!holdsSecret(block));
    process.stop();
    QVERIFY(!holdsSecret(PtyProcessTestAccess::queueBlock(process)));
}

// QC B3: stopping with the secret still queued zeroes it too.
void PtyProcessTest::stoppedSecretLeavesNoBytesInTheQueue() {
    PtyProcess process; QString error;
    QVERIFY2(startStalledReader(process, error), qPrintable(error));
    process.write(QByteArray(512 * 1024, 'a'));
    process.writeSecret(ptySecret);
    QVERIFY(holdsSecret(PtyProcessTestAccess::queueBlock(process)));
    process.stop();
    QCOMPARE(PtyProcessTestAccess::queued(process), 0);
    const QByteArray block = PtyProcessTestAccess::queueBlock(process);
    QVERIFY(!block.isEmpty()); // the block was kept, zeroed, not freed unzeroed
    QVERIFY(!holdsSecret(block));
}

QTEST_GUILESS_MAIN(PtyProcessTest)

#include "PtyProcessTest.moc"
