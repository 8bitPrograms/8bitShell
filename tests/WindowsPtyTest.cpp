#include <QtTest>
#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QDir>
#include <windows.h>
#include <bitshell/terminal/PtyProcess.h>
#include <bitshell/terminal/TerminalView.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/SessionLogger.h>
#include "TerminalTestAccess.h"
using bitshell::terminal::PtyProcess;
using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;
class WindowsPtyTest : public QObject {
    Q_OBJECT
    QString fixture() const { return QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe"); }
private slots:
    void independentWorkingDirectoriesAndCommandPrompt() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto first = root.filePath(QStringLiteral("folder one ç"));
        const auto second = root.filePath(QStringLiteral("folder two"));
        QVERIFY(QDir().mkpath(first)); QVERIFY(QDir().mkpath(second));
        const auto original = QDir::currentPath();
        PtyProcess a, b; QByteArray outputA, outputB; QString error;
        connect(&a, &PtyProcess::dataReceived, this, [&](const QByteArray& bytes) { outputA += bytes; });
        connect(&b, &PtyProcess::dataReceived, this, [&](const QByteArray& bytes) { outputB += bytes; });
        QVERIFY2(a.start(fixture(), {QStringLiteral("profile")}, 1000, 30, &error, first), qPrintable(error));
        QVERIFY2(b.start(fixture(), {QStringLiteral("profile")}, 1000, 30, &error, second), qPrintable(error));
        const auto expected = [](const QString& path) {
            QByteArray result("CWDHEX=");
            for (const auto ch : QDir::toNativeSeparators(path)) result += QByteArray::number(ch.unicode(), 16).rightJustified(4, '0');
            return result;
        };
        const auto plain = [](const QByteArray& bytes) { bitshell::core::AnsiStripper stripper; return stripper.filter(bytes); };
        QTRY_VERIFY2_WITH_TIMEOUT(plain(outputA).contains(expected(first)), outputA.constData(), 10000);
        QTRY_VERIFY2_WITH_TIMEOUT(plain(outputB).contains(expected(second)), outputB.constData(), 10000);
        QCOMPARE(QDir::currentPath(), original);
        a.stop(); b.stop();
        QVERIFY(!a.start(fixture(), {}, 80, 24, &error, root.filePath(QStringLiteral("absent"))));
        outputA.clear();
        QVERIFY2(a.start(bitshell::core::Platform::windowsSystemDirectory() + QStringLiteral("/cmd.exe"),
            {QStringLiteral("/D")}, 160, 30, &error, first), qPrintable(error));
        a.write("echo CMD_PROFILE_^OK\r");
        QTRY_VERIFY2_WITH_TIMEOUT(outputA.contains("CMD_PROFILE_OK"), outputA.constData(), 10000);
        QVERIFY(a.isRunning());
    }
    void controlInput_data() {
        QTest::addColumn<QByteArray>("input");
        QTest::newRow("backspace-bs") << QByteArray(1, '\x08');
        QTest::newRow("backspace-del") << QByteArray(1, '\x7f');
        QTest::newRow("return") << QByteArray(1, '\r');
        QTest::newRow("up-arrow") << QByteArray("\x1b[A");
        QTest::newRow("utf8") << QStringLiteral("ç").toUtf8();
    }
    void controlInput() {
        QFETCH(QByteArray, input);
        PtyProcess p; QByteArray output;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& b) { output += b; });
        QSignalSpy exit(&p, &PtyProcess::finished); QString error;
        QVERIFY2(p.start(fixture(), {QStringLiteral("raw")}, 80, 24, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("RAW_READY"), 10000);
        p.write(input);
        QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 10000);
        bitshell::core::AnsiStripper stripper;
        const auto plain = stripper.filter(output);
        QVERIFY2(plain.contains("INPUT=" + input.toHex()), plain.constData());
    }
    void literalUnicodeArguments() {
        PtyProcess p; QByteArray output;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& b) { output += b; });
        QSignalSpy exit(&p, &PtyProcess::finished);
        const QStringList values{QString{}, QStringLiteral("a b"), QStringLiteral("quote\"slash\\"),
            QStringLiteral("EMP@SYS%corp@target&calc.exe"), QStringLiteral("Türkçe 🫐")};
        auto args = values; args.prepend(QStringLiteral("args")); QString error;
        QVERIFY2(p.start(fixture(), args, 200, 30, &error), qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 10000);
        QCOMPARE(exit.first().first().toInt(), 0);
        bitshell::core::AnsiStripper stripper;
        const auto plain = stripper.filter(output);
        for (int n = 0; n < values.size(); ++n) {
            QByteArray expected = "ARG" + QByteArray::number(n) + '=';
            for (const auto c : values[n]) expected += QByteArray::number(c.unicode(), 16).rightJustified(4, '0');
            QVERIFY2(plain.contains(expected), plain.constData());
        }
    }
    void inputResizeAndExit() {
        PtyProcess p; QByteArray output;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& b) { output += b; });
        QSignalSpy exit(&p, &PtyProcess::finished); QString error;
        QVERIFY2(p.start(fixture(), {QStringLiteral("raw")}, 80, 24, &error), qPrintable(error));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("RAW_READY"), 10000);
        p.write("abc");
        QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 10000);
        QVERIFY2(output.contains("INPUT=616263"), output.constData());
        output.clear(); exit.clear();
        QVERIFY(p.start(fixture(), {QStringLiteral("resize")}, 80, 24, &error));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("SIZE_READY"), 10000);
        p.resize(132, 40);
        QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 10000);
        QVERIFY2(output.contains("SIZE=132x40"), output.constData());
    }
    void controlCLeavesPowerShellUsable_data() {
        QTest::addColumn<QString>("mode");
        QTest::addColumn<bool>("controlCCopy");
        QTest::newRow("raw-vt") << QStringLiteral("interrupt-raw") << false;
        QTest::newRow("cooked-interrupt") << QStringLiteral("interrupt-cooked") << false;
        QTest::newRow("win32-events") << QStringLiteral("interrupt-win32") << false;
        QTest::newRow("ctrl-c-copy-without-selection") << QStringLiteral("interrupt-raw") << true;
    }
    void controlCLeavesPowerShellUsable() {
        QFETCH(QString, mode);
        QFETCH(bool, controlCCopy);
        TerminalView terminal;
        if (controlCCopy) terminal.setKeyBindings({{QStringLiteral("copy"), QStringLiteral("Ctrl+C")}});
        terminal.setWidth(1000); terminal.setHeight(500); terminal.setLogEnabled(false);
        QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QString program = bitshell::core::Platform::windowsSystemDirectory()
            + QStringLiteral("/WindowsPowerShell/v1.0/powershell.exe");
        QString executable = fixture(); executable.replace(QLatin1Char('\''), QStringLiteral("''"));
        const QString script = QStringLiteral("Import-Module PSReadLine -ErrorAction SilentlyContinue; "
            "Set-PSReadLineOption -HistorySaveStyle SaveNothing; function prompt { 'PS_TEST_READY> ' }; & '%1' %2").arg(executable, mode);
        QVERIFY(terminal.startLocalCommand({program, QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile"),
            QStringLiteral("-NoExit"), QStringLiteral("-Command"), script}));
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("INTERRUPT_READY"), output.constData(), 15000);
        QKeyEvent cancel(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QString(QChar(3)));
        QCoreApplication::sendEvent(&terminal, &cancel);
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier, QString(QChar(3)));
        QCoreApplication::sendEvent(&terminal, &release);
        QKeyEvent controlReleased(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
        QCoreApplication::sendEvent(&terminal, &controlReleased);
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("PS_TEST_READY>"), output.right(1200).constData(), 15000);
        QKeyEvent text(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier,
            QStringLiteral("Write-Output ('CTRL_'+'C_RECOVERED')"));
        QCoreApplication::sendEvent(&terminal, &text);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QCoreApplication::sendEvent(&terminal, &enter);
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("CTRL_C_RECOVERED"), output.right(1600).constData(), 15000);
        QVERIFY(terminal.isRunning());
        TerminalTestAccess::stopFixture(terminal);
    }
    void noHandleInheritance() {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        const HANDLE event = CreateEventW(&sa, TRUE, FALSE, nullptr); QVERIFY(event);
        PtyProcess p; QByteArray output;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& b) { output += b; });
        QSignalSpy exit(&p, &PtyProcess::finished); QString error;
        QVERIFY(p.start(fixture(), {QStringLiteral("inherit"), QString::number(quintptr(event))}, 80, 24, &error));
        QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 10000);
        CloseHandle(event);
        QVERIFY2(output.contains("INHERITED=0"), qPrintable(QStringLiteral("Child exit %1; output: %2")
            .arg(exit.first().first().toInt()).arg(QString::fromUtf8(output))));
    }
    void stopIsBounded() {
        PtyProcess p; QByteArray output; QString error;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(p.start(fixture(), {QStringLiteral("sleep")}, 80, 24, &error));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("SLEEP_READY"), 10000);
        const HANDLE child = OpenProcess(SYNCHRONIZE, FALSE, DWORD(p.processId())); QVERIFY(child);
        QVERIFY(!p.canWrite(1024*1024+1));
        QElapsedTimer timer; timer.start(); p.stop();
        QVERIFY2(timer.elapsed() < 500, "Closing a session blocked the UI");
        QTRY_COMPARE_WITH_TIMEOUT(WaitForSingleObject(child, 0), DWORD(WAIT_OBJECT_0), 5000);
        CloseHandle(child);
        QVERIFY(!p.isRunning());
        QVERIFY(!p.start(QStringLiteral("cmd.exe"), {}, 80, 24, &error));
    }
    void shortLivedOutputCompletesBeforeFinished() {
        PtyProcess p; QByteArray output;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QSignalSpy exit(&p, &PtyProcess::finished);
        QString error;
        for (int round = 0; round < 12; ++round) {
            output.clear(); exit.clear();
            QVERIFY2(p.start(fixture(), {QStringLiteral("args"), QStringLiteral("ok")}, 80, 24, &error), qPrintable(error));
            QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 10000);
            QCOMPARE(exit.first().first().toInt(), 0);
            bitshell::core::AnsiStripper stripper;
            QVERIFY2(stripper.filter(output).contains("ARG0=006f006b"), output.constData());
        }
    }
    void bulkCopyAndPaste() {
        TerminalView terminal;
        terminal.setWidth(800);
        terminal.setHeight(400);
        terminal.setLogEnabled(false);
        QStringList lines;
        QByteArray screen;
        for (int index = 0; index < 80; ++index) {
            const auto line = QStringLiteral("BULK-%1-payload").arg(index, 3, 10, QLatin1Char('0'));
            lines << line;
            screen += line.toUtf8() + "\r\n";
        }
        TerminalTestAccess::feed(terminal, screen);
        terminal.selectAll();
        QVERIFY(terminal.hasSelection());
        terminal.copySelection();
        const auto copied = QGuiApplication::clipboard()->text();
        for (const auto& line : lines)
            QVERIFY2(copied.contains(line), qPrintable(copied.left(180) + QStringLiteral(" ... missing ") + line));

        QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(terminal.startLocalCommand({fixture(), QStringLiteral("broadcast")}));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
        QString bulk;
        for (int index = 0; index < 40; ++index)
            bulk += QStringLiteral("PASTE-%1\n").arg(index, 2, 10, QLatin1Char('0'));
        terminal.pasteText(bulk);
        terminal.confirmPaste(true);
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("ACK:PASTE-00"), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("ACK:PASTE-39"), 10000);
        QVERIFY2(output.contains("PASTE-15"), output.right(400).constData());
        TerminalTestAccess::stopFixture(terminal);
    }
    void outputPressureAndRestart() {
        PtyProcess p; QByteArray tail; qsizetype total = 0;
        connect(&p, &PtyProcess::dataReceived, this, [&](const QByteArray& b) { total += b.size(); tail = (tail + b).right(4096); });
        QSignalSpy exit(&p, &PtyProcess::finished); QString error;
        QVERIFY(p.start(fixture(), {QStringLiteral("flood")}, 200, 30, &error));
        QTest::qSleep(300); // Deliberately let the bounded receive queue fill.
        QTRY_COMPARE_WITH_TIMEOUT(exit.size(), 1, 30000);
        QVERIFY(total > 1000000); QVERIFY2(tail.contains("FLOOD_END"), tail.constData());
        for (int n = 0; n < 6; ++n) {
            QVERIFY(p.start(fixture(), {QStringLiteral("sleep")}, 80, 24, &error));
            p.stop();
        }
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    WindowsPtyTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "WindowsPtyTest.moc"
