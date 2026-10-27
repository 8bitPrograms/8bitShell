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
};
QTEST_APPLESS_MAIN(PlatformTest)
#include "PlatformTest.moc"
