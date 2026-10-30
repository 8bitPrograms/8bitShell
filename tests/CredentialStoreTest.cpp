#include <QtTest>

#include <QFile>
#include <QTemporaryDir>
#include <bitshell/core/CredentialStore.h>

#include "FakeSecretVault.h"

using bitshell::core::CredentialStore;

namespace {
const QByteArray keyText = "-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAA\n-----END OPENSSH PRIVATE KEY-----\n";

QByteArray fileBytes(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}

class CredentialStoreTest final : public QObject {
    Q_OBJECT
private slots:
    void init() {
        QVERIFY(m_root.isValid());
        m_path = m_root.filePath(QString::fromLatin1(QTest::currentTestFunction()) + QStringLiteral("/credentials.yaml"));
        m_vault = FakeSecretVault{};
    }

    void passwordCredentialKeepsSecretOutOfYaml() {
        CredentialStore store(m_vault, m_path);
        const QString id = store.saveCredential({{QStringLiteral("name"), QStringLiteral("ldap_creds")},
            {QStringLiteral("type"), QStringLiteral("password")}, {QStringLiteral("username"), QStringLiteral("jdoe")},
            {QStringLiteral("password"), QStringLiteral("Tr0ub4dor&3")}});
        QVERIFY2(!id.isEmpty(), qPrintable(store.lastError()));
        const QByteArray yaml = fileBytes(m_path);
        QVERIFY(yaml.contains("ldap_creds"));
        QVERIFY(yaml.contains("keychain: credential/" + id.toUtf8()));
        QVERIFY(!yaml.contains("Tr0ub4dor"));
        QCOMPARE(m_vault.entries.value(QStringLiteral("credential/%1/password").arg(id)), QByteArray("Tr0ub4dor&3"));
        // Nothing visible to QML carries the secret.
        const auto row = store.get(id);
        QVERIFY(!QJsonDocument::fromVariant(row).toJson().contains("Tr0ub4dor"));
        QCOMPARE(row.value(QStringLiteral("username")).toString(), QStringLiteral("jdoe"));

        const auto resolved = CredentialStore::resolve(id, m_vault, m_path);
        QVERIFY2(resolved.has_value(), resolved ? "" : qPrintable(resolved.error()));
        QCOMPARE(resolved->secrets.username, QByteArray("jdoe"));
        QCOMPARE(resolved->secrets.password, QByteArray("Tr0ub4dor&3"));
    }

    void editingWithoutSecretKeepsIt() {
        CredentialStore store(m_vault, m_path);
        const QString id = store.saveCredential({{QStringLiteral("name"), QStringLiteral("a")},
            {QStringLiteral("type"), QStringLiteral("password")}, {QStringLiteral("username"), QStringLiteral("jdoe")},
            {QStringLiteral("password"), QStringLiteral("one")}});
        QVERIFY(!id.isEmpty());
        QCOMPARE(store.saveCredential({{QStringLiteral("id"), id}, {QStringLiteral("name"), QStringLiteral("renamed")},
            {QStringLiteral("username"), QStringLiteral("jane")}}), id);
        const auto resolved = CredentialStore::resolve(id, m_vault, m_path);
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->name, QStringLiteral("renamed"));
        QCOMPARE(resolved->secrets.username, QByteArray("jane"));
        QCOMPARE(resolved->secrets.password, QByteArray("one"));
        QVERIFY(store.saveCredential({{QStringLiteral("id"), id}, {QStringLiteral("name"), QStringLiteral("renamed")},
            {QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("username"), QStringLiteral("jane")}}).isEmpty());
    }

