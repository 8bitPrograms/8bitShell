#include <QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QUrl>

#include <bitshell/core/Platform.h>
#include <bitshell/core/SshKeyStore.h>
#include <bitshell/core/SshSecurity.h>

using namespace bitshell::core;

class SshKeyStoreTest final : public QObject {
    Q_OBJECT

private slots:
    void importsSelectsReplacesAndRemovesWithoutLeakingMaterial();

private:
    static bool generate(const QString& path, const QString& type,
                         const QString& bits = {});
    static bool convertToPpk(const QString& input, const QString& output,
                             const QString& passphrase = {});
};

bool SshKeyStoreTest::generate(const QString& path, const QString& type,
                               const QString& bits) {
    QStringList arguments{QStringLiteral("-q"), QStringLiteral("-t"), type,
                          QStringLiteral("-N"), QString{}, QStringLiteral("-f"), path};
    if (!bits.isEmpty()) arguments << QStringLiteral("-b") << bits;
    QProcess process;
    process.start(Platform::sshKeygenExecutable(), arguments, QIODevice::ReadOnly);
    const bool ok = process.waitForStarted(3000) && process.waitForFinished(20000)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!ok) qWarning().noquote() << "Fixture key generation failed:" << process.errorString()
                                  << process.readAllStandardError();
    return ok;
}

bool SshKeyStoreTest::convertToPpk(const QString& input, const QString& output,
                                   const QString& passphrase) {
    QStringList arguments{input};
    const QString passphrasePath = output + QStringLiteral(".passphrase");
    if (!passphrase.isEmpty()) {
        QFile passphraseFile(passphrasePath);
        if (!passphraseFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
            || passphraseFile.write(passphrase.toUtf8()) != passphrase.toUtf8().size()
            || !passphraseFile.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner))
            return false;
        passphraseFile.close();
        arguments << QStringLiteral("--new-passphrase") << passphrasePath;
    }
    arguments << QStringLiteral("-O") << QStringLiteral("private")
              << QStringLiteral("-o") << output;
    QProcess process;
    process.start(Platform::puttygenExecutable(), arguments, QIODevice::ReadOnly);
    const bool success = process.waitForStarted(3000) && process.waitForFinished(20000)
        && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (!passphrase.isEmpty()) (void)QFile::remove(passphrasePath);
    return success;
}

