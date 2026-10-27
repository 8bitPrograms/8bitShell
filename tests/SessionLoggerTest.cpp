#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <bitshell/core/SessionLogger.h>

using namespace bitshell::core;

class SessionLoggerTest : public QObject {
    Q_OBJECT

private slots:
    void writesPlainXshellStyleLog();
    void timestampsCompletedLinesOnlyWhenEnabled();
    void stripsAnsiSequencesAcrossChunks();
    void rejectsDirectoryTraversal();
    void stripsControlStringsButKeepsUtf8();
};

void SessionLoggerTest::writesPlainXshellStyleLog() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    SessionLogger logger;
    const LogOptions options{
        .rootDirectory = temporaryDirectory.path(),
        .directoryTemplate = QStringLiteral("{session}"),
        .fileNameTemplate = QStringLiteral("{host}_{date}.log"),
        .existingFileMode = ExistingLogMode::Truncate,
    };
    const LogContext context{
        .session = QStringLiteral("Production DB"),
        .host = QStringLiteral("db-prod-01"),
        .user = QStringLiteral("operator"),
        .startedAt = QDateTime(QDate(2026, 9, 11), QTime(12, 30, 0)),
    };

    QString error;
    QVERIFY2(logger.start(options, context, &error), qPrintable(error));
    QVERIFY2(logger.writeOutput(QByteArrayLiteral("connected\nready\n"), &error),
             qPrintable(error));
    const auto path = logger.filePath();
    logger.stop();

    QCOMPARE(QFileInfo(path).fileName(), QStringLiteral("db-prod-01_2026-09-11.log"));
    QVERIFY(path.contains(QStringLiteral("Production_DB")));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("connected\nready\n"));
}

void SessionLoggerTest::timestampsCompletedLinesOnlyWhenEnabled() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    SessionLogger logger;
    const LogOptions options{
        .rootDirectory = temporaryDirectory.path(),
        .directoryTemplate = QStringLiteral("."),
        .fileNameTemplate = QStringLiteral("timestamped.log"),
        .existingFileMode = ExistingLogMode::Truncate,
        .timestampMode = LogTimestampMode::EachLine,
    };

    QString error;
    QVERIFY2(logger.start(options, LogContext{.session = QStringLiteral("test")}, &error),
             qPrintable(error));
    QVERIFY2(logger.writeOutput(QByteArrayLiteral("first line\nsecond"), &error),
             qPrintable(error));
    QVERIFY2(logger.writeOutput(QByteArrayLiteral(" line\n"), &error), qPrintable(error));
    const auto path = logger.filePath();
    logger.stop();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray contents = file.readAll();
    const QList<QByteArray> lines = contents.split('\n');
    QCOMPARE(lines.size(), 3);
    QVERIFY(lines.at(0).contains("] first line"));
    QVERIFY(lines.at(1).contains("] second line"));
    QVERIFY(lines.at(0).startsWith("[20"));
    QVERIFY(lines.at(1).startsWith("[20"));
    QVERIFY(lines.at(2).isEmpty());
}

void SessionLoggerTest::stripsAnsiSequencesAcrossChunks() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    SessionLogger logger;
    const LogOptions options{
        .rootDirectory = temporaryDirectory.path(),
        .directoryTemplate = QStringLiteral("."),
        .fileNameTemplate = QStringLiteral("output.log"),
        .existingFileMode = ExistingLogMode::Truncate,
        .stripAnsi = true,
    };
    const LogContext context{.session = QStringLiteral("test")};

    QString error;
    QVERIFY2(logger.start(options, context, &error), qPrintable(error));
    const QByteArray first("\x1b[31", 4);
    const QByteArray second("mERROR\x1b[0m\n");
    QVERIFY2(logger.writeOutput(first, &error), qPrintable(error));
    QVERIFY2(logger.writeOutput(second, &error), qPrintable(error));
    const auto path = logger.filePath();
    logger.stop();

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("ERROR\n"));
}

void SessionLoggerTest::rejectsDirectoryTraversal() {
    QTemporaryDir temporaryDirectory;
    QVERIFY(temporaryDirectory.isValid());

    SessionLogger logger;
    const LogOptions options{
        .rootDirectory = temporaryDirectory.path(),
        .directoryTemplate = QStringLiteral("../outside"),
    };
    QString error;
    QVERIFY(!logger.start(options, LogContext{.session = QStringLiteral("test")}, &error));
    QVERIFY(error.contains(QStringLiteral("escapes")));
}

void SessionLoggerTest::stripsControlStringsButKeepsUtf8() {
    AnsiStripper stripper;
    QCOMPARE(stripper.filter(QByteArray("ok\x1bPprivate payload")), QByteArray("ok"));
    QCOMPARE(stripper.filter(QByteArray("\x1b\\\x1b_secret\x1b\\\x07\x7f\n")), QByteArray("\n"));
    const auto unicode = QStringLiteral("Türkçe 🫐\t\r\n").toUtf8();
    QCOMPARE(stripper.filter(unicode), unicode);
}

QTEST_APPLESS_MAIN(SessionLoggerTest)
#include "SessionLoggerTest.moc"
