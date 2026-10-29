#include <QtTest>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/NativeSshCommand.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/SshSecurity.h>

using namespace bitshell::core;

class NativeSshCommandTest : public QObject {
    Q_OBJECT
    QTemporaryDir temporary;
    QByteArray previousProfile;

    static QVariantMap profile() {
        return {{QStringLiteral("host"), QStringLiteral("db.example.test")},
                {QStringLiteral("user"), QStringLiteral("opc")},
                {QStringLiteral("port"), 2222},
                {QStringLiteral("identityKey"), QStringLiteral("none")}};
    }
    static QVariantMap withKey(const QString& key) {
        auto value = profile();
        value.remove(QStringLiteral("identityKey"));
        value[QStringLiteral("identityFile")] = key;
        return value;
    }
    static QVariantMap route() {
        return {{QStringLiteral("type"), QStringLiteral("cyberark-psmp")},
                {QStringLiteral("host"), QStringLiteral("psmp.example.test")},
                {QStringLiteral("port"), 2200},
                {QStringLiteral("account"), QVariantMap{
                    {QStringLiteral("source"), QStringLiteral("identity")},
                    {QStringLiteral("suffix"), QStringLiteral("SYS")},
                    {QStringLiteral("domain"), QStringLiteral("corp.example.test")}}}};
    }
private slots:
    void initTestCase() {
        QVERIFY(temporary.isValid());
        previousProfile = qgetenv("BIT8SHELL_PROFILE");
    }
    void init() {
        qputenv("BIT8SHELL_PROFILE", temporary.filePath(QString::fromLatin1(QTest::currentTestFunction())).toUtf8());
    }
    void cleanupTestCase() {
        if (previousProfile.isNull()) qunsetenv("BIT8SHELL_PROFILE");
        else qputenv("BIT8SHELL_PROFILE", previousProfile);
    }
    void nativeClientsRetainTrustChecksAndSavedOptions() {
        auto value = profile();
        value[QStringLiteral("proxyJump")] = QStringLiteral("jumpuser@jump.example.test:2201");
        value[QStringLiteral("compression")] = true;
        value[QStringLiteral("forwardX11")] = true;
        for (const auto& kind : {QStringLiteral("powershell"), QStringLiteral("bash"), QStringLiteral("wsl-bash")}) {
            const auto result = NativeSshCommand::command(value, kind);
            QVERIFY2(result, qPrintable(result ? QString{} : result.error()));
            QVERIFY(result->contains(QStringLiteral("'-oStrictHostKeyChecking=ask'")));
            QVERIFY(result->contains(QStringLiteral("'-oForwardAgent=no'")));
            QVERIFY(result->contains(QStringLiteral("'-oPermitLocalCommand=no'")));
            QVERIFY(result->contains(QStringLiteral("'-p' '2222'")));
            QVERIFY(result->contains(QStringLiteral("'-J' 'jumpuser@jump.example.test:2201'")));
            QVERIFY(result->contains(QStringLiteral("'-C'")));
            QVERIFY(result->contains(QStringLiteral("'-X'")));
            QVERIFY(result->endsWith(QStringLiteral("'-l' 'opc' '--' 'db.example.test'")));
            QVERIFY(!result->contains(QStringLiteral("UserKnownHostsFile")));
            QVERIFY(!result->contains(QStringLiteral("StrictHostKeyChecking=no")));
            if (kind == QStringLiteral("powershell")) {
#ifdef Q_OS_WIN
                QVERIFY(result->startsWith(QStringLiteral("& (& { $bitshellNativeSsh = Get-Command 'ssh.exe' ")));
                QVERIFY(result->contains(QStringLiteral("if ($null -ne $bitshellNativeSsh) { $bitshellNativeSsh.Source } elseif (Test-Path -LiteralPath ")));
                QString fallback = Platform::sshExecutable(); fallback.replace(u'\'', QStringLiteral("''"));
                QVERIFY(result->contains(QStringLiteral("-LiteralPath '%1' -PathType Leaf").arg(fallback)));
#else
                QVERIFY(result->startsWith(QStringLiteral("& (Get-Command ")));
#endif
                QVERIFY(result->contains(QStringLiteral("-CommandType Application")));
            } else {
                QVERIFY(result->startsWith(QStringLiteral("command ssh ")));
                QVERIFY(result->contains(QStringLiteral("'-F' '/dev/null'")));
                QVERIFY(!result->contains(QStringLiteral("ssh.exe")));
            }
        }
        auto v6 = profile();
        v6[QStringLiteral("host")] = QStringLiteral("[2001:db8::1]");
        const auto command = NativeSshCommand::command(v6, QStringLiteral("bash"));
        QVERIFY(command);
        QVERIFY(command->endsWith(QStringLiteral("'--' '2001:db8::1'")));
    }
    void rejectsUnresolvedUnsafeOrMissingCredentials() {
        QVERIFY(!NativeSshCommand::command(profile(), QStringLiteral("cmd")));
        for (const auto& host : {QStringLiteral("-oProxyCommand=calc"), QStringLiteral("example.test;id"),
                                QStringLiteral("example.test\nwhoami"), QStringLiteral("$(id)")}) {
            auto value = profile(); value[QStringLiteral("host")] = host;
            QVERIFY(!NativeSshCommand::command(value, QStringLiteral("powershell")));
        }
        auto value = profile();
        value[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("inherit")}};
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("bash")));
        value = profile(); value[QStringLiteral("identityKey")] = QStringLiteral("missing");
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("powershell")));
        value[QStringLiteral("identityKey")] = QString(32, u'a');
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("wsl-bash")));
        value = profile(); value[QStringLiteral("agentForwarding")] = true;
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("bash")));
        value = profile(); value[QStringLiteral("trustedX11")] = true;
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("bash")));
        for (const auto& key : {QStringLiteral("~/$(id)"), QStringLiteral("~/%h"), QStringLiteral("~/key\nwhoami")})
            QVERIFY(!NativeSshCommand::command(withKey(key), QStringLiteral("powershell")));
    }
    void quotesLiteralKeyPathsAndResolvesHomeInTheTargetShell() {
        const auto ps = NativeSshCommand::command(withKey(QStringLiteral("~/a'b`;key")), QStringLiteral("powershell"));
        QVERIFY2(ps, qPrintable(ps ? QString{} : ps.error()));
        QVERIFY(ps->contains(QStringLiteral("(Join-Path $HOME 'a''b`;key')")));
        QVERIFY(ps->contains(QStringLiteral("Test-Path -LiteralPath")));
        QVERIFY(ps->contains(QStringLiteral("else { throw 'The saved SSH key is missing; connection cancelled.' }")));
        const auto bash = NativeSshCommand::command(withKey(QStringLiteral("~/a'b`;key")), QStringLiteral("bash"));
        QVERIFY(bash);
        QVERIFY(bash->startsWith(QStringLiteral("( bitshell_native_key=\"$HOME\"/'a'\\''b`;key'; ")));
        QVERIFY(bash->contains(QStringLiteral("[ -f \"$bitshell_native_key\" ] && [ -r \"$bitshell_native_key\" ]")));
        QVERIFY(bash->contains(QStringLiteral("'-i' \"$bitshell_native_key\"")));
        QVERIFY(!bash->contains(QStringLiteral("mktemp")));
    }
    void wslCopiesWindowsKeysPrivatelyAndCleansUp() {
#ifdef Q_OS_WIN
        const QString key = QStringLiteral("C:\\Users\\o'brien\\key ` literal");
        const auto result = NativeSshCommand::command(withKey(key), QStringLiteral("wsl-bash"));
        QVERIFY2(result, qPrintable(result ? QString{} : result.error()));
        QVERIFY(result->startsWith(QStringLiteral("( umask 077; ")));
        QVERIFY(result->contains(QStringLiteral("command wslpath -a -u 'C:\\Users\\o'\\''brien\\key ` literal'")));
        QVERIFY(result->contains(QStringLiteral("command mktemp -d /tmp/8bitshell-key.XXXXXXXX")));
        QVERIFY(result->contains(QStringLiteral("command chmod 600 \"$bitshell_key_dir/key\" || exit")));
        QVERIFY(result->contains(QStringLiteral("trap 'command rm -f -- \"$bitshell_key_dir/key\"; command rmdir -- \"$bitshell_key_dir\"' EXIT")));
        QVERIFY(result->endsWith(QStringLiteral("'--' 'db.example.test' )")));
        QVERIFY(!result->contains(QStringLiteral("ssh.exe")));
        QVERIFY(!NativeSshCommand::command(withKey(key), QStringLiteral("bash")));
#else
        QSKIP("Windows-managed key paths are tested on their source platform.");
#endif
    }
    void cyberArkCannotFallBackToDirectOrUnapprovedGateway() {
        auto value = profile();
        value[QStringLiteral("port")] = 22;
        value[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")},
                                                        {QStringLiteral("route"), QStringLiteral("work")}};
        const auto resolved = ConnectionRouting::prepare(value, {{QStringLiteral("work"), route()}}, {}, QStringLiteral("EMP1"));
        QVERIFY(resolved);
        QVERIFY(!NativeSshCommand::command(*resolved, QStringLiteral("powershell")));
        PersonalProfile personal;
        QVERIFY(personal.approveRoute(ConnectionRouting::fingerprint(route())));
        const auto command = NativeSshCommand::command(*resolved, QStringLiteral("powershell"));
        QVERIFY2(command, qPrintable(command ? QString{} : command.error()));
        QVERIFY(command->contains(QStringLiteral("'-p' '2200'")));
        QVERIFY(command->endsWith(QStringLiteral("'-l' 'EMP1@EMP1SYS%corp.example.test@db.example.test' '--' 'psmp.example.test'")));
        QVERIFY(personal.clearRouteApprovals());
        QVERIFY(!NativeSshCommand::command(*resolved, QStringLiteral("wsl-bash")));
    }
    void passwordModeBypassesKeysAndRestrictsAuthentication() {
        auto value = profile();
        value[QStringLiteral("authenticationMode")] = QStringLiteral("password");
        value[QStringLiteral("identityKey")] = QStringLiteral("deleted-key-reference");
        value[QStringLiteral("identityFile")] = temporary.filePath(QStringLiteral("missing-key"));
        for (const auto& kind : {QStringLiteral("powershell"), QStringLiteral("bash"), QStringLiteral("wsl-bash")}) {
            const auto command = NativeSshCommand::command(value, kind);
            QVERIFY2(command, qPrintable(command ? QString{} : command.error()));
            QVERIFY(command->contains(QStringLiteral("'-oPreferredAuthentications=password'")));
            QVERIFY(command->contains(QStringLiteral("'-oPubkeyAuthentication=no'")));
            QVERIFY(command->contains(QStringLiteral("'-oKbdInteractiveAuthentication=no'")));
            QVERIFY(!command->contains(QStringLiteral("missing-key")));
            QVERIFY(!command->contains(QStringLiteral("deleted-key-reference")));
            QVERIFY(!command->contains(QStringLiteral("ASKPASS"))); // Missing saved secret uses the normal terminal prompt.
        }
        for (const auto transport : {SshSecurity::Transport::Terminal, SshSecurity::Transport::Sftp, SshSecurity::Transport::Monitor}) {
            const auto command = SshSecurity::command(value, transport);
            QVERIFY2(command, qPrintable(command ? QString{} : command.error()));
            QVERIFY(command->arguments.contains(QStringLiteral("-oPubkeyAuthentication=no")));
            QVERIFY(command->arguments.contains(QStringLiteral("-oKbdInteractiveAuthentication=no")));
            QVERIFY(command->arguments.contains(QStringLiteral("-oNumberOfPasswordPrompts=1")));
            QVERIFY(!command->arguments.contains(QStringLiteral("-i")));
            if (transport != SshSecurity::Transport::Terminal) {
                QVERIFY(command->arguments.contains(QStringLiteral("-oBatchMode=no")));
                QVERIFY(!command->arguments.contains(QStringLiteral("-oBatchMode=yes")));
            }
            if (transport == SshSecurity::Transport::Sftp)
                QVERIFY(command->arguments.indexOf(QStringLiteral("-oBatchMode=no")) < command->arguments.indexOf(QStringLiteral("-b")));
        }
    }
    void savedPasswordEnvironmentIsScopedAndCannotFollowRoutes() {
        auto value = profile();
        value[QStringLiteral("authenticationMode")] = QStringLiteral("password");
        QProcessEnvironment helper;
        helper.insert(QStringLiteral("SSH_ASKPASS"), temporary.filePath(QStringLiteral("helper's path/askpass")));
        helper.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        helper.insert(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET"), QStringLiteral("fixture-socket"));
        helper.insert(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"), QStringLiteral("fixture-token"));
        helper.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        const auto ps = NativeSshCommand::command(value, QStringLiteral("powershell"), helper);
        QVERIFY2(ps, qPrintable(ps ? QString{} : ps.error()));
        QVERIFY(ps->contains(QStringLiteral("helper''s path/askpass")));
        QVERIFY(ps->startsWith(QStringLiteral("& { $bitshellAskpassPrevious=@{};")));
        QVERIFY(ps->contains(QStringLiteral("finally { foreach ($bitshellAskpassName")));
        QVERIFY(ps->contains(QStringLiteral("$bitshellAskpassPrevious[$bitshellAskpassName],'Process'")));
        const auto bash = NativeSshCommand::command(value, QStringLiteral("bash"), helper);
        QVERIFY(bash);
        QVERIFY(bash->startsWith(QStringLiteral("( export ")));
        QVERIFY(bash->contains(QStringLiteral("helper'\\''s path/askpass")));
        QVERIFY(bash->endsWith(QStringLiteral("'--' 'db.example.test' )")));
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("wsl-bash"), helper));
        QVERIFY(!NativeSshCommand::command(profile(), QStringLiteral("powershell"), helper));
        value[QStringLiteral("proxyJump")] = QStringLiteral("jump.example.test");
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("powershell"), helper));
        value.remove(QStringLiteral("proxyJump"));
        auto invalid = helper;
        invalid.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral("arbitrary"));
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("bash"), invalid));
        invalid = helper;
        invalid.insert(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"), QStringLiteral("token\ncommand"));
        QVERIFY(!NativeSshCommand::command(value, QStringLiteral("powershell"), invalid));
    }
    void passwordEnvironmentRestoresAfterNativePowerShellInvocation() {
#ifdef Q_OS_WIN
        const QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        if (powershell.isEmpty() || !QFileInfo(Platform::sshExecutable()).isExecutable())
            QSKIP("PowerShell and OpenSSH are needed for environment-isolation integration.");
        auto value = profile();
        value[QStringLiteral("authenticationMode")] = QStringLiteral("password");
        QProcessEnvironment helper;
        helper.insert(QStringLiteral("SSH_ASKPASS"), temporary.filePath(QStringLiteral("unused helper's path.exe")));
        helper.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
        helper.insert(QStringLiteral("SSH_ASKPASS_PROMPT"), QString{});
        helper.insert(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET"), QStringLiteral("fixture-socket"));
        helper.insert(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"), QStringLiteral("fixture-token"));
        helper.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        const auto command = NativeSshCommand::command(value, QStringLiteral("powershell"), helper);
        QVERIFY(command);
        QString evaluation = *command;
        const auto options = evaluation.indexOf(QStringLiteral(" '-F' "));
        QVERIFY(options >= 0);
        evaluation.insert(options, QStringLiteral(" '-G'"));
        evaluation = QStringLiteral("$env:SSH_ASKPASS='parent-helper'; $env:LC_ALL='parent-locale'; $env:SSH_ASKPASS_PROMPT='confirm'; ") + evaluation
            + QStringLiteral("; if ($env:SSH_ASKPASS -ne 'parent-helper' -or $env:LC_ALL -ne 'parent-locale' "
                "-or $env:SSH_ASKPASS_PROMPT -ne 'confirm' -or $null -ne $env:BIT8SHELL_ASKPASS_TOKEN -or $null -ne $env:BIT8SHELL_ASKPASS_SOCKET) { throw 'Environment escaped the SSH invocation' }; 'ENV_RESTORED'");
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.remove(QStringLiteral("BIT8SHELL_ASKPASS_TOKEN"));
        environment.remove(QStringLiteral("BIT8SHELL_ASKPASS_SOCKET"));
        process.setProcessEnvironment(environment);
        process.start(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-Command"), evaluation});
        QVERIFY(process.waitForFinished(10000));
        const QByteArray output = process.readAllStandardOutput().replace("\r\n", "\n");
        const QByteArray error = process.readAllStandardError();
        QVERIFY2(process.exitCode() == 0, error.constData());
        QVERIFY(output.contains("preferredauthentications password\n"));
        QVERIFY(output.contains("pubkeyauthentication false\n") || output.contains("pubkeyauthentication no\n"));
        QVERIFY(output.contains("kbdinteractiveauthentication no\n"));
        QVERIFY(output.contains("ENV_RESTORED"));
#else
        QSKIP("Native PowerShell environment integration runs on Windows.");
#endif
    }
    void powershellQuotingRoundTripsThroughNativeSshWithoutConnecting_data() {
        QTest::addColumn<bool>("clearPath");
        QTest::newRow("shell-native-client") << false;
        QTest::newRow("packaged-client-fallback-with-no-path") << true;
    }
    void powershellQuotingRoundTripsThroughNativeSshWithoutConnecting() {
#ifdef Q_OS_WIN
        QFETCH(bool, clearPath);
        const QString powershell = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        const QString ssh = QStandardPaths::findExecutable(QStringLiteral("ssh.exe"));
        if (powershell.isEmpty() || (!clearPath && ssh.isEmpty())
            || (clearPath && !QFileInfo(Platform::sshExecutable()).isExecutable()))
            QSKIP("PowerShell and a matching native or packaged OpenSSH client are needed for argv integration.");
        const QString key = temporary.filePath(QStringLiteral("o'brien `literal;key"));
        QFile file(key); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("not a real key\n"); file.close();
        auto value = withKey(key);
        const auto command = NativeSshCommand::command(value, QStringLiteral("powershell"));
        QVERIFY2(command, qPrintable(command ? QString{} : command.error()));
        QString evaluation = *command;
        // -G only evaluates configuration: no DNS lookup, connection or key read.
        const auto options = evaluation.indexOf(QStringLiteral(" '-F' "));
        QVERIFY(options >= 0);
        evaluation.insert(options, QStringLiteral(" '-G'"));
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        if (clearPath) environment.insert(QStringLiteral("PATH"), QString{});
        process.setProcessEnvironment(environment);
        process.start(powershell, {QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                                  QStringLiteral("-Command"), evaluation});
        QVERIFY(process.waitForFinished(10000));
        const QByteArray output = process.readAllStandardOutput().replace("\r\n", "\n");
        const QByteArray error = process.readAllStandardError();
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, error.constData());
        QVERIFY(output.contains("hostname db.example.test\n"));
        QVERIFY(output.contains("user opc\n"));
        QVERIFY(output.contains("port 2222\n"));
        QVERIFY(output.contains("stricthostkeychecking ask\n"));
        QVERIFY2(output.contains("identityfile " + key.toUtf8() + '\n'), output.constData());
#else
        QSKIP("Native PowerShell argv integration runs on Windows.");
#endif
    }
};

QTEST_MAIN(NativeSshCommandTest)
#include "NativeSshCommandTest.moc"
