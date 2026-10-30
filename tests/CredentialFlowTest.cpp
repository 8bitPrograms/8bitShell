#include <QtTest>

#include <bitshell/core/CredentialFlow.h>

using bitshell::core::CredentialFlow;
using bitshell::core::CredentialSecrets;
using Action = CredentialFlow::Action;
using Prompt = CredentialFlow::Prompt;
using Scope = CredentialFlow::Scope;

namespace {
CredentialSecrets secrets(bool passphrase = false) {
    CredentialSecrets value;
    value.username = "jdoe";
    value.password = "s3cret pw";
    if (passphrase) {
        value.passphrase = "key phrase";
        value.keyPath = "/run/user/1000/8bitShell-key/id";
    }
    return value;
}
}

class CredentialFlowTest final : public QObject {
    Q_OBJECT
private slots:
    void passwordThenCodePauseThenContinues() {
        CredentialFlow flow(secrets(true), Scope::OwnedSsh);
        QCOMPARE(flow.feed("Warning: Permanently added 'host' (ED25519) to the list of known hosts.\r\n").action, Action::None);
        auto step = flow.feed("login: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("jdoe\r"));
        step = flow.feed("jdoe\r\n(jdoe@host) Password: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("s3cret pw\r"));
        step = flow.feed("\r\n(jdoe@host) Verification code: ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::OneTimeCode);
        QVERIFY(step.bytes.isEmpty());
        // The user types the code. Its echo and newline are ordinary output.
        QCOMPARE(flow.feed("\r\n").action, Action::None);
        QVERIFY(!flow.stopped());
        step = flow.feed("Enter passphrase for key '/run/user/1000/8bitShell-key/id': ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("key phrase\r"));
        // Logged in: ordinary output past the settle budget disarms the flow.
        step = flow.feed(QByteArray(CredentialFlow::settledBytes + 1, 'x'));
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
        QVERIFY(flow.stopped());
    }

    void codePromptsAreNeverFilled_data() {
        QTest::addColumn<QByteArray>("line");
        for (const char* prompt : {"Verification code:", "(jdoe@host) Verification code: ", "One-time password (OATH) for `jdoe':",
                                   "Enter OTP:", "Duo two-factor login for jdoe\r\nPasscode or option (1-3):", "Token:",
                                   "Enter PASSCODE:", "2FA code:", "Authenticator code:", "Password + token:",
                                   "Enter your security code:", "MFA:", "PIN:"})
            QTest::newRow(prompt) << QByteArray(prompt);
    }
    void codePromptsAreNeverFilled() {
        QFETCH(QByteArray, line);
        CredentialFlow flow(secrets(true), Scope::OwnedSsh);
        const auto step = flow.feed(line);
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::OneTimeCode);
        QVERIFY(step.bytes.isEmpty());
    }

    void wrongPasswordRepeatStopsWithoutResending() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::Send);
        const auto step = flow.feed("\r\nPermission denied, please try again.\r\njdoe@host's password: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
        QVERIFY(step.bytes.isEmpty());
        QVERIFY(flow.stopped());
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::None);
    }

    void repeatedUsernameAlsoStops() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        QCOMPARE(flow.feed("Username: ").action, Action::Send);
        QCOMPARE(flow.feed("Password: ").action, Action::Send);
        const auto step = flow.feed("\r\nLogin incorrect\r\nUsername: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
    }

    void unknownPromptGoesToUserAndFlowStaysAlive() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        auto step = flow.feed("Are you sure you want to continue connecting (yes/no/[fingerprint])? ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::Unknown);
        QVERIFY(!flow.stopped());
        QCOMPARE(flow.feed("yes\r\n").action, Action::None);
        step = flow.feed("jdoe@host's password: ");
        QCOMPARE(step.action, Action::Send);
    }

    void missingSecretGoesToUser() {
        CredentialSecrets keyOnly;
        keyOnly.username = "jdoe";
        CredentialFlow flow(std::move(keyOnly), Scope::OwnedSsh);
        const auto step = flow.feed("jdoe@host's password: ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::Password);
    }

    void splitAcrossChunks() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        QCOMPARE(flow.feed("jdoe@ho").action, Action::None);
        QCOMPARE(flow.feed("st's pass").action, Action::None);
        QCOMPARE(flow.feed("word: ").action, Action::Send);
    }

    void escapeCodesAroundPrompt() {
        {
            CredentialFlow flow(secrets(), Scope::OwnedSsh);
            QCOMPARE(flow.feed("\x1b[1mjdoe@host's password:\x1b[0m \x1b[?25h").action, Action::Send);
        }
        {
            CredentialFlow flow(secrets(), Scope::OwnedSsh);
            QCOMPARE(flow.feed("Password: \x1b]0;title\a").action, Action::Send);
        }
        QCOMPARE(CredentialFlow::promptLine("a\r\n\x1b[32mUser\x1b[0mname:  "), QByteArray("Username:"));
    }

    void bigChunkEndingInPrompt() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        QByteArray chunk(1 << 20, 'y');
        chunk += "\r\njdoe@host's password: ";
        QCOMPARE(flow.feed(chunk).action, Action::Send);
    }

    void hostNamesNeverLookLikeCodes() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        QCOMPARE(flow.feed("jdoe@token-otp-01's password: ").action, Action::Send);
    }

    void ownedSshDisarmsAfterLongPreamble() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        const auto step = flow.feed(QByteArray(CredentialFlow::preambleBytes + 1, 'b'));
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
    }

    void adHocOnlyAnswersSshPromptForBoundUser() {
        CredentialFlow flow(secrets(true), Scope::Shell);
        QCOMPARE(flow.feed("[sudo] password for jdoe: ").action, Action::None);
        QCOMPARE(flow.feed("\r\nPassword: ").action, Action::None);
        QCOMPARE(flow.feed("\r\nroot@host's password: ").action, Action::None);
        QCOMPARE(flow.feed("\r\nWhat is your name? ").action, Action::None); // not engaged yet
        QCOMPARE(flow.feed("\r\nEnter passphrase for key '/home/jdoe/.ssh/id_ed25519': ").action, Action::None);
        QCOMPARE(flow.feed(QByteArray(1 << 16, 'z')).action, Action::None); // no budget before engagement
        auto step = flow.feed("\r\nJDoe@db01's password: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("s3cret pw\r"));
        step = flow.feed("\r\nUnexpected question? ");
        QCOMPARE(step.action, Action::HandToUser);
        // Back at the local prompt: a new round may answer again.
        QCOMPARE(flow.feed("\r\njdoe@db01's password: ").action, Action::Stop);
        flow.restart();
        QCOMPARE(flow.feed("jdoe@db02's password: ").action, Action::Send);
    }

    void ordinaryOutputIsIgnored() {
        CredentialFlow flow(secrets(), Scope::OwnedSsh);
        for (const char* text : {"total 12\r\n", "drwxr-xr-x 2 jdoe jdoe 4096 .\r\n", "jdoe@host:~$ ", "Last login: Fri\r\n"})
            QCOMPARE(flow.feed(text).action, Action::None);
    }
};

QTEST_GUILESS_MAIN(CredentialFlowTest)
#include "CredentialFlowTest.moc"