void SshKeyStoreTest::importsSelectsReplacesAndRemovesWithoutLeakingMaterial() {
    QTemporaryDir source;
    QVERIFY(source.isValid());
    const QString firstSource = source.filePath(QStringLiteral("daily-ed25519"));
    const QString cyberSource = source.filePath(QStringLiteral("cyber-ed25519"));
    const QString replacementSource = source.filePath(QStringLiteral("replacement-ed25519"));
    const QString weakSource = source.filePath(QStringLiteral("weak-rsa"));
    QVERIFY(generate(firstSource, QStringLiteral("ed25519")));
    QVERIFY(generate(cyberSource, QStringLiteral("ed25519")));
    QVERIFY(generate(replacementSource, QStringLiteral("ed25519")));
    QVERIFY(generate(weakSource, QStringLiteral("rsa"), QStringLiteral("2048")));
    const bool ppkReady = QFileInfo(Platform::puttygenExecutable()).isExecutable();
    const QString plainPpk = source.filePath(QStringLiteral("plain.ppk"));
    const QString encryptedPpk = source.filePath(QStringLiteral("encrypted.ppk"));
    const QString ppkSecret = QStringLiteral("one-time-ppk-secret");
    if (ppkReady) {
        QVERIFY(convertToPpk(firstSource, plainPpk));
        QVERIFY(convertToPpk(cyberSource, encryptedPpk, ppkSecret));
    }

    SshKeyStore store;
    QVERIFY2(store.lastError().isEmpty(), qPrintable(store.lastError()));
    const QString first = store.importKey(QUrl::fromLocalFile(firstSource).toString(),
                                          QStringLiteral("Daily key"));
    QVERIFY2(!first.isEmpty(), qPrintable(store.lastError()));
    QCOMPARE(first.size(), 32);
    const QString cyber = store.importKey(QUrl::fromLocalFile(cyberSource).toString(),
                                          QStringLiteral("CyberArk key"));
    QVERIFY2(!cyber.isEmpty(), qPrintable(store.lastError()));
    int expectedKeyCount = 2;
    if (ppkReady) {
        const QString plain = store.importKey(QUrl::fromLocalFile(plainPpk).toString(),
                                              QStringLiteral("Imported PPK"));
        QVERIFY2(!plain.isEmpty(), qPrintable(store.lastError()));
        QCOMPARE(store.get(plain).value(QStringLiteral("format")).toString(), QStringLiteral("ppk"));
        QVERIFY(store.importKey(QUrl::fromLocalFile(encryptedPpk).toString()).isEmpty());
        QVERIFY(store.lastError().contains(QStringLiteral("encrypted"), Qt::CaseInsensitive));
        const QString encrypted = store.importKey(QUrl::fromLocalFile(encryptedPpk).toString(),
                                                  QStringLiteral("Encrypted PPK"), ppkSecret);
        QVERIFY2(!encrypted.isEmpty(), qPrintable(store.lastError()));
        QCOMPARE(store.get(encrypted).value(QStringLiteral("format")).toString(), QStringLiteral("ppk"));
        expectedKeyCount += 2;
    }
    QCOMPARE(store.keys().size(), expectedKeyCount);
    QVERIFY(store.setDefaultKey(first));
    QVERIFY(store.setCyberArkKey(cyber));

    const QVariantMap firstEntry = store.get(first);
    const QString firstFile = firstEntry.value(QStringLiteral("file")).toString();
    const QString firstPath = QDir(Platform::appConfigDirectory())
                                  .filePath(QStringLiteral("keys/") + firstFile);
    QVERIFY(QFileInfo::exists(firstPath));
#ifndef Q_OS_WIN
    const auto exposed = QFileDevice::ReadGroup | QFileDevice::ReadOther
        | QFileDevice::WriteGroup | QFileDevice::WriteOther;
    QVERIFY(!(QFileInfo(firstPath).permissions() & exposed));
#endif

    const auto direct = SshKeyStore::applyDefaults(
        {{QStringLiteral("host"), QStringLiteral("direct.example.test")}});
    QVERIFY2(direct.has_value(), qPrintable(direct ? QString{} : direct.error()));
    QCOMPARE(direct->value(QStringLiteral("identityKey")).toString(), first);
    QCOMPARE(direct->value(QStringLiteral("identityFile")).toString(), firstPath);
    const auto directCommand = SshSecurity::command(
        {{QStringLiteral("host"), QStringLiteral("direct.example.test")}},
        SshSecurity::Transport::Terminal);
    QVERIFY2(directCommand.has_value(), qPrintable(directCommand ? QString{} : directCommand.error()));
    QVERIFY(directCommand->arguments.contains(QStringLiteral("-oIdentitiesOnly=yes")));
    QVERIFY(directCommand->arguments.contains(firstPath));

    const auto routed = SshKeyStore::applyDefaults(
        {{QStringLiteral("host"), QStringLiteral("target.example.test")},
         {QStringLiteral("identityKey"), first},
         {QStringLiteral("psmp"), QVariantMap{{QStringLiteral("route"), QVariantMap{}}}}});
    QVERIFY2(routed.has_value(), qPrintable(routed ? QString{} : routed.error()));
    QCOMPARE(routed->value(QStringLiteral("identityKey")).toString(), cyber);

    const auto ordinary = SshKeyStore::applyDefaults(
        {{QStringLiteral("host"), QStringLiteral("agent.example.test")},
         {QStringLiteral("identityKey"), QStringLiteral("none")}});
    QVERIFY(ordinary.has_value());
    QVERIFY(!ordinary->contains(QStringLiteral("identityFile")));
    QVERIFY(!SshKeyStore::applyDefaults(
        {{QStringLiteral("host"), QStringLiteral("missing.example.test")},
         {QStringLiteral("identityKey"), QString(32, u'f')}}));

    QFile metadata(store.storagePath());
    QVERIFY(metadata.open(QIODevice::ReadOnly));
    const QByteArray yaml = metadata.readAll();
    QVERIFY(yaml.contains(QByteArrayLiteral("Daily key")));
    QVERIFY(yaml.contains(QByteArrayLiteral("SHA256:")));
    QVERIFY(!yaml.contains(QByteArrayLiteral("BEGIN OPENSSH PRIVATE KEY")));
    QVERIFY(!yaml.contains(firstSource.toUtf8()));
    QVERIFY(!yaml.contains(ppkSecret.toUtf8()));
    metadata.close();

    const QString oldFingerprint = firstEntry.value(QStringLiteral("fingerprint")).toString();
    QVERIFY2(store.replaceKey(first, QUrl::fromLocalFile(replacementSource).toString()),
             qPrintable(store.lastError()));
    QCOMPARE(store.get(first).value(QStringLiteral("id")).toString(), first);
    QVERIFY(store.get(first).value(QStringLiteral("file")).toString() != firstFile);
    QVERIFY(store.get(first).value(QStringLiteral("fingerprint")).toString() != oldFingerprint);
    QVERIFY(!QFileInfo::exists(firstPath));
    QCOMPARE(store.defaultKeyId(), first);

    const int beforeRejected = store.keys().size();
    QVERIFY(store.importKey(QUrl::fromLocalFile(weakSource).toString()).isEmpty());
    QVERIFY(store.lastError().contains(QStringLiteral("3072")));
    QCOMPARE(store.keys().size(), beforeRejected);
    QVERIFY(store.importKey(QUrl::fromLocalFile(firstSource + QStringLiteral(".pub")).toString()).isEmpty());
    QVERIFY(store.lastError().contains(QStringLiteral("Public keys")));

    SshKeyStore reloaded;
    QVERIFY2(reloaded.lastError().isEmpty(), qPrintable(reloaded.lastError()));
    QCOMPARE(reloaded.keys().size(), expectedKeyCount);
    QCOMPARE(reloaded.defaultKeyId(), first);
    QCOMPARE(reloaded.cyberArkKeyId(), cyber);
    QVERIFY(reloaded.removeKey(cyber));
    QVERIFY(reloaded.cyberArkKeyId().isEmpty());
    QCOMPARE(reloaded.keys().size(), expectedKeyCount - 1);
}

int main(int argc, char** argv) {
    QTemporaryDir config;
    if (!config.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("ssh-key-store-test"));
    SshKeyStoreTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "SshKeyStoreTest.moc"
