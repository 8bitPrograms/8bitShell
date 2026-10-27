#include <QtTest>
#include <QDir>
#include <QFile>
#include <QStringConverter>
#include <QTemporaryDir>
#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/Platform.h>
using namespace bitshell::core;

class LocalProfileStoreTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
private slots:
    void init() {
        QVERIFY(root.isValid());
        qputenv("XDG_CONFIG_HOME", root.filePath(QString::fromLatin1(QTest::currentTestFunction())).toUtf8());
    }
    void savesDefaultCustomAndDetectedOverrides() {
        LocalProfileStore store;
        const auto detected = store.defaultProfile();
        QVERIFY(!detected.isEmpty());
        auto value = detected;
        value[QStringLiteral("id")] = QString{};
        value[QStringLiteral("name")] = QStringLiteral("Projects ç");
        value[QStringLiteral("workingDirectory")] = root.path();
        value[QStringLiteral("arguments")] = QVariantList{QStringLiteral("literal with spaces"), QStringLiteral("a\"b\\"), QStringLiteral("$(not-executed)&text")};
        const auto id = store.saveProfile(value);
        QVERIFY2(!id.isEmpty(), qPrintable(store.lastError()));
        QVERIFY(store.setDefaultProfile(id));
        LocalProfileStore loaded;
        QCOMPARE(loaded.defaultProfileId(), id);
        QCOMPARE(loaded.defaultProfile().value(QStringLiteral("workingDirectory")).toString(), root.path());
        QString error;
        const auto launch = LocalProfileStore::prepareLaunch(loaded.defaultProfile(), error);
        QVERIFY2(!launch.isEmpty(), qPrintable(error));
        QCOMPARE(launch.value(QStringLiteral("arguments")).toStringList(), value.value(QStringLiteral("arguments")).toStringList());
        auto override = detected;
        override[QStringLiteral("workingDirectory")] = root.path();
        QVERIFY(!loaded.saveProfile(override).isEmpty());
        QCOMPARE(loaded.profile(QStringLiteral("system")).value(QStringLiteral("workingDirectory")).toString(), root.path());
        QVERIFY(loaded.removeProfile(QStringLiteral("system")));
        QCOMPARE(loaded.profile(QStringLiteral("system")).value(QStringLiteral("workingDirectory")).toString(), QString{});
        QVERIFY(loaded.removeProfile(id));
        QCOMPARE(loaded.defaultProfileId(), QStringLiteral("system"));
    }
    void rejectsInvalidLaunchAndPreservesExternalEdits() {
        LocalProfileStore store;
        auto profile = store.defaultProfile();
        const auto id = store.saveProfile(profile);
        QVERIFY2(!id.isEmpty(), qPrintable(store.lastError()));
        auto invalid = profile;
        invalid[QStringLiteral("workingDirectory")] = root.filePath(QStringLiteral("missing folder"));
        QVERIFY(store.saveProfile(invalid).isEmpty());
        invalid = profile;
        invalid[QStringLiteral("arguments")] = QVariantList{QString(QChar::Null)};
        QVERIFY(store.saveProfile(invalid).isEmpty());
        invalid = profile;
        invalid[QStringLiteral("program")] = QStringLiteral("missing-bitshell-test-program.exe");
        QVERIFY(store.saveProfile(invalid).isEmpty());
        QFile file(store.storagePath());
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write("\n# external edit\n"); file.close();
        QVERIFY(!store.setDefaultProfile(id));
        QVERIFY(store.lastError().contains(QStringLiteral("changed outside")));
        QVERIFY(store.reload());
        QVERIFY(store.setDefaultProfile(id));
        for (const auto& profiles : {QByteArray("wrong"), QByteArray("[wrong]")}) {
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            const QByteArray broken = "version: 1\nkind: 8bitshell/local-profiles\nprofiles: " + profiles + "\ndefaultProfile: system\n";
            file.write(broken); file.close();
            QVERIFY(!store.reload());
            QVERIFY(!store.lastError().isEmpty());
            QVERIFY(store.saveProfile(profile).isEmpty());
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), broken);
            file.close();
        }
    }
    void listsCommandPromptAndWindowsPowerShell() {
#ifdef Q_OS_WIN
        LocalProfileStore store;
        QStringList described;
        for (const auto& value : store.profiles()) {
            const auto profile = value.toMap();
            described << profile.value(QStringLiteral("name")).toString()
                          + QStringLiteral(" [") + profile.value(QStringLiteral("program")).toString() + QStringLiteral("]");
        }
        const auto joined = described.join(QStringLiteral(" | "));
        QVERIFY2(joined.contains(QStringLiteral("Windows PowerShell")), qPrintable(joined));
        QVERIFY2(joined.contains(QStringLiteral("Command Prompt")), qPrintable(joined));
#endif
    }
    void parsesWslUnicodeWithoutLocalizedColumns() {
        const QString text = QStringLiteral("Ubuntu\r\nFedora Linux\r\nTürkçe\r\nUbuntu\r\n");
        const QStringList expected{QStringLiteral("Ubuntu"), QStringLiteral("Fedora Linux"), QStringLiteral("Türkçe")};
        QCOMPARE(LocalProfileStore::distributionNames(text.toUtf8()), expected);
        QStringEncoder encoder(QStringEncoder::Utf16LE);
        QCOMPARE(LocalProfileStore::distributionNames(QByteArray("\xff\xfe") + encoder(text)), expected);
        QVERIFY(LocalProfileStore::distributionNames(QByteArray(65537, 'x')).isEmpty());
    }
    void wslUsesLinuxDirectoryAsLiteralArgument() {
#ifdef Q_OS_WIN
        LocalProfileStore store;
        auto profile = store.profile(QStringLiteral("wsl:Test Distro"));
        profile[QStringLiteral("workingDirectory")] = QStringLiteral("/home/test/projects with spaces");
        QString error;
        if (!QFileInfo::exists(Platform::windowsSystemDirectory() + QStringLiteral("/wsl.exe"))) QSKIP("WSL launcher is not installed");
        const auto launch = LocalProfileStore::prepareLaunch(profile, error);
        QVERIFY2(!launch.isEmpty(), qPrintable(error));
        QCOMPARE(launch.value(QStringLiteral("arguments")).toStringList(),
            QStringList({QStringLiteral("--distribution"), QStringLiteral("Test Distro"), QStringLiteral("--cd"), QStringLiteral("/home/test/projects with spaces")}));
        QVERIFY(launch.value(QStringLiteral("workingDirectory")).toString().isEmpty());
#endif
    }
};
QTEST_GUILESS_MAIN(LocalProfileStoreTest)
#include "LocalProfileStoreTest.moc"
