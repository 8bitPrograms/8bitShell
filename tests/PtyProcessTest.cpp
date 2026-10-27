#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <fcntl.h>
#include <unistd.h>

#include <bitshell/terminal/PtyProcess.h>

using bitshell::terminal::PtyProcess;

class PtyProcessTest final : public QObject {
    Q_OBJECT

private slots:
    void exchangesInteractiveDataAndResizes();
    void doesNotInheritApplicationDescriptors();
    void rejectsInvalidArgumentsAndBoundsInput();
    void interactiveBashFlushesHistoryAtEveryPrompt();
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

void PtyProcessTest::interactiveBashFlushesHistoryAtEveryPrompt() {
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
    QVERIFY2(process.start(QStringLiteral("/bin/bash"),
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

QTEST_GUILESS_MAIN(PtyProcessTest)

#include "PtyProcessTest.moc"
