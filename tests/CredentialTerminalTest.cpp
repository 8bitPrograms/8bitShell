// End-to-end credential flows through TerminalView with prompt-emitting
// shell scripts in place of ssh. No network, no real keychain.
#include <QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/CredentialStore.h>
#include <bitshell/core/PasswordStore.h>
#include "FakeKeychain.h"
#include "FakeSecretVault.h"
#include "TerminalTestAccess.h"

using bitshell::core::ConnectionRouting;
using bitshell::core::CredentialStore;
using bitshell::core::PasswordStore;
using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

namespace {
const QByteArray secret = "s3cret-Pw";
const QByteArray keyText = "-----BEGIN OPENSSH PRIVATE KEY-----\nb3BlbnNzaC1rZXktdjEAAAAA\n-----END OPENSSH PRIVATE KEY-----\n";
// What a server prints once the login is done.
const QString loggedIn = QStringLiteral("printf 'Welcome\\n$ '; sleep 5");
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
    // The scripts record what they read in a file, never on screen: a line of
    // output after an answer ends the login window, as a real login does.
    QString receivedPath() const { return m_root.filePath(QStringLiteral("received")); }
    QByteArray received() const {
        QFile file(receivedPath());
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    bool waitForReceived(const QByteArray& text, int timeout = 5000) const {
        QElapsedTimer timer; timer.start();
        while (!received().contains(text) && timer.elapsed() < timeout) QTest::qWait(10);
        return received().contains(text);
    }
    // Reads a hidden answer into $var, records it as "label:value", then
    // prints the newline ssh prints after hidden input.
    QString reads(const char* var, const char* label) const {
        return QStringLiteral("IFS= read -r %1; printf '%2:%s\\n' \"$%1\" >> '%3'; printf '\\n';")
            .arg(QLatin1String(var), QLatin1String(label), receivedPath());
    }
    QString passwordPrompt(const char* var, const char* label, const QString& who = QStringLiteral("jdoe@fake.test")) const {
        return QStringLiteral("printf '%s' \"%1's password: \";").arg(who) + reads(var, label);
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
    QString keyboardInteractive() const {
        return QStringLiteral("stty -echo; printf '(jdoe@fake.test) Password: ';") + reads("a", "GOT_PW")
            + QStringLiteral("printf '(jdoe@fake.test) Verification code: ';") + reads("b", "GOT_CODE") + loggedIn;
    }
    // A CyberArk-routed session, as ConnectionRouting::prepare resolves it.
    static QVariantMap psmpSession() {
        const QVariantMap account{{QStringLiteral("source"), QStringLiteral("identity")}, {QStringLiteral("suffix"), QStringLiteral("SYS")},
            {QStringLiteral("domain"), QStringLiteral("corp.example.test")}};
        const QVariantMap route{{QStringLiteral("type"), QStringLiteral("cyberark-psmp")}, {QStringLiteral("host"), QStringLiteral("psmp.example.test")},
            {QStringLiteral("port"), 22}, {QStringLiteral("account"), account}};
        const QVariantMap session{{QStringLiteral("host"), QStringLiteral("db.example.test")}, {QStringLiteral("user"), QStringLiteral("other")},
            {QStringLiteral("group"), QStringLiteral("Oracle/Prod")},
            {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("inherit")}}}};
        const QVariantMap folders{{QStringLiteral("Oracle"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")}, {QStringLiteral("route"), QStringLiteral("work")}}}};
        const auto prepared = ConnectionRouting::prepare(session, {{QStringLiteral("work"), route}}, folders, QStringLiteral("EMP000123"));
        return prepared ? *prepared : QVariantMap{};
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
    void init() { QFile::remove(receivedPath()); }
    void cleanup() { m_keychain.locked = false; m_vault.failing = false; }

    // QA (2): a remembered password with a keyboard-interactive login. The
    // real launch path allows keyboard-interactive, types the saved password
    // into the password prompt once, and leaves the code to the user.
    void savedPasswordAllowsTwoFactor() {
        Tab tab;
        QVERIFY(installFakeSsh(keyboardInteractive()));
        tab.view.startSshProfile(passwordSession());
        QVERIFY(waitForReceived("GOT_PW:saved-Pw-1\n"));
        QVERIFY(waitFor(tab, "Verification code: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_CODE"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("verification code yourself")));
        tab.view.sendText(QStringLiteral("654321\r"));
        QVERIFY(waitForReceived("GOT_CODE:654321\n"));
        QVERIFY(waitFor(tab, "Welcome"));
        QCOMPARE(received().count("saved-Pw-1"), 1);
        QVERIFY(!tab.output.contains("saved-Pw-1"));
        QTRY_VERIFY_WITH_TIMEOUT(!TerminalTestAccess::credentialArmed(tab.view), 2000);
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
        QVERIFY(waitForReceived("GOT_PW:" + secret + "\n"));
        QVERIFY(waitFor(tab, "Verification code: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_CODE"));
        tab.view.sendText(QStringLiteral("111222\r"));
        QVERIFY(waitForReceived("GOT_CODE:111222\n"));
        QCOMPARE(received().count(secret), 1);
        QVERIFY(!tab.output.contains(secret));
        QVERIFY(recordedArgv().contains(QStringLiteral("jdoe@fake.test")) || recordedArgv().join(u' ').contains(QStringLiteral("jdoe")));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC B1: a password credential logs in by password only, and a password
    // prompt that is not ssh's for jdoe@fake.test never gets the secret, even
    // when no ssh prompt came first (as after a key or agent login).
    void passwordCredentialIgnoresLaterPrompts() {
        Tab tab; attach(tab);
        QVERIFY(installFakeSsh(QStringLiteral("stty -echo; printf 'Last login: Fri Oct 30\\njdoe@fake:~$ sudo -v\\n';"
            "printf '[sudo] password for jdoe: ';") + reads("a", "SUDO")
            + QStringLiteral("printf 'Password: ';") + reads("b", "VAULT") + QStringLiteral("sleep 5")));
        tab.view.startSshProfile(session(m_passwordId));
        QVERIFY(waitFor(tab, "password for jdoe: "));
        QTest::qWait(300);
        QVERIFY(received().isEmpty());
        tab.view.sendText(QStringLiteral("mine\r"));
        QVERIFY(waitForReceived("SUDO:mine\n"));
        QVERIFY(waitFor(tab, "Password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("VAULT"));
        tab.view.sendText(QStringLiteral("vault\r"));
        QVERIFY(waitForReceived("VAULT:vault\n"));
        QVERIFY(!received().contains(secret));
        const QStringList argv = recordedArgv();
        QVERIFY2(argv.contains(QStringLiteral("-oPubkeyAuthentication=no")), qPrintable(argv.join(u' ')));
        QVERIFY(argv.contains(QStringLiteral("-oPreferredAuthentications=keyboard-interactive,password")));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC B1: after a successful password login the window is closed, so a
    // later password prompt (even ssh-shaped, e.g. a nested ssh) gets nothing.
    void noAnswerAfterPasswordLogin() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral("stty -echo;")
            + passwordPrompt("a", "GOT_PW") + QStringLiteral("printf 'Welcome\\njdoe@fake:~$ '; sleep 0.3;")
            + passwordPrompt("b", "LATER") + QStringLiteral("sleep 5"), error), qPrintable(error));
        QVERIFY(waitForReceived("GOT_PW:" + secret + "\n"));
        QVERIFY(waitFor(tab, "Welcome"));
        QTRY_VERIFY_WITH_TIMEOUT(!TerminalTestAccess::credentialArmed(tab.view), 2000);
        QVERIFY(waitFor(tab, "$ jdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("LATER"));
        QVERIFY(!tab.view.statusText().contains(QStringLiteral("was wrong")));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("LATER:typed\n"));
        QCOMPARE(received().count(secret), 1);
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QA (1): a locked or unavailable keyring never blocks the login. ssh
    // starts, its prompt goes to the user, and a short notice says why.
    void lockedKeyringFallsBackToPrompt() {
        Tab tab;
        m_keychain.locked = true;
        QVERIFY(installFakeSsh(QStringLiteral("stty -echo;") + passwordPrompt("a", "GOT_PW") + QStringLiteral("sleep 5")));
        tab.view.startSshProfile(passwordSession());
        QVERIFY(waitFor(tab, "password: "));
        QVERIFY2(waitForStatus(tab, QStringLiteral("Could not read the saved password")), qPrintable(tab.view.statusText()));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("Type it yourself")));
        QVERIFY(!TerminalTestAccess::passwordHandoffActive(tab.view));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_PW"));
        tab.view.sendText(QStringLiteral("typed-pw\r"));
        QVERIFY(waitForReceived("GOT_PW:typed-pw\n"));
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
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId),
            QStringLiteral("stty -echo;") + passwordPrompt("a", "GOT_PW") + QStringLiteral("sleep 5"),
            error, &effective, &notice), qPrintable(error));
        QVERIFY2(notice.contains(QStringLiteral("Could not read the saved secret")), qPrintable(notice));
        QCOMPARE(effective.value(QStringLiteral("user")).toString(), QStringLiteral("jdoe"));
        QVERIFY(!effective.contains(QStringLiteral("credential")));
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(waitFor(tab, "password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_PW"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("GOT_PW:typed\n"));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // Password, then a verification code typed by the user, then the login
    // finishes and the flow disarms.
    void twoFactorPausesForUser() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral("stty -echo;")
            + passwordPrompt("a", "GOT_PW") + QStringLiteral("printf '(jdoe@fake.test) Verification code: ';")
            + reads("b", "GOT_CODE") + loggedIn, error), qPrintable(error));
        QVERIFY(waitForReceived("GOT_PW:" + secret + "\n"));
        QVERIFY(waitFor(tab, "Verification code: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_CODE"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("verification code yourself")));
        QVERIFY(TerminalTestAccess::credentialArmed(tab.view));
        tab.view.sendText(QStringLiteral("123456\r"));
        QVERIFY(waitForReceived("GOT_CODE:123456\n"));
        // Logged in: the flow disarms and the secret is gone from the tab.
        QTRY_VERIFY_WITH_TIMEOUT(!TerminalTestAccess::credentialArmed(tab.view), 5000);
        QCOMPARE(received().count(secret), 1);
        QVERIFY(!tab.output.contains(secret));
        QVERIFY(!tab.view.statusText().contains(QString::fromUtf8(secret)));
        TerminalTestAccess::stopFixture(tab.view);
    }

    void wrongPasswordIsNotSentTwice() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral("stty -echo;")
            + passwordPrompt("a", "GOT_PW") + QStringLiteral("printf 'Permission denied, please try again.\\n';")
            + passwordPrompt("b", "GOT_SECOND") + QStringLiteral("sleep 5"), error), qPrintable(error));
        QVERIFY(waitForReceived("GOT_PW:" + secret));
        QVERIFY(waitFor(tab, "try again.\r\njdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_SECOND"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("was wrong")));
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        tab.view.sendText(QStringLiteral("typed-by-user\r"));
        QVERIFY(waitForReceived("GOT_SECOND:typed-by-user"));
        QCOMPARE(received().count(secret), 1);
        TerminalTestAccess::stopFixture(tab.view);
    }

    void unknownPromptGoesToUser() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; printf 'Are you sure you want to continue connecting (yes/no/[fingerprint])? ';")
            + reads("a", "ANSWER") + passwordPrompt("b", "GOT_PW") + QStringLiteral("sleep 5"), error), qPrintable(error));
        QVERIFY(waitFor(tab, "[fingerprint])? "));
        QTest::qWait(300);
        QVERIFY(!received().contains("ANSWER:"));
        QVERIFY(tab.view.statusText().contains(QStringLiteral("Answer it yourself")));
        tab.view.sendText(QStringLiteral("yes\r"));
        QVERIFY(waitForReceived("ANSWER:yes\n"));
        QVERIFY(waitForReceived("GOT_PW:" + secret + "\n"));
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
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("stty -echo;") + passwordPrompt("a", "GOT_PW")
            + QStringLiteral("sleep 5"), error));
        QVERIFY(waitFor(tab, "password: "));
        QTest::qWait(400);
        QVERIFY(!received().contains("GOT_PW"));
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(tab.view.credentialName().isEmpty());
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("GOT_PW:typed\n"));
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
            "printf \"Enter passphrase for key '%s': \" \"$(cat '%1')\";").arg(handoff) + reads("a", "GOT_PASS")
            + QStringLiteral("sleep 5"), error, &started), qPrintable(error));
        const QString loginKey = started.value(QStringLiteral("identityFile")).toString();
        QVERIFY(QFileInfo::exists(loginKey));
        QFile path(handoff);
        QVERIFY(path.open(QIODevice::WriteOnly));
        path.write(loginKey.toUtf8()); path.close();
        QVERIFY(waitForReceived("GOT_PASS:pp-123\n"));
        TerminalTestAccess::stopFixture(tab.view);
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(!QFileInfo::exists(loginKey));
    }

    // A credential bound to a shell tab answers only ssh's prompt for its
    // user at the host typed in this tab. A prompt that is not ssh's, answered
    // by the user, closes the window: the later exact prompt gets nothing (QC2 F1).
    void boundTabIgnoresSudo() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral(
            "stty -echo; sleep 0.3; printf '[sudo] password for jdoe: ';") + reads("a", "SUDO")
            + passwordPrompt("b", "GOT_PW") + QStringLiteral("sleep 5"), error));
        QSignalSpy changed(&tab.view, &TerminalView::credentialChanged);
        QVERIFY(tab.view.bindCredential(m_passwordId));
        QVERIFY(tab.view.credentialBound());
        QCOMPARE(tab.view.credentialName(), QStringLiteral("prod"));
        TerminalTestAccess::noteTypedSsh(tab.view, {{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")}});
        QVERIFY(waitFor(tab, "password for jdoe: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("SUDO:"));
        tab.view.sendText(QStringLiteral("mine\r"));
        QVERIFY(waitForReceived("SUDO:mine\n"));
        QVERIFY(waitFor(tab, "jdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("GOT_PW"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("GOT_PW:typed\n"));
        QVERIFY(!received().contains(secret));
        // The foreign prompt closed the window for good, so the secret is gone too.
        QVERIFY(!tab.view.credentialBound());
        QVERIFY(changed.count() >= 2);
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC2 F1: the typed ssh logs in with a key, so ssh never asks. The remote
    // shell's prompt ends the window, and a later exact prompt gets nothing.
    void boundTabKeyLoginGetsNothingLater() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("stty -echo; sleep 0.3;"
            "printf 'Last login: Fri Oct 30\\njdoe@fake:~$ '; sleep 0.3; printf 'ssh jdoe@fake.test\\n';") + passwordPrompt("a", "LATER")
            + QStringLiteral("sleep 5"), error));
        QVERIFY(tab.view.bindCredential(m_passwordId));
        TerminalTestAccess::noteTypedSsh(tab.view, {{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")}});
        QVERIFY(waitFor(tab, "ssh jdoe@fake.test\r\njdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("LATER"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("LATER:typed\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC3 blocker, the reviewer's repro: a bound tab, ssh logs in with a key,
    // the host shows a motd (or not) and a prompt, the user runs cat, and the
    // file ends in an exact ssh prompt. The fake remote gets nothing.
    void boundTabCatOfAPromptAfterKeyLoginGetsNothing_data() {
        QTest::addColumn<QString>("prompt");
        QTest::addColumn<bool>("motd");
        const QStringList prompts{QStringLiteral("jdoe@fake:~$ "), QStringLiteral("\\342\\236\\234  ~ "),
            QStringLiteral("\\033[1;32m\\342\\236\\234  \\033[36m~\\033[0m \\033[s\\033[60C[10:12]\\033[u"),
            QStringLiteral("[jdoe@fake ~] "), QStringLiteral("\\342\\213\\212> ~ "), QStringLiteral("\\316\\273 "),
            QStringLiteral("\\342\\225\\255\\342\\224\\200 jdoe@fake ~\\n\\342\\225\\260\\342\\224\\200 ")};
        const char* names[] = {"plain", "robbyrussell", "rprompt", "[jdoe@h ~]", "fish", "lambda", "powerline"};
        for (int i = 0; i < prompts.size(); ++i) {
            QTest::newRow(qPrintable(QStringLiteral("%1 motd").arg(QLatin1String(names[i])))) << prompts[i] << true;
            QTest::newRow(qPrintable(QStringLiteral("%1 quiet").arg(QLatin1String(names[i])))) << prompts[i] << false;
        }
    }
    void boundTabCatOfAPromptAfterKeyLoginGetsNothing() {
        QFETCH(QString, prompt);
        QFETCH(bool, motd);
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("stty -echo; sleep 0.3; printf '\\n';")
            + (motd ? QStringLiteral("printf 'Welcome to fake.test\\nLast login: Fri Oct 30 09:12:01 2026\\n';") : QString{})
            + QStringLiteral("printf '%1'; IFS= read -r cmd; printf 'meeting at ten\\n';").arg(prompt)
            + passwordPrompt("a", "REMOTE_GOT") + QStringLiteral("sleep 5"), error));
        QVERIFY(tab.view.bindCredential(m_passwordId));
        TerminalTestAccess::noteTypedSsh(tab.view, {{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")}});
        QVERIFY(waitFor(tab, "\n", 3000));
        QTest::qWait(400); // the prompt is on screen
        tab.view.sendText(QStringLiteral("cat notes.txt\r"));
        QVERIFY(waitFor(tab, "meeting at ten\r\njdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("REMOTE_GOT"));
        QVERIFY(!tab.view.credentialBound()); // closed for good, and let go
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("REMOTE_GOT:typed\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // A real key press closes the window; a modifier alone does not.
    void typingAfterLoginClosesTheWindow() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("stty -echo; IFS= read -r first; printf '\\n\\316\\273 ';"
            "IFS= read -r cmd; printf 'x\\n';") + passwordPrompt("a", "LATER") + QStringLiteral("sleep 5"), error));
        QVERIFY(tab.view.bindCredential(m_passwordId));
        TerminalTestAccess::press(tab.view, Qt::Key_Return, QStringLiteral("\r")); // no typed ssh yet: nothing to close
        QVERIFY(tab.view.credentialBound());
        TerminalTestAccess::noteTypedSsh(tab.view, {{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")}});
        QVERIFY(waitFor(tab, "\xce\xbb "));
        TerminalTestAccess::press(tab.view, Qt::Key_Shift);
        QVERIFY(tab.view.credentialBound());
        TerminalTestAccess::press(tab.view, Qt::Key_L, QStringLiteral("l"));
        QVERIFY(!tab.view.credentialBound());
        TerminalTestAccess::press(tab.view, Qt::Key_Return, QStringLiteral("\r"));
        QVERIFY(waitFor(tab, "jdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("LATER"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("LATER:typed\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC3 limit (a): a shell prompt and an exact ssh prompt in one read.
    void shellPromptAndSshPromptInOneReadGetNothing() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("stty -echo; sleep 0.3;"
            "printf '\\njdoe@fake:~$ \\njdoe@fake.test'\"'\"'s password: '; IFS= read -r a; printf 'ONE_READ:%s\\n' \"$a\" >> '%1'; sleep 5")
            .arg(receivedPath()), error));
        QVERIFY(tab.view.bindCredential(m_passwordId));
        TerminalTestAccess::noteTypedSsh(tab.view, {{QStringLiteral("host"), QStringLiteral("fake.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")}});
        QVERIFY(waitFor(tab, "jdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("ONE_READ"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("ONE_READ:typed\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // ProxyJump: the credential answers the jump host for its own user, and
    // the target either way.
    void proxyJumpWithMatchingCredential() {
        Tab tab; attach(tab);
        QVariantMap profile = session(m_passwordId);
        profile.insert(QStringLiteral("proxyJump"), QStringLiteral("jdoe@jump.test:2222"));
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, profile, QStringLiteral("stty -echo;")
            + passwordPrompt("a", "JUMP", QStringLiteral("jdoe@jump.test")) + passwordPrompt("b", "TARGET")
            + QStringLiteral("sleep 5"), error), qPrintable(error));
        QVERIFY(waitForReceived("JUMP:" + secret + "\n"));
        QVERIFY(waitForReceived("TARGET:" + secret + "\n"));
        QCOMPARE(received().count(secret), 2);
        TerminalTestAccess::stopFixture(tab.view);
    }

    void proxyJumpWithOtherUserIsLeftToTheUser() {
        Tab tab; attach(tab);
        QVariantMap profile = session(m_passwordId);
        profile.insert(QStringLiteral("proxyJump"), QStringLiteral("ops@jump.test"));
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, profile, QStringLiteral("stty -echo;")
            + passwordPrompt("a", "JUMP", QStringLiteral("ops@jump.test")) + passwordPrompt("b", "TARGET")
            + QStringLiteral("sleep 5"), error), qPrintable(error));
        QVERIFY(waitFor(tab, "ops@jump.test's password: "));
        QVERIFY(waitForStatus(tab, QStringLiteral("Answer it yourself")));
        QVERIFY(!received().contains("JUMP"));
        tab.view.sendText(QStringLiteral("mine\r"));
        QVERIFY(waitForReceived("JUMP:mine\n"));
        QVERIFY(waitForReceived("TARGET:" + secret + "\n"));
        QCOMPARE(received().count(secret), 1);
        TerminalTestAccess::stopFixture(tab.view);
    }

    // A window closed for good lets go of the secret at once, in owned tabs too.
    void ownedForeignPromptReleasesTheCredential() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; sleep 0.3; printf '[sudo] password for jdoe: ';") + reads("a", "SUDO") + QStringLiteral("sleep 5"),
            error), qPrintable(error));
        QVERIFY(TerminalTestAccess::credentialArmed(tab.view));
        QVERIFY(waitFor(tab, "password for jdoe: "));
        QTRY_VERIFY_WITH_TIMEOUT(!TerminalTestAccess::credentialArmed(tab.view), 2000);
        tab.view.sendText(QStringLiteral("mine\r"));
        QVERIFY(waitForReceived("SUDO:mine\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC2 F1: ssh asked something the credential does not cover and the user
    // answered it. After the login, a later exact prompt gets nothing.
    void userAnsweredPromptThenLoginGetsNothingLater() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, session(m_passwordId), QStringLiteral(
            "stty -echo; printf '(jdoe@fake.test) Parola: ';") + reads("a", "PAROLA")
            + QStringLiteral("printf 'Welcome\\n'; sleep 0.3;") + passwordPrompt("b", "LATER") + QStringLiteral("sleep 5"),
            error), qPrintable(error));
        QVERIFY(waitFor(tab, "Parola: "));
        QVERIFY(waitForStatus(tab, QStringLiteral("Answer it yourself")));
        tab.view.sendText(QStringLiteral("mine\r"));
        QVERIFY(waitForReceived("PAROLA:mine\n"));
        QVERIFY(waitFor(tab, "Welcome\r\njdoe@fake.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("LATER"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("LATER:typed\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC B2: a bound tab never answers for a host other than the one typed,
    // and without a typed ssh it answers nothing.
    void boundTabNeedsTheTypedHost() {
        Tab tab; attach(tab);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("stty -echo; sleep 0.3;")
            + passwordPrompt("a", "UNTYPED") + passwordPrompt("b", "OTHER") + QStringLiteral("sleep 5"), error));
        QVERIFY(tab.view.bindCredential(m_passwordId));
        QVERIFY(waitFor(tab, "password: "));
        QTest::qWait(300);
        QVERIFY(received().isEmpty());
        TerminalTestAccess::noteTypedSsh(tab.view, {{QStringLiteral("host"), QStringLiteral("other.test")}, {QStringLiteral("user"), QStringLiteral("jdoe")}});
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("UNTYPED:typed\n"));
        QTest::qWait(300);
        QVERIFY(!received().contains("OTHER"));
        tab.view.sendText(QStringLiteral("typed\r"));
        QVERIFY(waitForReceived("OTHER:typed\n"));
        QVERIFY(!received().contains(secret));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // QC B2: a binding is short-lived. The default is minutes, not an hour.
    void boundCredentialExpires() {
        Tab tab; attach(tab);
        QVERIFY(TerminalTestAccess::boundCredentialMs(tab.view) <= 10 * 60 * 1000);
        QString error;
        QVERIFY(TerminalTestAccess::startFixture(tab.view, QStringLiteral("sleep 5"), error));
        TerminalTestAccess::setBoundCredentialMs(tab.view, 200);
        QVERIFY(tab.view.bindCredential(m_passwordId));
        QVERIFY(tab.view.credentialBound());
        QTRY_VERIFY_WITH_TIMEOUT(!tab.view.credentialBound(), 2000);
        QVERIFY(!TerminalTestAccess::credentialArmed(tab.view));
        TerminalTestAccess::stopFixture(tab.view);
    }

    // CyberArk: the session keeps its user, ssh logs in to the gateway with the
    // route's login string, and only the gateway's prompt gets the password.
    // A later prompt for the target host is not ssh's for the gateway login.
    void psmpCredentialAnswersTheGateway() {
        Tab tab; attach(tab);
        QVariantMap profile = psmpSession();
        QVERIFY(!profile.isEmpty());
        const auto login = ConnectionRouting::login(profile);
        QVERIFY2(login, qPrintable(login ? QString{} : login.error()));
        profile.insert(QStringLiteral("credential"), m_passwordId);
        QString error; QVariantMap effective;
        QVERIFY2(TerminalTestAccess::startCredentialFixture(tab.view, profile, QStringLiteral("stty -echo;")
            + passwordPrompt("a", "GATEWAY", *login + QStringLiteral("@psmp.example.test"))
            + passwordPrompt("b", "TARGET", QStringLiteral("jdoe@db.example.test")) + QStringLiteral("sleep 5"),
            error, &effective), qPrintable(error));
        QCOMPARE(effective.value(QStringLiteral("user")), profile.value(QStringLiteral("user")));
        QCOMPARE(effective.value(QStringLiteral("authenticationMode")).toString(), QStringLiteral("password"));
        QVERIFY(waitForReceived("GATEWAY:" + secret + "\n"));
        QVERIFY(waitFor(tab, "jdoe@db.example.test's password: "));
        QTest::qWait(300);
        QVERIFY(!received().contains("TARGET"));
        tab.view.sendText(QStringLiteral("x\r"));
        QVERIFY(waitForReceived("TARGET:x\n"));
        QCOMPARE(received().count(secret), 1);
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
