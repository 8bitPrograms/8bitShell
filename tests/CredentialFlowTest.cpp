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
        QCOMPARE(flow.feed("jdoe\r\n(jdoe@host) Password: ").action, Action::Send);
        const auto step = flow.feed("\r\nLogin incorrect\r\n(jdoe@host) Username: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
    }

    void unknownPromptGoesToUserAndFlowStaysAlive() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        auto step = flow.feed("Are you sure you want to continue connecting (yes/no/[fingerprint])? ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::HostKey);
        QVERIFY(!flow.stopped());
        QCOMPARE(flow.feed("yes\r\nWarning: Permanently added 'host' (ED25519) to the list of known hosts.\r\n").action, Action::None);
        step = flow.feed("(jdoe@host) Your favourite colour? ");
        QCOMPARE(step.action, Action::HandToUser);
        QCOMPARE(step.prompt, Prompt::Unknown);
        // The user's answer ends the line, as ssh prints a newline after it.
        step = flow.feed("\r\njdoe@host's password: ");
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
        const QByteArray prompt = "\r\njdoe@host's password: ";
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        QCOMPARE(flow.feed(QByteArray(CredentialFlow::preambleBytes - prompt.size(), 'y') + prompt).action, Action::Send);
        auto over = flowFor(Scope::OwnedSsh);
        QCOMPARE(over->feed(QByteArray(1 << 20, 'y') + prompt).action, Action::Stop);
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
        // ssh's host-key question is answered by the user and keeps the window open.
        QCOMPARE(flow.feed("Are you sure you want to continue connecting (yes/no/[fingerprint])? ").action, Action::None);
        auto step = flow.feed("yes\r\n\r\nJDoe@DB01's password: ");
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

    // QC B2: the right user at another host gets nothing, and that prompt
    // closes the window.
    void boundTabNeedsTheTypedHost() {
        for (const char* prompt : {"jdoe@db02's password: ", "(jdoe@db02) Password: ", "jdoe@db01.evil's password: ",
                                   "(jdoe@db011) Password: ", "jdoe@db0's password: "}) {
            CredentialFlow flow(secrets(), Scope::Shell);
            flow.setTarget("jdoe", "db01");
            QCOMPARE(flow.feed(QByteArray("\r\n") + prompt).action, Action::Stop);
            QCOMPARE(flow.feed("\r\n(jdoe@db01) Password: ").action, Action::None);
        }
        CredentialFlow flow(secrets(), Scope::Shell);
        flow.setTarget("jdoe", "db01");
        QCOMPARE(flow.feed("\r\n(jdoe@db01) Password: ").action, Action::Send);
    }

    // QC2 F1: any prompt that is not ssh's for the target closes the window,
    // even though nothing was sent, in both kinds of flow.
    void foreignPromptClosesTheWindow_data() {
        QTest::addColumn<QByteArray>("line");
        for (const char* prompt : {"[sudo] password for jdoe: ", "Password: ", "root@db01's password: ", "What is your name? ",
                                   "Enter passphrase for key '/home/jdoe/.ssh/id_ed25519': ", "Press Enter to accept: "})
            QTest::newRow(prompt) << QByteArray(prompt);
    }
    void foreignPromptClosesTheWindow() {
        QFETCH(QByteArray, line);
        for (const Scope scope : {Scope::OwnedSsh, Scope::Shell}) {
            auto flow = flowFor(scope, false, "db01");
            QVERIFY(flow->feed(line).action != Action::Send);
            QVERIFY(flow->stopped());
            QCOMPARE(flow->feed("typed by the user\r\njdoe@db01's password: ").action, Action::None);
        }
    }

    // QC2 F1: ssh asked something the user answered (an unrecognised prompt, or
    // a code first); once the login is over, a later exact prompt gets nothing.
    void loginEndsAfterUserAnswers_data() {
        QTest::addColumn<QByteArray>("line");
        QTest::newRow("unrecognised") << QByteArray("(jdoe@db01) Parola: ");
        QTest::newRow("code first") << QByteArray("(jdoe@db01) Verification code: ");
    }
    void loginEndsAfterUserAnswers() {
        QFETCH(QByteArray, line);
        for (const Scope scope : {Scope::OwnedSsh, Scope::Shell}) {
            auto flow = flowFor(scope, false, "db01");
            QCOMPARE(flow->feed(line).action, Action::HandToUser);
            QCOMPARE(flow->feed("\r\n").action, Action::None);
            QCOMPARE(flow->feed("Welcome to db01\r\n").action, Action::Stop);
            QCOMPARE(flow->feed("jdoe@db01's password: ").action, Action::None);
        }
    }

    // QC3 blocker: after a key login nothing in the output, whatever the
    // remote prompt looks like, can collect the secret. The window closes on
    // events: the first line that is not the login's, or the first keystroke.
    void keyLoginCatOfAPromptGetsNothing_data() {
        QTest::addColumn<QByteArray>("prompt");
        QTest::newRow("plain $") << QByteArray("\x1b[01;32mjdoe@db01\x1b[00m:\x1b[01;34m~\x1b[00m$ ");
        QTest::newRow("robbyrussell") << QByteArray("\x1b[1;32m\xe2\x9e\x9c  \x1b[36m~\x1b[00m ");
        QTest::newRow("rprompt") << QByteArray("\xe2\x9e\x9c  ~ \x1b[s\x1b[60C\x1b[2m[10:12:01]\x1b[0m\x1b[u");
        QTest::newRow("[jdoe@h ~]") << QByteArray("[jdoe@db01 ~] ");
        QTest::newRow("fish") << QByteArray("\xe2\x8b\x8a> ~ ");
        QTest::newRow("lambda") << QByteArray("\xce\xbb ");
        QTest::newRow("powerline") << QByteArray("\xe2\x95\xad\xe2\x94\x80 jdoe@db01 ~\r\n\xe2\x95\xb0\xe2\x94\x80 ");
    }
    void keyLoginCatOfAPromptGetsNothing() {
        QFETCH(QByteArray, prompt);
        const QByteArray cat = "cat notes.txt\r\nmeeting at ten\r\njdoe@db01's password: ";
        // With a motd: its first line closes the window.
        CredentialFlow motd(secrets(), Scope::Shell);
        motd.setTarget("jdoe", "db01");
        QCOMPARE(motd.feed("\r\n").action, Action::None); // the Enter that launched ssh
        QCOMPARE(motd.feed("Welcome to db01\r\nLast login: Fri Oct 30 09:12:01 2026\r\n" + prompt).action, Action::Stop);
        QCOMPARE(motd.feed(cat).action, Action::None);
        // Without one: the prompt alone settles nothing, the keystroke closes.
        CredentialFlow quiet(secrets(), Scope::Shell);
        quiet.setTarget("jdoe", "db01");
        QCOMPARE(quiet.feed("ssh jdoe@db01\r\n").action, Action::None); // a repainted command echo
        QVERIFY(quiet.feed(prompt).action != Action::Send);
        if (!quiet.stopped()) QVERIFY(quiet.noteUserInput());
        QVERIFY(quiet.stopped());
        const auto step = quiet.feed(cat);
        QCOMPARE(step.action, Action::None);
        QVERIFY(step.bytes.isEmpty());
    }

    // Typing closes a bound window, except into a prompt ssh left to the user.
    void typingClosesTheBoundWindow() {
        CredentialFlow flow(secrets(), Scope::Shell);
        QVERIFY(!flow.noteUserInput()); // no typed ssh yet: nothing to close
        flow.setTarget("jdoe", "db01");
        QCOMPARE(flow.feed("Are you sure you want to continue connecting (yes/no/[fingerprint])? ").action, Action::None);
        QVERIFY(!flow.noteUserInput()); // the user answers ssh's question
        QCOMPARE(flow.feed("yes\r\nWarning: Permanently added 'db01' (ED25519) to the list of known hosts.\r\n").action, Action::None);
        QCOMPARE(flow.feed("jdoe@db01's password: ").action, Action::Send);
        QCOMPARE(flow.feed("\r\n").action, Action::None);
        QVERIFY(flow.noteUserInput());
        QCOMPARE(flow.feed("jdoe@db01's password: ").action, Action::None);

        CredentialFlow early(secrets(), Scope::Shell);
        early.setTarget("jdoe", "db01");
        QVERIFY(early.noteUserInput()); // typed before ssh asked anything
        QCOMPARE(early.feed("jdoe@db01's password: ").action, Action::None);

        auto owned = flowFor(Scope::OwnedSsh); // an owned login keeps its own rules
        QVERIFY(!owned->noteUserInput());
        QCOMPARE(owned->feed("jdoe@host's password: ").action, Action::Send);
    }

    // QC3 limit (a): every complete line counts, not only the last.
    void shellPromptAndSshPromptInOneReadGetNothing() {
        for (const QByteArray read : {QByteArray("jdoe@db01:~$ \r\njdoe@db01's password: "),
                                      QByteArray("Last login: Fri Oct 30 09:12:01 2026\r\njdoe@db01's password: "),
                                      QByteArray("\xe2\x9e\x9c  ~ cat x\r\n(jdoe@db01) Password: ")}) {
            CredentialFlow flow(secrets(), Scope::Shell);
            flow.setTarget("jdoe", "db01");
            const auto step = flow.feed(read);
            QCOMPARE(step.action, Action::Stop);
            QVERIFY(step.bytes.isEmpty());
        }
    }

    // ProxyJump: the jump host's prompt is part of the login. The credential
    // answers it only for its own user there; the target is answered either way.
    void proxyJumpWithMatchingCredential() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        flow.setJumpHosts("JDoe@Jump.example:2222, bastion");
        auto step = flow.feed("jdoe@jump.example's password: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("s3cret pw\r"));
        step = flow.feed("\r\n(jdoe@bastion) Password: "); // ssh picks bastion's user
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(flow.feed("\r\nThe authenticity of host 'host (<no hostip for proxy command>)' can't be established.\r\n"
                           "ED25519 key fingerprint is SHA256:abc.\r\n").action, Action::None);
        QCOMPARE(flow.feed("Are you sure you want to continue connecting (yes/no/[fingerprint])? ").action, Action::HandToUser);
        QCOMPARE(flow.feed("yes\r\nWarning: Permanently added 'host' (ED25519) to the list of known hosts.\r\n").action, Action::None);
        step = flow.feed("jdoe@host's password: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("s3cret pw\r"));
        // A jump that asks again was refused: no second try.
        auto again = flowFor(Scope::OwnedSsh);
        again->setJumpHosts("jump.example");
        QCOMPARE(again->feed("jdoe@jump.example's password: ").action, Action::Send);
        step = again->feed("\r\nPermission denied, please try again.\r\njdoe@jump.example's password: ");
        QCOMPARE(step.action, Action::Stop);
        QVERIFY(step.rejected);
    }

    void proxyJumpWithOtherUserIsLeftToTheUser() {
        auto owned = flowFor(Scope::OwnedSsh); auto& flow = *owned;
        flow.setJumpHosts("ops@jump.example,[fd00::1]:22");
        auto step = flow.feed("ops@jump.example's password: ");
        QCOMPARE(step.action, Action::HandToUser);
        QVERIFY(step.bytes.isEmpty());
        QVERIFY(!flow.stopped());
        step = flow.feed("\r\nadmin@fd00::1's password: ");
        QCOMPARE(step.action, Action::HandToUser);
        QVERIFY(step.bytes.isEmpty());
        step = flow.feed("\r\njdoe@host's password: ");
        QCOMPARE(step.action, Action::Send);
        QCOMPARE(step.bytes, QByteArray("s3cret pw\r"));
        // A prompt for a host that is no jump stays foreign.
        auto other = flowFor(Scope::OwnedSsh);
        other->setJumpHosts("jump.example");
        QCOMPARE(other->feed("jdoe@elsewhere's password: ").action, Action::HandToUser);
        QVERIFY(other->stopped());
    }

    // QC2 F2: the byte budget counts the read that carries the prompt.
    void oversizedReadEndingInPromptGetsNothing() {
        const QByteArray prompt = "\r\njdoe@db01's password: ";
        CredentialFlow flow(secrets(), Scope::Shell);
        flow.setTarget("jdoe", "db01");
        // Blank filler: only the byte budget can end the window here.
        const auto step = flow.feed(QByteArray(9000 - prompt.size(), ' ') + prompt);
        QVERIFY(step.action != Action::Send);
        QVERIFY(step.bytes.isEmpty());
        CredentialFlow fits(secrets(), Scope::Shell);
        fits.setTarget("jdoe", "db01");
        QCOMPARE(fits.feed(QByteArray(CredentialFlow::shellWindowBytes - prompt.size(), ' ') + prompt).action, Action::Send);
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
        // A key credential's own temporary file needs no target.
        QCOMPARE(flow.feed("Enter passphrase for key '/run/user/1000/8bitShell-key/id': ").action, Action::Send);
        QVERIFY(flow.feed("\r\njdoe@host's password: ").action != Action::Send);
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
        for (const char* text : {"total 12\r\n", "drwxr-xr-x 2 jdoe jdoe 4096 .\r\n", "Last login: Fri\r\n"})
            QCOMPARE(flow.feed(text).action, Action::None);
    }
};

QTEST_GUILESS_MAIN(CredentialFlowTest)
#include "CredentialFlowTest.moc"
