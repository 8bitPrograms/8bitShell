#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibraryInfo>
#include <QProcess>
#include <QTemporaryDir>

class MultiInstanceTest : public QObject {
    Q_OBJECT
private slots:
    void twoProcessesShareProfileAndSurviveIndependentExit() {
        QTemporaryDir temporary(QDir::currentPath() + QStringLiteral("/instance-test-XXXXXX"));
        QVERIFY(temporary.isValid());
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("BIT8SHELL_PROFILE"), temporary.filePath(QStringLiteral("profile")));
        environment.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
        auto app = QCoreApplication::applicationDirPath() +
#ifdef Q_OS_WIN
            QStringLiteral("/8bitShell.exe");
#else
            QStringLiteral("/../8bitShell");
#endif
        QVERIFY(QFile::exists(app));
#ifdef Q_OS_WIN
        // The real executable deliberately removes PATH from DLL dependency
        // searches. Give the child a private runtime, as portable deployment
        // does, without weakening that startup policy for a developer SDK.
        const auto runtime = temporary.filePath(QStringLiteral("runtime"));
        QVERIFY(QDir().mkpath(runtime));
        const auto copiedApp = runtime + QStringLiteral("/8bitShell.exe");
        QVERIFY(QFile::copy(app, copiedApp));
        const QDir sdk(QLibraryInfo::path(QLibraryInfo::BinariesPath));
        for (const auto& dll : sdk.entryList({QStringLiteral("*.dll")},QDir::Files))
            QVERIFY(QFile::copy(sdk.filePath(dll),runtime + u'/' + dll));
        QFile qtconf(runtime + QStringLiteral("/qt.conf"));
        QVERIFY(qtconf.open(QIODevice::WriteOnly));
        qtconf.write((QStringLiteral("[Paths]\nPrefix=") + QLibraryInfo::path(QLibraryInfo::PrefixPath) + u'\n').toUtf8());
        qtconf.close();
        app = copiedApp;
#endif
        QProcess first, second;
        first.setProcessEnvironment(environment); second.setProcessEnvironment(environment);
        const auto arguments = [&](const QString& report, const QString& duration) {
            return QStringList{QStringLiteral("--performance-report"), temporary.filePath(report),
                QStringLiteral("--performance-tabs"), QStringLiteral("0"),
                QStringLiteral("--performance-duration"), duration};
        };
        first.start(app, arguments(QStringLiteral("first.json"), QStringLiteral("1500")));
        second.start(app, arguments(QStringLiteral("second.json"), QStringLiteral("5000")));
        QVERIFY(first.waitForStarted()); QVERIFY(second.waitForStarted());
        QVERIFY(first.processId() != second.processId());
        QTRY_VERIFY_WITH_TIMEOUT(first.state() == QProcess::NotRunning,10000);
        const auto diagnostics = QByteArray::number(first.exitCode()) + ": " + first.readAllStandardError() + first.readAllStandardOutput();
        QVERIFY2(first.exitCode() == 0, diagnostics.constData());
        QCOMPARE(second.state(),QProcess::Running);
        QTRY_VERIFY_WITH_TIMEOUT(second.state() == QProcess::NotRunning,10000);
        QVERIFY2(second.exitCode() == 0, second.readAllStandardError().constData());
        for (const auto& name : {QStringLiteral("first.json"),QStringLiteral("second.json")}) {
            QFile report(temporary.filePath(name));
            QVERIFY(report.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument::fromJson(report.readAll()).object().value(QStringLiteral("tabs")).toInt(),0);
        }
    }
};
QTEST_GUILESS_MAIN(MultiInstanceTest)
#include "MultiInstanceTest.moc"