    void keyCredentialWithPassphrase() {
        const QString keyFile = m_root.filePath(QStringLiteral("id_test"));
        QFile file(keyFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(keyText);
        file.close();
        CredentialStore store(m_vault, m_path);
        const QString id = store.saveCredential({{QStringLiteral("name"), QStringLiteral("deploy key")},
            {QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("username"), QStringLiteral("deploy")},
            {QStringLiteral("keyFile"), QUrl::fromLocalFile(keyFile).toString()},
            {QStringLiteral("passphrase"), QStringLiteral("open sesame")}});
        QVERIFY2(!id.isEmpty(), qPrintable(store.lastError()));
        const QByteArray yaml = fileBytes(m_path);
        QVERIFY(!yaml.contains("OPENSSH PRIVATE KEY"));
        QVERIFY(!yaml.contains("sesame"));
        QVERIFY(store.get(id).value(QStringLiteral("passphrase")).toBool());
        auto resolved = CredentialStore::resolve(id, m_vault, m_path);
        QVERIFY(resolved.has_value());
        QCOMPARE(resolved->privateKey, keyText);
        QCOMPARE(resolved->secrets.passphrase, QByteArray("open sesame"));

        QVERIFY(!store.saveCredential({{QStringLiteral("id"), id}, {QStringLiteral("name"), QStringLiteral("deploy key")},
            {QStringLiteral("username"), QStringLiteral("deploy")}, {QStringLiteral("clearPassphrase"), true}}).isEmpty());
        QVERIFY(!m_vault.entries.contains(QStringLiteral("credential/%1/passphrase").arg(id)));
        resolved = CredentialStore::resolve(id, m_vault, m_path);
        QVERIFY(resolved.has_value());
        QVERIFY(resolved->secrets.passphrase.isEmpty());
    }

    void rejectsInvalidInput() {
        CredentialStore store(m_vault, m_path);
        const QVariantMap good{{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("type"), QStringLiteral("password")},
            {QStringLiteral("username"), QStringLiteral("jdoe")}, {QStringLiteral("password"), QStringLiteral("p")}};
        QVERIFY(!store.saveCredential(good).isEmpty());
        QVERIFY(store.saveCredential(good).isEmpty()); // duplicate name
        auto value = good;
        value[QStringLiteral("name")] = QStringLiteral("y");
        value[QStringLiteral("password")] = QStringLiteral("line\nbreak");
        QVERIFY(store.saveCredential(value).isEmpty());
        value[QStringLiteral("password")] = QString();
        QVERIFY(store.saveCredential(value).isEmpty()); // new password credential needs a password
        value[QStringLiteral("password")] = QStringLiteral("p");
        value[QStringLiteral("username")] = QStringLiteral("bad user;rm");
        QVERIFY(store.saveCredential(value).isEmpty());
        value[QStringLiteral("username")] = QStringLiteral("jdoe");
        value[QStringLiteral("type")] = QStringLiteral("key");
        value[QStringLiteral("keyFile")] = m_path; // YAML is not a private key
        QVERIFY(store.saveCredential(value).isEmpty());
        QCOMPARE(store.credentials().size(), 1);
    }

    void keychainFailureSavesNothing() {
        CredentialStore store(m_vault, m_path);
        m_vault.failing = true;
        QVERIFY(store.saveCredential({{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("type"), QStringLiteral("password")},
            {QStringLiteral("username"), QStringLiteral("jdoe")}, {QStringLiteral("password"), QStringLiteral("p")}}).isEmpty());
        QVERIFY(store.lastError().contains(QStringLiteral("locked")));
        QVERIFY(store.credentials().isEmpty());
        QVERIFY(!QFile::exists(m_path));
    }

    // A locked keychain still resolves the metadata, so the login can fall
    // back to typing the secret as the credential's user.
    void lockedKeychainResolvesMetadataOnly() {
        CredentialStore store(m_vault, m_path);
        const QString id = store.saveCredential({{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("type"), QStringLiteral("password")},
            {QStringLiteral("username"), QStringLiteral("jdoe")}, {QStringLiteral("password"), QStringLiteral("p")}});
        QVERIFY(!id.isEmpty());
        m_vault.failing = true;
        const auto resolved = CredentialStore::resolve(id, m_vault, m_path);
        m_vault.failing = false;
        QVERIFY(resolved.has_value());
        QVERIFY(resolved->secretError.contains(QStringLiteral("locked")));
        QCOMPARE(resolved->secrets.username, QByteArray("jdoe"));
        QVERIFY(resolved->secrets.password.isEmpty());
        QCOMPARE(resolved->name, QStringLiteral("x"));
    }

    void removeDeletesSecrets() {
        CredentialStore store(m_vault, m_path);
        const QString id = store.saveCredential({{QStringLiteral("name"), QStringLiteral("x")}, {QStringLiteral("type"), QStringLiteral("password")},
            {QStringLiteral("username"), QStringLiteral("jdoe")}, {QStringLiteral("password"), QStringLiteral("p")}});
        QVERIFY(!id.isEmpty());
        QVERIFY(store.removeCredential(id));
        QVERIFY(m_vault.entries.isEmpty());
        QVERIFY(store.credentials().isEmpty());
        const auto resolved = CredentialStore::resolve(id, m_vault, m_path);
        QVERIFY(!resolved.has_value());
    }

    void handEditedSecretFieldIsRejected() {
        QVERIFY(QDir().mkpath(QFileInfo(m_path).path()));
        QFile file(m_path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("kind: 8bitshell/credentials\nversion: 1\ncredentials:\n  - id: 2f1b8e8a-2a43-4c1e-9a55-0d8b2e3f4a10\n"
                   "    name: x\n    type: password\n    username: jdoe\n    keychain: credential/2f1b8e8a-2a43-4c1e-9a55-0d8b2e3f4a10\n"
                   "    password: plaintext\n");
        file.close();
        CredentialStore store(m_vault, m_path);
        QVERIFY(store.lastError().contains(QStringLiteral("password")));
        QVERIFY(store.credentials().isEmpty());
    }

    // Runs against the real OS keychain when one is reachable (on Linux: a
    // Secret Service on the session bus). Skipped otherwise.
    void systemKeychainRoundTrip() {
        auto& vault = bitshell::core::SecretVault::system();
        if (!vault.available()) QSKIP("No OS keychain is reachable in this session.");
        qputenv("BIT8SHELL_PROFILE", m_root.filePath(QStringLiteral("profile")).toUtf8());
        const QString reference = QStringLiteral("credential/test-%1").arg(QCoreApplication::applicationPid());
        QByteArray large;
        for (int i = 0; i < 10000; ++i) large.append(static_cast<char>(i % 251 + 1));
        large.append("\n\r\0tail", 7);
        const auto written = vault.write(reference, large);
#ifdef Q_OS_LINUX
        // The library loads without a running Secret Service; that is the
        // environment, not this code. CI without a keyring skips here.
        if (!written) QSKIP(qPrintable(QStringLiteral("Secret Service unreachable: ") + written.error()));
#endif
        QVERIFY2(written.has_value(), written ? "" : qPrintable(written.error()));
        auto read = vault.read(reference);
        QVERIFY2(read.has_value(), read ? "" : qPrintable(read.error()));
        QCOMPARE(*read, large);
        QVERIFY(vault.write(reference, "short").has_value());
        QCOMPARE(*vault.read(reference), QByteArray("short"));
        QVERIFY(vault.remove(reference).has_value());
        read = vault.read(reference);
        QVERIFY(read.has_value());
        QVERIFY(read->isEmpty());
        qunsetenv("BIT8SHELL_PROFILE");
    }

private:
    QTemporaryDir m_root;
    QString m_path;
    FakeSecretVault m_vault;
};

QTEST_GUILESS_MAIN(CredentialStoreTest)
#include "CredentialStoreTest.moc"
