#include <QtTest>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <bitshell/core/LocalProfileStore.h>

// Load the application UI with a harmless profile executable that records cwd.
// Covers startup properties, QML profiles and the PTY child without a user shell.
class StartupDirectoryTest : public QObject {
    Q_OBJECT
private slots:
    void profileDirectoryPrecedence_data() {
        QTest::addColumn<bool>("overrideDirectory");
        QTest::newRow("saved-profile-directory") << false;
        QTest::newRow("explicit-directory-with-spaces-and-unicode") << true;
    }
    void profileDirectoryPrecedence() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto profileRoot = root.filePath(QStringLiteral("profile"));
        const auto savedDirectory = root.filePath(QStringLiteral("saved default"));
        const auto requestedDirectory = root.filePath(QStringLiteral("selected \u00f6 \u0131 ; & % $ ( ) ! [ ]"));
        QVERIFY(QDir().mkpath(savedDirectory));
        QVERIFY(QDir().mkpath(requestedDirectory));
        const auto report = root.filePath(QStringLiteral("child-cwd.txt"));
        const auto previous = qgetenv("BIT8SHELL_PROFILE");
        const bool wasSet = qEnvironmentVariableIsSet("BIT8SHELL_PROFILE");
        const auto restore = qScopeGuard([&] {
            if (wasSet) qputenv("BIT8SHELL_PROFILE", previous);
            else qunsetenv("BIT8SHELL_PROFILE");
        });
        qputenv("BIT8SHELL_PROFILE", profileRoot.toUtf8());
        bitshell::core::LocalProfileStore store;
        auto profile = store.defaultProfile();
        profile[QStringLiteral("id")] = QString{};
        profile[QStringLiteral("name")] = QStringLiteral("Startup cwd fixture");
        profile[QStringLiteral("kind")] = QStringLiteral("local");
        profile[QStringLiteral("program")] = QCoreApplication::applicationFilePath();
        profile[QStringLiteral("arguments")] = QVariantList{QStringLiteral("--record-cwd"), report};
        profile[QStringLiteral("workingDirectory")] = savedDirectory;
        const auto id = store.saveProfile(profile);
        QVERIFY2(!id.isEmpty(), qPrintable(store.lastError()));
        QVERIFY2(store.setDefaultProfile(id), qPrintable(store.lastError()));

        QFETCH(bool, overrideDirectory);
        QQmlApplicationEngine engine;
        if (overrideDirectory)
            engine.setInitialProperties({{QStringLiteral("startupDirectory"), requestedDirectory}});
        engine.loadFromModule(QStringLiteral("BitShell"), QStringLiteral("Main"));
        QVERIFY(!engine.rootObjects().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(report), 10000);
        QFile recorded(report);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(recorded.readAll()), overrideDirectory ? requestedDirectory : savedDirectory);
        // Opening another normal local tab must still use the saved directory.
        recorded.close();
        QVERIFY(QFile::remove(report));
        auto* pane = engine.rootObjects().first()->findChild<QObject*>(QStringLiteral("terminalPane"));
        QVERIFY(pane);
        QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(report), 10000);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        QCOMPARE(QString::fromUtf8(recorded.readAll()), savedDirectory);
        QVERIFY(store.reload());
        QCOMPARE(store.defaultProfile().value(QStringLiteral("workingDirectory")).toString(), savedDirectory);
    }
};

int main(int argc, char** argv) {
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--record-cwd")) {
        QCoreApplication app(argc, argv);
        const auto args = app.arguments();
        QSaveFile file(args.at(2));
        const auto data = QDir::currentPath().toUtf8();
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit() ? 0 : 1;
    }
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    StartupDirectoryTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "StartupDirectoryTest.moc"
