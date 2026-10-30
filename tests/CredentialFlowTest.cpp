#include <QtTest>

#include <bitshell/core/CredentialFlow.h>

#include <memory>

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
// A flow for ssh to jdoe@host, as TerminalView arms it.
std::unique_ptr<CredentialFlow> flowFor(Scope scope, bool passphrase = false, QByteArrayView host = "host") {
    auto flow = std::make_unique<CredentialFlow>(secrets(passphrase), scope);
    flow->setTarget("jdoe", host);
    return flow;
}
}

class CredentialFlowTest final : public QObject {
    Q_OBJECT
private slots:
    void passwordThenCodePauseThenContinues() {
        auto owned = flowFor(Scope::OwnedSsh, true); auto& flow = *owned;
        QCOMPARE(flow.feed("Warning: Permanently added 'host' (ED25519) to the list of known hosts.\r\n").action, Action::None);
        auto step = flow.feed("(jdoe@host) Username: ");
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
                                   "Enter your security code:", "MFA:", "PIN:",
                                   // Hardware tokens and second factors that name a password.
                                   "Enter PASSCODE for RSA SecurID:", "(jdoe@host) SecurID passcode: ", "YubiKey for `jdoe':",
                                   "(jdoe@host) Enter YubiKey OTP: ", "Second password:", "(jdoe@host) Second Password: ",
                                   "(jdoe@host) Passcode: ", "(jdoe@host) OTP: ", "(jdoe@host) SMS code: ", "Enter SMS:",
                                   "(jdoe@host) Password + token: "})
            QTest::newRow(prompt) << QByteArray(prompt);
    }
    void codePromptsAreNeverFilled() {
        QFETCH(QByteArray, line);
        auto owned = flowFor(Scope::OwnedSsh, true); auto& flow = *owned;
        const auto step = flow.feed(line);
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::OneTimeCode);
        QVERIFY(step.bytes.isEmpty());
    }

    void wrongPasswordRepeatStopsWithoutResending() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::Send);
        const auto step = flow.feed("\r\nPermission denied, please try again.\r\njdoe@host's password: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
        QVERIFY(step.bytes.isEmpty());
        QVERIFY(flow.stopped());
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::None);
    }

    void repeatedUsernameAlsoStops() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed("(jdoe@host) Username: ").action, Action::Send);
        QCOMPARE(flow.feed("(jdoe@host) Password: ").action, Action::Send);
        const auto step = flow.feed("\r\nLogin incorrect\r\n(jdoe@host) Username: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
    }

    void unknownPromptGoesToUserAndFlowStaysAlive() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        auto step = flow.feed("Are you sure you want to continue connecting (yes/no/[fingerprint])? ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::Other);
        QVERIFY(!flow.stopped());
        QCOMPARE(flow.feed("yes\r\n").action, Action::None);
        step = flow.feed("(jdoe@host) Your favourite colour? ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::Unknown);
        step = flow.feed("jdoe@host's password: ");
        QCOMPARE(step.action, Action::Send);
    }

    void missingSecretGoesToUser() {
        CredentialSecrets keyOnly;
        keyOnly.username = "jdoe";
        CredentialFlow flow(std::move(keyOnly), Scope::OwnedSsh);
        flow.setTarget("jdoe", "host");
        const auto step = flow.feed("jdoe@host's password: ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::Password);
    }

    void splitAcrossChunks() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed("jdoe@ho").action, Action::None);
        QCOMPARE(flow.feed("st's pass").action, Action::None);
        QCOMPARE(flow.feed("word: ").action, Action::Send);
    }

    void escapeCodesAroundPrompt() {
        {
            auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
            QCOMPARE(flow.feed("\x1b[1mjdoe@host's password:\x1b[0m \x1b[?25h").action, Action::Send);
        }
        {
            auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
            QCOMPARE(flow.feed("(jdoe@host) Password: \x1b]0;title\a").action, Action::Send);
        }
        QCOMPARE(CredentialFlow::promptLine("a\r\n\x1b[32mUser\x1b[0mname:  "), QByteArray("Username:"));
    }

    void bigChunkEndingInPrompt() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QByteArray chunk(1 << 20, 'y');
        chunk += "\r\njdoe@host's password: ";
        QCOMPARE(flow.feed(chunk).action, Action::Send);
    }

    void hostNamesNeverLookLikeCodes() {
        auto owned = flowFor(Scope::OwnedSsh, false, "TOKEN-otp-01"); auto& flow = *owned;
        QCOMPARE(flow.feed("jdoe@token-otp-01's password: ").action, Action::Send);
    }

    void ownedSshDisarmsAfterLongPreamble() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        const auto step = flow.feed(QByteArray(CredentialFlow::preambleBytes + 1, 'b'));
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
    }

    void adHocOnlyAnswersSshPromptForBoundUser() {
        CredentialFlow flow(secrets(true), Scope::Shell);
        // No typed ssh yet: nothing is answered, at no byte cost.
        QCOMPARE(flow.feed("jdoe@db01's password: ").action, Action::None);
        QCOMPARE(flow.feed(QByteArray(1 << 16, 'z')).action, Action::None);
        flow.setTarget("jdoe", "db01");
        QCOMPARE(flow.feed("[sudo] password for jdoe: ").action, Action::None);
        QCOMPARE(flow.feed("\r\nPassword: ").action, Action::None);
        QCOMPARE(flow.feed("\r\nroot@db01's password: ").action, Action::None);
        QCOMPARE(flow.feed("\r\nWhat is your name? ").action, Action::None); // not engaged yet
        QCOMPARE(flow.feed("\r\nEnter passphrase for key '/home/jdoe/.ssh/id_ed25519': ").action, Action::None);
        auto step = flow.feed("\r\nJDoe@DB01's password: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("s3cret pw\r"));
        // Any prompt that is not ssh's after the answer ends the round.
        step = flow.feed("\r\nUnexpected question? ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
        QCOMPARE(flow.feed("\r\njdoe@db01's password: ").action, Action::None);
        // Back at the local prompt: the target is gone until the next typed ssh.
        flow.restart();
        QCOMPARE(flow.feed("jdoe@db02's password: ").action, Action::None);
        flow.setTarget("jdoe", "db02");
        QCOMPARE(flow.feed("\r\njdoe@db02's password: ").action, Action::Send);
    }

    // QC B2: the right user at another host gets nothing.
    void boundTabNeedsTheTypedHost() {
        CredentialFlow flow(secrets(), Scope::Shell);
        flow.setTarget("jdoe", "db01");
        for (const char* prompt : {"jdoe@db02's password: ", "(jdoe@db02) Password: ", "jdoe@db01.evil's password: ",
                                   "(jdoe@db011) Password: ", "jdoe@db0's password: "})
            QCOMPARE(flow.feed(QByteArray("\r\n") + prompt).action, Action::None);
        QCOMPARE(flow.feed("\r\n(jdoe@db01) Password: ").action, Action::Send);
    }

    // QC B2: a bound round expires by output size, not only by time.
    void boundWindowExpiresByBytes() {
        CredentialFlow flow(secrets(), Scope::Shell);
        flow.setTarget("jdoe", "db01");
        QCOMPARE(flow.feed(QByteArray(CredentialFlow::shellWindowBytes, 'm')).action, Action::None);
        const auto step = flow.feed("m");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
        QCOMPARE(flow.feed("\r\njdoe@db01's password: ").action, Action::None);
    }

    // QC B1: only OpenSSH's own prompt for the target user@host gets the secret.
    void foreignPasswordPromptsNeverGetTheSecret_data() {
        QTest::addColumn<QByteArray>("line");
        for (const char* prompt : {"[sudo] password for jdoe:", "Password:", "password:", "Vault password:",
                                   "Enter password:", "jdoe's password:", "root@host's password:", "jdoe@other's password:",
                                   "(root@host) Password:", "(jdoe@other) Password:", "x jdoe@host's password:",
                                   "jdoe@host's password for vault:", "Enter passphrase for key '/home/jdoe/.ssh/id':",
                                   "login:", "Username:", "(jdoe@host) New password:"})
            QTest::newRow(prompt) << QByteArray(prompt);
    }
    void foreignPasswordPromptsNeverGetTheSecret() {
        QFETCH(QByteArray, line);
        auto owned = flowFor(Scope::OwnedSsh, true); auto& flow = *owned;
        const auto step = flow.feed(line + ' ');
        QVERIFY(step.action != Action::Send);
        QVERIFY(step.bytes.isEmpty());
    }

    void noTargetNoAnswer() {
        CredentialFlow flow(secrets(true), Scope::OwnedSsh);
        QVERIFY(flow.feed("jdoe@host's password: ").action != Action::Send);
        // A key credential's own temporary file needs no target.
        QCOMPARE(flow.feed("\r\nEnter passphrase for key '/run/user/1000/8bitShell-key/id': ").action, Action::Send);
    }

    // QC B1: a successful login closes the window at its first line of output.
    void loginClosesTheWindow() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::Send);
        QCOMPARE(flow.feed("\r\n").action, Action::None); // ssh's newline after the hidden input
        const auto step = flow.feed("Welcome to host\r\n");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::None);
    }

    void foreignPromptAfterAnswerClosesTheWindow() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed("jdoe@host's password: ").action, Action::Send);
        const auto step = flow.feed("\r\n[sudo] password for jdoe: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(!step.rejected);
    }

    // Instructions and the prompt in one read still give the 2FA hint.
    void duoInstructionsWithPromptStillPause() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed("(jdoe@host) Password: ").action, Action::Send);
        const auto step = flow.feed("\r\nDuo two-factor login for jdoe\r\n\r\nEnter a passcode or select one of the following options:\r\n\r\n"
                                    " 1. Duo Push to XXX-XXX-1234\r\n\r\n(jdoe@host) Passcode or option (1-1): ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::OneTimeCode);
    }

    void sshAddBadPassphraseIsNotRetried() {
        CredentialFlow flow(secrets(true), Scope::OwnedSsh);
        QCOMPARE(flow.feed("Enter passphrase for /run/user/1000/8bitShell-key/id: ").action, Action::Send);
        const auto step = flow.feed("\r\nBad passphrase, try again for /run/user/1000/8bitShell-key/id: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
    }

    void ordinaryOutputIsIgnored() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        for (const char* text : {"total 12\r\n", "drwxr-xr-x 2 jdoe jdoe 4096 .\r\n", "jdoe@host:~$ ", "Last login: Fri\r\n"})
            QCOMPARE(flow.feed(text).action, Action::None);
    }
};

QTEST_GUILESS_MAIN(CredentialFlowTest)
#include "CredentialFlowTest.moc"
