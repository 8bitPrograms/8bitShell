#include <QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTemporaryDir>
#include <bitshell/core/PasswordStore.h>
#include <bitshell/core/Platform.h>

using bitshell::core::PasswordStore;
using bitshell::core::Platform;

namespace {
QVariantMap profile(const QString& id = QStringLiteral("saved-password-fixture")) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), QStringLiteral("Password fixture")},
        {QStringLiteral("host"), QStringLiteral("db.example.test")}, {QStringLiteral("user"), QStringLiteral("operator")},
        {QStringLiteral("port"), 22}, {QStringLiteral("authenticationMode"), QStringLiteral("password")},
        {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("direct")}}}};
}
class ScopedPassword {
public:
    explicit ScopedPassword(QVariantMap value) : value(std::move(value)) {}
    ~ScopedPassword() { (void)PasswordStore::remove(value); }
    QVariantMap value;
};
}

class PasswordStoreTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(m_root.isValid());
        m_previousProfile = qgetenv("BIT8SHELL_PROFILE");
    }
    void init() {
        const auto directory = m_root.filePath(QString::fromLatin1(QTest::currentTestFunction()));
        QVERIFY(QDir().mkpath(directory));
        qputenv("BIT8SHELL_PROFILE", directory.toUtf8());
    }
    void cleanupTestCase() {
        if (m_previousProfile.isNull()) qunsetenv("BIT8SHELL_PROFILE");
        else qputenv("BIT8SHELL_PROFILE", m_previousProfile);
    }
    void canonicalBindingsExcludeMutableLabelsAndIncludeAccountAndPort() {
        const auto original = profile();
        const auto binding = PasswordStore::binding(original);
        QVERIFY(!binding.isEmpty());
        auto changed = original;
        changed[QStringLiteral("name")] = QStringLiteral("Renamed server");
        changed[QStringLiteral("group")] = QStringLiteral("Moved folder");
        changed[QStringLiteral("host")] = QStringLiteral("DB.EXAMPLE.TEST.");
        QCOMPARE(PasswordStore::binding(changed), binding);
        changed[QStringLiteral("user")] = QStringLiteral("different-user");
        QVERIFY(PasswordStore::binding(changed) != binding);
        changed = original;
        changed[QStringLiteral("port")] = 2222;
        QVERIFY(PasswordStore::binding(changed) != binding);
        changed = original;
        changed[QStringLiteral("host")] = QStringLiteral("[2001:db8::1]");
        const auto ipv6 = PasswordStore::binding(changed);
        QVERIFY(!ipv6.isEmpty());
        changed[QStringLiteral("host")] = QStringLiteral("2001:0db8:0:0:0:0:0:1");
        QCOMPARE(PasswordStore::binding(changed), ipv6);
    }
    void rejectsAmbiguousProfilesAndUnsupportedSecretBytes() {
        const auto original = profile();
        const QList<QPair<QString, QVariant>> changes{
            {QStringLiteral("id"), QString{}}, {QStringLiteral("id"), QStringLiteral("invalid\nrecord")},
            {QStringLiteral("user"), QString{}}, {QStringLiteral("host"), QStringLiteral("-oProxyCommand=bad")},
            {QStringLiteral("port"), 0}, {QStringLiteral("port"), 65536},
            {QStringLiteral("authenticationMode"), QStringLiteral("auto")},
            {QStringLiteral("proxyJump"), QStringLiteral("jump.example.test")},
            {QStringLiteral("psmp"), QVariantMap{}},
            {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("inherit")}}},
            {QStringLiteral("connection"), QStringLiteral("direct")},
            {QStringLiteral("connection"), QVariantMap{}}
        };
        for (const auto& [key, value] : changes) {
            auto changed = original;
            changed[key] = value;
            QVERIFY2(PasswordStore::binding(changed).isEmpty(), qPrintable(key));
            QVERIFY(!PasswordStore::has(changed));
            QVERIFY(!PasswordStore::read(changed).has_value());
            QVERIFY(!PasswordStore::write(changed, QByteArrayLiteral("never-stored")).has_value());
        }
        for (const auto& secret : {QByteArray{}, QByteArrayLiteral("one\ntwo"), QByteArrayLiteral("one\rtwo"),
                                  QByteArray("one\0two", 7), QByteArray(4097, 'x'), QByteArray(1, char(0xff))})
            QVERIFY(!PasswordStore::write(original, secret).has_value());
        QVERIFY(!PasswordStore::has(original));
        const auto missing = PasswordStore::read(original);
        QVERIFY(missing.has_value());
        QVERIFY(missing->isEmpty());
    }
    void roundTripIsPrivateAndScopedAndCanBeReplacedAndRemoved() {
#ifndef Q_OS_WIN
        // CI must never request unlocking a developer's real desktop keyring.
        if (qEnvironmentVariable("BIT8SHELL_TEST_OS_KEYRING") != QStringLiteral("1"))
            QSKIP("Set BIT8SHELL_TEST_OS_KEYRING=1 to test the OS keyring with an isolated temporary namespace.");
#endif
        QVERIFY(PasswordStore::available());
        QVERIFY(!PasswordStore::backendName().isEmpty());
        ScopedPassword saved(profile());
        const QByteArray secret = QStringLiteral("fixture-only-Passw\u00f6rd-9284!$'\"").toUtf8();
        const auto stored = PasswordStore::write(saved.value, secret);
        QVERIFY2(stored.has_value(), stored ? "" : qPrintable(stored.error()));
        QVERIFY(PasswordStore::has(saved.value));
        auto recovered = PasswordStore::read(saved.value);
        QVERIFY2(recovered.has_value(), recovered ? "" : qPrintable(recovered.error()));
        QCOMPARE(*recovered, secret);
        recovered->fill('\0');

        QDirIterator files(Platform::appConfigDirectory(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        int storedFiles = 0;
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto bytes = file.readAll();
            QVERIFY(!bytes.contains(secret));
            QVERIFY(!bytes.contains(secret.toBase64()));
            ++storedFiles;
        }
        QCOMPARE(storedFiles, 1);

        for (const auto& [key, value] : QList<QPair<QString, QVariant>>{
                 {QStringLiteral("id"), QStringLiteral("duplicated-profile")},
                 {QStringLiteral("host"), QStringLiteral("other.example.test")},
                 {QStringLiteral("user"), QStringLiteral("other-user")}, {QStringLiteral("port"), 2222}}) {
            auto different = saved.value;
            different[key] = value;
            QVERIFY(!PasswordStore::has(different));
            const auto wrongTarget = PasswordStore::read(different);
            QVERIFY(wrongTarget.has_value());
            QVERIFY(wrongTarget->isEmpty());
        }
        const auto originalDirectory = qgetenv("BIT8SHELL_PROFILE");
        const auto otherDirectory = m_root.filePath(QStringLiteral("other-profile"));
        QVERIFY(QDir().mkpath(otherDirectory));
        qputenv("BIT8SHELL_PROFILE", otherDirectory.toUtf8());
        const auto otherNamespace = PasswordStore::read(saved.value);
        qputenv("BIT8SHELL_PROFILE", originalDirectory);
        QVERIFY(otherNamespace.has_value());
        QVERIFY(otherNamespace->isEmpty());

        const QByteArray replacement = QByteArrayLiteral("replacement-fixture-only");
        QVERIFY(PasswordStore::write(saved.value, replacement).has_value());
        recovered = PasswordStore::read(saved.value);
        QVERIFY(recovered.has_value());
        QCOMPARE(*recovered, replacement);
        recovered->fill('\0');
        QVERIFY(PasswordStore::remove(saved.value).has_value());
        QVERIFY(!PasswordStore::has(saved.value));
        recovered = PasswordStore::read(saved.value);
        QVERIFY(recovered.has_value());
        QVERIFY(recovered->isEmpty());
        QVERIFY(PasswordStore::remove(saved.value).has_value());
    }
    void windowsRejectsTamperingAndCiphertextCopiedToAnotherTarget() {
#ifndef Q_OS_WIN
        QSKIP("DPAPI integrity and entropy binding are Windows-specific.");
#else
        ScopedPassword first(profile(QStringLiteral("first"))), second(profile(QStringLiteral("second")));
        QVERIFY(PasswordStore::write(first.value, QByteArrayLiteral("first-fixture-secret")).has_value());
        const QDir directory(QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("passwords")));
        const auto firstNames = directory.entryList({QStringLiteral("password-*.dat")}, QDir::Files);
        QCOMPARE(firstNames.size(), 1);
        QFile firstFile(directory.filePath(firstNames.first()));
        QVERIFY(firstFile.open(QIODevice::ReadOnly));
        const auto firstBytes = firstFile.readAll();
        firstFile.close();
        QVERIFY(PasswordStore::write(second.value, QByteArrayLiteral("second-fixture-secret")).has_value());
        auto secondNames = directory.entryList({QStringLiteral("password-*.dat")}, QDir::Files);
        secondNames.removeAll(firstNames.first());
        QCOMPARE(secondNames.size(), 1);
        QFile secondFile(directory.filePath(secondNames.first()));
        QVERIFY(secondFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(secondFile.write(firstBytes), firstBytes.size());
        secondFile.close();
        QVERIFY(!PasswordStore::read(second.value).has_value());
        auto damaged = firstBytes;
        damaged[damaged.size() - 1] = char(damaged.back() ^ 0x40);
        QVERIFY(firstFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(firstFile.write(damaged), damaged.size());
        firstFile.close();
        QVERIFY(!PasswordStore::read(first.value).has_value());
        QVERIFY(PasswordStore::remove(first.value).has_value());
        QVERIFY(PasswordStore::remove(second.value).has_value());
#endif
    }
private:
    QTemporaryDir m_root;
    QByteArray m_previousProfile;
};

QTEST_GUILESS_MAIN(PasswordStoreTest)
#include "PasswordStoreTest.moc"
