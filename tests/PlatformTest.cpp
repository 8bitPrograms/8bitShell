#include <QtTest>
#include <bitshell/core/Platform.h>
using bitshell::core::Platform;
class PlatformTest : public QObject {
    Q_OBJECT
private slots:
    void windowsQuoting() {
        QCOMPARE(Platform::windowsCommandLine(QStringLiteral("C:/Windows/System32/cmd.exe"), {QStringLiteral("/D")}),
                 QStringLiteral("C:/Windows/System32/cmd.exe /D"));
        QCOMPARE(Platform::windowsCommandLine(QStringLiteral("C:/Windows/System32/cmd.exe"),
            {QStringLiteral("/D"), QStringLiteral("/K"), QStringLiteral("/D")}),
            QStringLiteral("C:/Windows/System32/cmd.exe /D /K /D"));
        QCOMPARE(Platform::windowsCommandLine(QStringLiteral("C:/Windows/System32/wsl.exe"),
            {QStringLiteral("--distribution"), QStringLiteral("Fedora Linux"), QStringLiteral("--cd"), QStringLiteral("/tmp")}),
            QStringLiteral("C:/Windows/System32/wsl.exe --distribution \"Fedora Linux\" --cd /tmp"));
        QCOMPARE(Platform::windowsCommandLine(QStringLiteral("C:/Program Files/app.exe"),
            {QStringLiteral(""), QStringLiteral("a b"), QStringLiteral("a\"b"), QStringLiteral("C:\\folder\\"),
             QStringLiteral("EMP@EMPSYS%corp@db&echo"), QStringLiteral("Türkçe🫐")}),
            QStringLiteral("\"C:/Program Files/app.exe\" \"\" \"a b\" \"a\\\"b\" C:\\folder\\ EMP@EMPSYS%corp@db&echo Türkçe🫐"));
        QVERIFY(Platform::windowsCommandLine({}, {}).isEmpty());
        QVERIFY(Platform::windowsCommandLine(QStringLiteral("x.exe"), {QString(QChar::Null)}).isEmpty());
        QVERIFY(Platform::windowsCommandLine(QStringLiteral("x.exe"), {QString(32767, u'x')}).isEmpty());
        QVERIFY(Platform::windowsCommandLine(QString(32767, u'x'), {}).isEmpty());
        QStringList tooMany; for (int n = 0; n < 129; ++n) tooMany << QStringLiteral("x");
        QVERIFY(Platform::windowsCommandLine(QStringLiteral("x.exe"), tooMany).isEmpty());
    }
    void persistentHistoryUsesExistingPowerShellFile() {
#ifdef Q_OS_WIN
        const auto plain = Platform::persistentHistoryArguments(
            QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"), {QStringLiteral("-NoLogo")});
        QCOMPARE(plain.mid(0, 3), QStringList({QStringLiteral("-NoLogo"), QStringLiteral("-NoExit"), QStringLiteral("-Command")}));
        QVERIFY(plain.last().contains(QStringLiteral("SaveAtExit")));
        QVERIFY(plain.last().contains(QStringLiteral("SaveIncrementally")));
        QVERIFY(!plain.last().contains(QStringLiteral("HistorySavePath")));
        const QStringList script{QStringLiteral("-NoLogo"), QStringLiteral("-File"), QStringLiteral("C:/work/run.ps1")};
        QCOMPARE(Platform::persistentHistoryArguments(QStringLiteral("C:/Program Files/PowerShell/7/pwsh.exe"), script), script);
        QCOMPARE(Platform::persistentHistoryArguments(QStringLiteral("C:/Windows/System32/cmd.exe"), {QStringLiteral("/D")}),
                 QStringList({QStringLiteral("/D")}));
#else
        QCOMPARE(Platform::persistentHistoryArguments(QStringLiteral("/bin/bash"), {QStringLiteral("-l")}),
                 QStringList({QStringLiteral("-l")}));
#endif
    }
    void windowsNames() {
        for (const auto& name : {"", ".", "..", "CON", "nul.txt", "Aux.LOG", "COM1", "LPT9.txt", "CONIN$", "CONOUT$",
                "file:stream", "a/b", "a\\b", "space ", "dot.", "?", "*", "a\n"})
            QVERIFY2(!Platform::windowsFileName(QString::fromUtf8(name)), name);
        QVERIFY(!Platform::windowsFileName(QStringLiteral("COM¹.log")));
        for (const auto& name : {QStringLiteral("file.yaml"), QStringLiteral("Türkçe 🫐.txt"), QStringLiteral("COM10"),
                 QStringLiteral("normal folder"), QStringLiteral("%a&b")})
            QVERIFY2(Platform::windowsFileName(name), qPrintable(name));
    }
    void promptIntegrationIsSessionScopedAndPreservesExplicitCommands() {
        const QString token = QStringLiteral("test-0123456789abcdef-session");
#ifdef Q_OS_WIN
        const QString program = QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe");
        const auto integrated = Platform::persistentHistoryArguments(program, {QStringLiteral("-NoLogo")}, token);
        QVERIFY(integrated.last().contains(QStringLiteral("PSConsoleHostReadLine")));
        QVERIFY(integrated.last().contains(QStringLiteral("777;8bitshell;") + token + QStringLiteral(";powershell")));
        for (const auto* phase : {"A", "B", "C"})
            QVERIFY(integrated.last().contains(QStringLiteral("133;") + QString::fromLatin1(phase) + u';' + token));
        QVERIFY(integrated.last().contains(QStringLiteral("finally")));
        QVERIFY(!integrated.last().contains(QStringLiteral("Set-Content")));
        QVERIFY(!integrated.last().contains(QStringLiteral("AddToHistoryHandler")));
        for (const auto* option : {"-Command", "-co", "-CommandWithArgs", "-File", "-f", "-EncodedCommand", "-ec", "-NonInteractive"}) {
            const QStringList explicitArgs{QString::fromLatin1(option), QStringLiteral("user supplied input")};
            QCOMPARE(Platform::persistentHistoryArguments(program, explicitArgs, token), explicitArgs);
        }
        const auto malformed = Platform::persistentHistoryArguments(program, {}, QStringLiteral("invalid;token\n"));
        QVERIFY(!malformed.last().contains(QStringLiteral("777;8bitshell")));
        const QStringList wslArgs{QStringLiteral("--distribution"), QStringLiteral("Test Distro"),
                                 QStringLiteral("--cd"), QStringLiteral("/home/test/projects with spaces")};
        const auto wsl = Platform::persistentHistoryArguments(QStringLiteral("C:/Windows/System32/wsl.exe"), wslArgs, token);
        QCOMPARE(wsl.mid(0, 4), wslArgs);
        QCOMPARE(wsl.mid(4, 3), QStringList({QStringLiteral("--exec"), QStringLiteral("/bin/sh"), QStringLiteral("-c")}));
        QVERIFY(wsl.last().contains(QStringLiteral("wsl-bash")));
        QVERIFY(wsl.last().contains(QStringLiteral("__8bit_shell")));
        QVERIFY(wsl.last().contains(QStringLiteral(".bash_profile")));
        QVERIFY(!Platform::windowsCommandLine(QStringLiteral("C:/Windows/System32/wsl.exe"), wsl).isEmpty());
        auto explicitWsl = wslArgs;
        explicitWsl << QStringLiteral("--exec") << QStringLiteral("zsh");
        QCOMPARE(Platform::persistentHistoryArguments(QStringLiteral("C:/Windows/System32/wsl.exe"), explicitWsl, token), explicitWsl);
#else
        const auto integrated = Platform::persistentHistoryArguments(QStringLiteral("/bin/bash"), {QStringLiteral("-l")}, token);
        QCOMPARE(integrated.size(), 3);
        QCOMPARE(integrated.first(), QStringLiteral("-c"));
        QCOMPARE(integrated.last(), QStringLiteral("/bin/bash"));
        QVERIFY(integrated.at(1).contains(QStringLiteral("777;8bitshell;") + token + QStringLiteral(";bash")));
        QVERIFY(integrated.at(1).contains(QStringLiteral("PS0")));
        QVERIFY(integrated.at(1).contains(QStringLiteral(".bash_profile")));
        QVERIFY(integrated.at(1).contains(QStringLiteral("export -n PS0 PS1 PROMPT_COMMAND")));
        const QStringList explicitArgs{QStringLiteral("-c"), QStringLiteral("printf untouched")};
        QCOMPARE(Platform::persistentHistoryArguments(QStringLiteral("/bin/bash"), explicitArgs, token), explicitArgs);
        QCOMPARE(Platform::persistentHistoryArguments(QStringLiteral("/bin/zsh"), {QStringLiteral("-l")}, token),
                 QStringList({QStringLiteral("-l")}));
#endif
    }
};
QTEST_APPLESS_MAIN(PlatformTest)
#include "PlatformTest.moc"
