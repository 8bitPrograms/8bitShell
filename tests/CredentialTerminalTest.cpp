// End-to-end credential flows through TerminalView with prompt-emitting
// shell scripts in place of ssh. No network, no real keychain.
#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <bitshell/core/CredentialStore.h>
#include <bitshell/core/PasswordStore.h>
#include "FakeKeychain.h"
#include "FakeSecretVault.h"
#include "TerminalTestAccess.h"

using bitshell::core::CredentialStore;
using bitshell::core::PasswordStore;
using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

namespace {
const QByteArray secret = "s3cret-Pw";
const QByteArray keyText = "-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAA\n-----END OPENSSH PRIVATE KEY-----\n";
// Enough ordinary output after the last answer to mean "logged in".
const QString loggedIn = QStringLiteral("printf 'Welcome\\n'; head -c 6000 /dev/zero | tr '\\\\0' x; printf '\\n$ '; sleep 5");
}

class CredentialTerminalTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_root;
    FakeSecretVault m_vault;
    FakeKeychain m_keychain;
    QString m_path;
    QString m_passwordId;
    QString m_keyId;

    struct Tab {
        TerminalView view;
        QByteArray output;
        Tab() {
            view.setWidth(900); view.setHeight(400);
            QObject::connect(&view, &TerminalView::rawOutputReceived, &view, [this](const QByteArray& data) { output += data; });
        }
    };

    void attach(Tab& tab) { TerminalTestAccess::useCredentials(tab.view, m_vault, m_path); }
    QVariantMap session(const QString& credential) const {
        return {{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("credential"), credential}};
    }
    static bool waitFor(Tab& tab, const QByteArray& text, int timeout = 5000) {
        QElapsedTimer timer; timer.start();
        while (!tab.output.contains(text) && timer.elapsed() < timeout) QTest::qWait(10);
        return tab.output.contains(text);
    }

    // A session in password mode whose password PasswordStore remembers.
    static QVariantMap passwordSession() {
        return {{QStringLiteral("id"), QStringLiteral("saved-session-1")}, {QStringLiteral("host"), QStringLiteral("fake.test")},
                {QStringLiteral("user"), QStringLiteral("jdoe")}, {QStringLiteral("authenticationMode"), QStringLiteral("password")}};
    }
    QString argvPath() const { return m_root.filePath(QStringLiteral("ssh-argv")); }
    // The real launch path (startSshProfile) runs the app's bundled-ssh slot,
    // openssh/ssh beside this binary, which is this fake: it records its
    // arguments and plays the scenario.
    bool installFakeSsh(const QString& scenario) {
        const QString script = m_root.filePath(QStringLiteral("scenario.sh"));
        QFile file(script);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        file.write(scenario.toUtf8()); file.close();
        QFile::remove(argvPath());
        const QString fake = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("openssh/ssh"));
        QDir().mkpath(QFileInfo(fake).path());
        QFile ssh(fake);
        if (!ssh.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        ssh.write(QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$@\" > '%1'\n"
                                 "env | grep -c ASKPASS >> '%1'\nexec /bin/sh '%2'\n").arg(argvPath(), script).toUtf8());
        ssh.close();
        return ssh.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    QStringList recordedArgv() const {
        QFile file(argvPath());
        if (!file.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    }
    static bool waitForStatus(Tab& tab, const QString& text, int timeout = 5000) {
        QElapsedTimer timer; timer.start();
        while (!tab.view.statusText().contains(text) && timer.elapsed() < timeout) QTest::qWait(10);
        return tab.view.statusText().contains(text);
    }
    // Keyboard-interactive style: password, then a code only the user knows.
    static QString keyboardInteractive() {
        return QStringLiteral(
            "stty -echo; printf '(jdoe@fake.test) Password: '; IFS= read -r a; printf '\\nGOT_PW:%s\\n' \"$a\";"
            "printf '(jdoe@fake.test) Verification code: '; IFS= read -r b; printf '\\nGOT_CODE:%s\\n' \"$b\";") + loggedIn;
    }

private slots:
    void initTestCase() {
        QVERIFY(m_root.isValid());
        // Saved passwords use this in-process keychain: no Secret Service needed.
        PasswordStore::useTestKeychain(&m_keychain);
        qputenv("BIT8SHELL_PROFILE", m_root.filePath(QStringLiteral("profile")).toUtf8());
        m_path = m_root.filePath(QStringLiteral("credentials.yaml"));
        CredentialStore store(m_vault, m_path);
        m_passwordId = store.saveCredential({{QStringLiteral("name"), QStringLiteral("prod")},
            {QStringLiteral("type"), QStringLiteral("password")}, {QStringLiteral("username"), QStringLiteral("jdoe")},
            {QStringLiteral("password"), QString::fromUtf8(secret)}});
        QVERIFY2(!m_passwordId.isEmpty(), qPrintable(store.lastError()));
        const QString keyFile = m_root.filePath(QStringLiteral("id_test"));
        QFile file(keyFile);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(keyText); file.close();
        m_keyId = store.saveCredential({{QStringLiteral("name"), QStringLiteral("deploy-key")},
            {QStringLiteral("type"), QStringLiteral("key")}, {QStringLiteral("username"), QStringLiteral("deploy")},
            {QStringLiteral("keyFile"), QUrl::fromLocalFile(keyFile).toString()}, {QStringLiteral("passphrase"), QStringLiteral("pp-123")}});
        QVERIFY2(!m_keyId.isEmpty(), qPrintable(store.lastError()));
        const QVariantMap saved = passwordSession();
        QVERIFY(PasswordStore::write(saved, "saved-Pw-1"));
        QVERIFY(PasswordStore::has(saved));
    }
    void cleanupTestCase() {
        QFile::remove(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("openssh/ssh")));
        PasswordStore::useTestKeychain(nullptr);
    }
    void cleanup() { m_keychain.locked = false; m_vault.failing = false; }

    // QA (2): a remembered password with a keyboard-interactive login. The
    // real launch path allows keyboard-interactive, types the saved password
    // into the password prompt once, and leaves the code to the user.
    void savedPasswordAllowsTwoFactor() {
        Tab tab;
        QVERIFY(installFakeSsh(keyboardInteractive()));
        tab.view.startSshProfile(passwordSession());
        QVERIFY(waitFor(tab, "GOT_PW:saved-Pw-1\r\n"));
        QVERIFY(waitFor(tab, "Verification code: "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("GOT_CODE"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("verification code yourself")));
        tab.view.sendText(QStringLiteral("654321\r"));
        QVERIFY(waitFor(tab, "GOT_CODE:654321\r\n"));
        QVERIFY(waitFor(tab, "Welcome"));
        QCOMPARE(tab.output.count("saved-Pw-1"), 1);
        const QStringList argv = recordedArgv();
        QVERIFY2(argv.contains(QStringLiteral("-oPreferredAuthentications=keyboard-interactive,password")), qPrintable(argv.join(u' ')));
        QVERIFY(argv.contains(QStringLiteral("-oKbdInteractiveAuthentication=yes")));
        QVERIFY(argv.contains(QStringLiteral("-oNumberOfPasswordPrompts=1")));
        QVERIFY(!argv.join(u' ').contains(QStringLiteral("saved-Pw-1")));
        QCOMPARE(argv.last(), QStringLiteral("0")); // No askpass helper: the prompt is answered in the terminal.
        QVERIFY(!tab.view.statusText().contains(QStringLiteral("saved-Pw-1")));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // The same keyboard-interactive login with a credential attached.
    void credentialAllowsTwoFactor() {
        Tab tab; attach(tab);
        QVERIFY(installFakeSsh(keyboardInteractive()));
        tab.view.startSshProfile(session(m_passwordId));
        QVERIFY(waitFor(tab, "GOT_PW:" + secret + "\r\n"));
        QVERIFY(waitFor(tab, "Verification code: "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("GOT_CODE"));
        tab.view.sendText(QStringLiteral("111222\r"));
        QVERIFY(waitFor(tab, "GOT_CODE:111222\r\n"));
        QCOMPARE(tab.output.count(secret), 1);
        QVERIFY(recordedArgv().contains(QStringLiteral("jdoe@fake.test")) || recordedArgv().join(u' ').contains(QStringLiteral("jdoe")));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QA (1): a locked or unavailable keyring never blocks the login. ssh
    // starts, its prompt goes to the user, and a short notice says why.
    void lockedKeyringFallsBackToPrompt() {
        Tab tab;
        m_keychain.locked = true;
        QVERIFY(installFakeSsh(QStringLiteral(
            "stty -echo; printf \"jdoe@fake.test's password: \"; IFS= read -r a; printf '\\nGOT_PW:%s\\n' \"$a\"; sleep 5")));
        tab.view.startSshProfile(passwordSession());
        QVERIFY(waitFor(tab, "password: "));
        QVERIFY2(waitForStatus(tab, QStringLiteral("Could not read the saved password")), qPrintable(tab.view.statusText()));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("Type it yourself")));
        QVERIFY(!TerminalTestAccess::passwordHandoffActive(tab.view));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("GOT_PW"));
        tab.view.sendText(QStringLiteral("typed-pw\r"));
        QVERIFY(waitFor(tab, "GOT_PW:typed-pw\r\n"));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // The password was remembered, but its keychain entry has gone.
    void missingSavedPasswordFallsBack() {
        Tab tab;
        const auto saved = m_keychain.entries;
        m_keychain.entries.clear();
        QVariantMap effective; QString notice;
        TerminalTestAccess::prepareCredential(tab.view, passwordSession(), effective, notice);
        m_keychain.entries = saved;
        QVERIFY2(notice.contains(QStringLiteral("missing")), qPrintable(notice));
        QCOMPARE(effective, passwordSession());
        QVERIFY(!TerminalTestAccess::passwordHandoffActive(tab.view));
    }

    // The credential list is readable but its keychain is locked: log in as
    // the credential's user, typing the secret.
    void lockedCredentialVaultFallsBack() {
        Tab tab; attach(tab);
        m_vault.failing = true;
        QString error, notice; QVariantMap effective;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; printf \"jdoe@fake.test's password: \"; IFS= read -r a; printf '\\nGOT_PW:%s\\n' \"$a\"; sleep 5"),
            error, &effective, &notice), qPrintable(error));
        QVERIFY2(notice.contains(QStringLiteral("Could not read the saved secret")), qPrintable(notice));
        QCOMPARE(effective.value(QStringLiteral("user")).toString(), QStringLiteral("jdoe"));
        QVERIFY(!effective.contains(QStringLiteral("credential")));
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(waitFor(tab, "password: "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("GOT_PW"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitFor(tab, "GOT_PW:typed\r\n"));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // Password, then a verification code typed by the user, then the flow
    // continues with the next prompt it knows.
    void twoFactorPausesForUser() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; printf \"jdoe@fake.test's password: \"; IFS= read -r a; printf '\\nGOT_PW:%s\\n' \"$a\";"
            "printf '(jdoe@fake.test) Verification code: '; IFS= read -r b; printf '\\nGOT_CODE:%s\\n' \"$b\";"
            "printf 'login: '; IFS= read -r c; printf '\\nGOT_USER:%s\\n' \"$c\";") + loggedIn, error), qPrintable(error));
        QVERIFY(waitFor(tab, "GOT_PW:" + secret + "\r\n"));
        QVERIFY(waitFor(tab, "Verification code: "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("GOT_CODE"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("verification code yourself")));
        QVERIFY(TerminalTestAccess::credentialArmed(tab.view));
        tab.view.sendText(QStringLiteral("123456\r"));
        QVERIFY(waitFor(tab, "GOT_CODE:123456\r\n"));
        QVERIFY(waitFor(tab, "GOT_USER:jdoe\r\n"));
        // Settled: the flow disarms and the secret is gone from the tab.
        QTRY_VERIFY_WITH_TIMEOUT(!TerminalTestAccess::credentialArmed(tab.view), 5000);
        QCOMPARE(tab.output.count(secret), 1);
        QVERIFY(!tab.view.statusText().contains(QString::fromUtf8(secret)));
        TerminalTestAccess::stopFixture(tab.view);
    }

    void wrongPasswordIsNotSentTwice() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; printf \"jdoe@fake.test's password: \"; IFS= read -r a; printf '\\nGOT_PW:%s\\n' \"$a\";"
            "printf 'Permission denied, please try again.\\n'; printf \"jdoe@fake.test's password: \";"
            "IFS= read -r b; printf '\\nGOT_SECOND:%s\\n' \"$b\"; sleep 5"), error), qPrintable(error));
        QVERIFY(waitFor(tab, "GOT_PW:" + secret));
        QVERIFY(waitFor(tab, "try again.\r\njdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("GOT_SECOND"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("was wrong")));
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        tab.view.sendText(QStringLiteral("typed-by-user\r"));
        QVERIFY(waitFor(tab, "GOT_SECOND:typed-by-user"));
        QCOMPARE(tab.output.count(secret), 1);
        TerminalTestAccess::stopFixture(tab.view);
    }

    void unknownPromptGoesToUser() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; printf 'Are you sure you want to continue connecting (yes/no/[fingerprint])? ';"
            "IFS= read -r a; printf '\\nANSWER:%s\\n' \"$a\";"
            "printf \"jdoe@fake.test's password: \"; IFS= read -r b; printf '\\nGOT_PW:%s\\n' \"$b\"; sleep 5"), error), qPrintable(error));
        QVERIFY(waitFor(tab, "[fingerprint])? "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("ANSWER:"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("Answer it yourself")));
        tab.view.sendText(QStringLiteral("yes\r"));
        QVERIFY(waitFor(tab, "ANSWER:yes\r\n"));
        QVERIFY(waitFor(tab, "GOT_PW:" + secret + "\r\n"));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // No credential: nothing is injected and the profile is used unchanged.
    void noCredentialBehavesAsBefore() {
        Tab tab; attach(tab);
        const QVariantMap plain{{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")},
            {QStringLiteral("authenticationMode"), QStringLiteral("password")}};
        QVariantMap effective; QString error;
        TerminalTestAccess::prepareCredential(tab.view, plain, effective, error);
        QVERIFY(error.isEmpty());
        QCOMPARE(effective, plain);
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral(
            "stty -echo; printf \"jdoe@fake.test's password: \"; IFS= read -r a; printf '\\nGOT_PW:%s\\n' \"$a\"; sleep 5"), error));
        QVERIFY(waitFor(tab, "password: "));
        QTest::qWait(400);
        QVERIFY(!tab.output.contains("GOT_PW"));
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(tab.view.credentialName().isEmpty());
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitFor(tab, "GOT_PW:typed\r\n"));
        TerminalTestAccess::stopFixture(tab.view);
    }

    void noCredentialNeverReadsKeychain() {
        Tab tab; attach(tab);
        const int before = m_vault.reads;
        const int keychainReads = m_keychain.reads;
        QVariantMap effective; QString error;
        TerminalTestAccess::prepareCredential(tab.view, {{QStringLiteral("host"), QStringLiteral("h")}}, effective, error);
        QCOMPARE(m_vault.reads, before);
        QCOMPARE(m_keychain.reads, keychainReads);
    }

    // A key goes to a private temporary file for one login and is removed after.
    void keyCredentialUsesTemporaryFile() {
        Tab tab; attach(tab);
        QVariantMap profile = session(m_keyId);
        profile.insert(QStringLiteral("user"), QStringLiteral("someone-else"));
        profile.insert(QStringLiteral("identityKey"), QStringLiteral("none"));
        QVariantMap effective; QString error;
        TerminalTestAccess::prepareCredential(tab.view, profile, effective, error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QString keyPath = effective.value(QStringLiteral("identityFile")).toString();
        QVERIFY(QFileInfo(keyPath).isAbsolute());
        QCOMPARE(effective.value(QStringLiteral("user")).toString(), QStringLiteral("deploy"));
        QCOMPARE(effective.value(QStringLiteral("identityKey")).toString(), QString{});
        QCOMPARE(effective.value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("auto"));
        QFile key(keyPath);
        QVERIFY(key.open(QIODevice::ReadOnly));
        QCOMPARE(key.readAll(), keyText);
        key.close();
        QCOMPARE(QFileInfo(keyPath).permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther), QFileDevice::Permissions{});
        // Discard this preparation; it must not leave the key behind.
        TerminalTestAccess::stopFixture(tab.view);
        TerminalTestAccess::prepareCredential(tab.view, {{QStringLiteral("host"), QStringLiteral("h")}}, effective, error);
        QVERIFY(!QFileInfo::exists(keyPath));

        // Now a full login that asks for this key's passphrase.
        // The script waits until the test tells it which file ssh would read.
        const QString handoff = m_root.filePath(QStringLiteral("key-path"));
        QFile::remove(handoff);
        QVariantMap started;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, profile, QStringLiteral(
            "stty -echo; while [ ! -s '%1' ]; do sleep 0.05; done;"
            "printf \"Enter passphrase for key '%s': \" \"$(cat '%1')\"; IFS= read -r a; printf '\\nGOT_PASS:%s\\n' \"$a\"; sleep 5")
            .arg(handoff), error, &started), qPrintable(error));
        const QString loginKey = started.value(QStringLiteral("identityFile")).toString();
        QVERIFY(QFileInfo::exists(loginKey));
        QFile path(handoff);
        QVERIFY(path.open(QIODevice::WriteOnly));
        path.write(loginKey.toUtf8()); path.close();
        QVERIFY(waitFor(tab, "GOT_PASS:pp-123\r\n"));
        TerminalTestAccess::stopFixture(tab.view);
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(!QFileInfo::exists(loginKey));
    }

    // A credential bound to a shell tab answers only ssh's prompt for its user.
    void boundTabIgnoresSudo() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral(
            "stty -echo; sleep 0.3; printf '[sudo] password for jdoe: '; IFS= read -r a; printf '\\nSUDO:%s\\n' \"$a\";"
            "printf \"jdoe@fake.test's password: \"; IFS= read -r b; printf '\\nGOT_PW:%s\\n' \"$b\"; sleep 5"), error));
        QSignalSpy changed(&tab.view, &TerminalView::credentialChanged);
        QVERIFY(tab.view.bindCredential(m_passwordId));
        QVERIFY(tab.view.credentialBound());
        QCOMPARE(tab.view.credentialName(), QStringLiteral("prod"));
        QVERIFY(waitFor(tab, "password for jdoe: "));
        QTest::qWait(300);
        QVERIFY(!tab.output.contains("SUDO:"));
        tab.view.sendText(QStringLiteral("mine\r"));
        QVERIFY(waitFor(tab, "SUDO:mine\r\n"));
        QVERIFY(waitFor(tab, "GOT_PW:" + secret + "\r\n"));
        tab.view.unbindCredential();
        QVERIFY(!tab.view.credentialBound());
        QVERIFY(changed.count() >= 2);
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QA (1): a deleted credential never sent anything, so it falls back to
    // the session's own login with a notice instead of refusing.
    void deletedCredentialFallsBack() {
        Tab tab; attach(tab);
        QVariantMap profile = session(QStringLiteral("0f8fad5b-d9cb-469f-a165-70867728950e"));
        profile.insert(QStringLiteral("user"), QStringLiteral("session-user"));
        QVariantMap effective; QString notice;
        TerminalTestAccess::prepareCredential(tab.view, profile, effective, notice);
        QVERIFY2(notice.contains(QStringLiteral("deleted")), qPrintable(notice));
        QVERIFY(notice.contains(QStringLiteral("Log in yourself")));
        QCOMPARE(effective.value(QStringLiteral("user")).toString(), QStringLiteral("session-user"));
        QVERIFY(!effective.contains(QStringLiteral("credential")));
        QVERIFY(!TerminalTestAccess::passwordHandoffActive(tab.view));
    }
};

QTEST_MAIN(CredentialTerminalTest)
#include "CredentialTerminalTest.moc"
