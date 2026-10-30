#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QClipboard>
#include <QGuiApplication>

#include <bitshell/terminal/TerminalView.h>

using bitshell::terminal::TerminalView;

class TerminalViewTest final : public QObject {
    Q_OBJECT

private slots:
    void quickConnectBlocksArbitraryOptions();
    void acceptsLiveHighlightRules();
    void supportsBackspaceModesAndTerminalPalettes();
    void sendsConfiguredBackspaceByteThroughPty();
    void macroBytesClipboardSelectionAndCustomKeys();
    void hostileTerminalOutputCannotTouchClipboard();
    void x11NeedsExplicitConfirmation();
    void localCommandPreservesLiteralArguments();
};

void TerminalViewTest::quickConnectBlocksArbitraryOptions() {
    TerminalView terminal;
    terminal.setWidth(900);
    terminal.setHeight(600);
    terminal.setLogEnabled(false);

    QVERIFY(terminal.startLocalShell());
    terminal.connectToTarget(QStringLiteral("ssh -oProxyCommand=touch /tmp/must-not-run example.test"));
    QVERIFY(terminal.statusText().contains(QStringLiteral("Security policy")));
    QVERIFY(terminal.isRunning());
    QCOMPARE(terminal.activeTarget(), QStringLiteral("Local"));
}

void TerminalViewTest::acceptsLiveHighlightRules() {
    TerminalView terminal;
    const QVariantList rules{
        QVariantMap{
            {QStringLiteral("id"), QStringLiteral("ticket")},
            {QStringLiteral("name"), QStringLiteral("Ticket IDs")},
            {QStringLiteral("pattern"), QStringLiteral(R"(\bINC-[0-9]+\b)")},
            {QStringLiteral("foreground"), QStringLiteral("#ad8ee6")},
            {QStringLiteral("background"), QString{}},
            {QStringLiteral("bold"), true},
            {QStringLiteral("underline"), false},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("caseSensitive"), true},
            {QStringLiteral("priority"), 50},
        },
    };
    terminal.setHighlightRules(rules);
    QCOMPARE(terminal.highlightRules(), rules);
    terminal.setHighlightRules({});
    QVERIFY(terminal.highlightRules().isEmpty());

    terminal.setLogDirectoryTemplate(QStringLiteral("{host}/{date}"));
    terminal.setLogFileNameTemplate(QStringLiteral("{session}.log"));
    terminal.setLogTimestamps(true);
    terminal.setLogAppend(false);
    terminal.setLogRotationMegabytes(42);
    terminal.setLogRetainedFiles(7);
    QCOMPARE(terminal.logDirectoryTemplate(), QStringLiteral("{host}/{date}"));
    QCOMPARE(terminal.logFileNameTemplate(), QStringLiteral("{session}.log"));
    QVERIFY(terminal.logTimestamps());
    QVERIFY(!terminal.logAppend());
    QCOMPARE(terminal.logRotationMegabytes(), 42);
    QCOMPARE(terminal.logRetainedFiles(), 7);
}

void TerminalViewTest::supportsBackspaceModesAndTerminalPalettes() {
    QCOMPARE(TerminalView::backspaceSequence(QStringLiteral("del")),
             QByteArray(1, '\x7f'));
    QCOMPARE(TerminalView::backspaceSequence(QStringLiteral("bs")),
             QByteArray(1, '\x08'));
    QCOMPARE(TerminalView::backspaceSequence(QStringLiteral("bs"), Qt::AltModifier),
             QByteArrayLiteral("\x1b\x08"));

    TerminalView terminal;
    terminal.setBackspaceMode(QStringLiteral("BS"));
    QCOMPARE(terminal.backspaceMode(), QStringLiteral("bs"));
    terminal.setBackspaceMode(QStringLiteral("invalid"));
    QCOMPARE(terminal.backspaceMode(), QStringLiteral("del"));

    const QVariantList ansi{
        QStringLiteral("#000000"), QStringLiteral("#110000"),
        QStringLiteral("#001100"), QStringLiteral("#111100"),
        QStringLiteral("#000011"), QStringLiteral("#110011"),
        QStringLiteral("#001111"), QStringLiteral("#bbbbbb"),
        QStringLiteral("#555555"), QStringLiteral("#ff5555"),
        QStringLiteral("#55ff55"), QStringLiteral("#ffff55"),
        QStringLiteral("#5555ff"), QStringLiteral("#ff55ff"),
        QStringLiteral("#55ffff"), QStringLiteral("#ffffff"),
    };
    terminal.setColorScheme({
        {QStringLiteral("foreground"), QStringLiteral("#dedede")},
        {QStringLiteral("background"), QStringLiteral("#101010")},
        {QStringLiteral("cursor"), QStringLiteral("#abcdef")},
        {QStringLiteral("selection"), QStringLiteral("#223344")},
        {QStringLiteral("ansi"), ansi},
    });
    QCOMPARE(terminal.colorScheme().value(QStringLiteral("foreground")).toString(),
             QStringLiteral("#dedede"));
    QCOMPARE(terminal.colorScheme().value(QStringLiteral("background")).toString(),
             QStringLiteral("#101010"));
    QCOMPARE(terminal.colorScheme().value(QStringLiteral("ansi")).toList().size(), 16);
}

void TerminalViewTest::sendsConfiguredBackspaceByteThroughPty() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString probePath = directory.filePath(QStringLiteral("erase-probe"));
    QFile probe(probePath);
    QVERIFY(probe.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray script = QByteArrayLiteral(
        "#!/bin/sh\n"
        "stty -echo icanon\n"
        "printf 'READY\\n'\n"
        "IFS= read -r value\n"
        "printf 'VALUE:%s\\n' \"$value\"\n");
    QCOMPARE(probe.write(script), script.size());
    probe.close();
    QVERIFY(probe.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                 | QFileDevice::ExeOwner));

    const QByteArray previousShell = qgetenv("SHELL");
    qputenv("SHELL", probePath.toUtf8());
    const auto verifyMode = [](const QString& mode) {
        QQuickWindow window;
        window.setWidth(640);
        window.setHeight(360);
        TerminalView terminal(window.contentItem());
        terminal.setWidth(640);
        terminal.setHeight(360);
        terminal.setLogEnabled(false);
        terminal.setBackspaceMode(mode);
        QSignalSpy outputSpy(&terminal, &TerminalView::rawOutputReceived);
        window.show();
        QVERIFY(terminal.startLocalShell());
        terminal.forceActiveFocus();

        QByteArray output;
        QElapsedTimer timer;
        timer.start();
        while (!output.contains(QByteArrayLiteral("READY")) && timer.elapsed() < 2000) {
            if (outputSpy.isEmpty()) {
                outputSpy.wait(50);
            }
            while (!outputSpy.isEmpty()) {
                output += outputSpy.takeFirst().at(0).toByteArray();
            }
        }
        QVERIFY2(output.contains(QByteArrayLiteral("READY")), output.constData());

        terminal.sendText(QStringLiteral("abc"));
        QTest::keyClick(&window, Qt::Key_Backspace);
        terminal.sendText(QStringLiteral("d\n"));
        timer.restart();
        while (!output.contains(QByteArrayLiteral("VALUE:abd"))
               && timer.elapsed() < 2000) {
            if (outputSpy.isEmpty()) {
                outputSpy.wait(50);
            }
            while (!outputSpy.isEmpty()) {
                output += outputSpy.takeFirst().at(0).toByteArray();
            }
        }
        QVERIFY2(output.contains(QByteArrayLiteral("VALUE:abd")), output.constData());
    };

    verifyMode(QStringLiteral("bs"));
    verifyMode(QStringLiteral("del"));
    if (previousShell.isNull()) {
        qunsetenv("SHELL");
    } else {
        qputenv("SHELL", previousShell);
    }
}

void TerminalViewTest::macroBytesClipboardSelectionAndCustomKeys() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("input-probe"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\nstty -echo raw\nprintf '\\033[?2004hREADY\\r\\n'\ndd bs=1 count=16 2>/dev/null | od -An -tx1\n";
    QCOMPARE(file.write(script), script.size()); file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const QByteArray previous = qgetenv("SHELL");
    qputenv("SHELL", path.toUtf8());
    QQuickWindow window; window.resize(640, 360);
    TerminalView terminal(window.contentItem());
    terminal.setWidth(640); terminal.setHeight(360); terminal.setLogEnabled(false);
    QByteArray output;
    connect(&terminal, &TerminalView::rawOutputReceived, this, [&output](const QByteArray& bytes) { output += bytes; });
    window.show(); QVERIFY(terminal.startLocalShell()); terminal.forceActiveFocus();
    qputenv("SHELL", previous);
    QTRY_VERIFY(output.contains("READY"));
    QSignalSpy confirmations(&terminal, &TerminalView::pasteConfirmationRequested);
    terminal.pasteText(QStringLiteral("unsafe\n"));
    QCOMPARE(confirmations.size(), 1);
    terminal.confirmPaste(false);
    terminal.pasteText(QStringLiteral("\x1b[201~escape\n"));
    QCOMPARE(confirmations.size(), 1);
    QVERIFY(terminal.statusText().contains(QStringLiteral("Paste blocked")));
    terminal.sendMacro(QStringLiteral("abc"), true);
    QCOMPARE(confirmations.size(), 2);
    QTest::qWait(50);
    // GNU od separates bytes with one space, BSD/macOS od pads wider columns.
    QVERIFY(!output.simplified().contains("1b 5b 32 30 30"));
    terminal.confirmPaste(true);
    QTRY_VERIFY(output.simplified().contains("1b 5b 32 30 30 7e 61 62 63 1b 5b 32 30 31 7e 0d"));
    terminal.selectAll();
    QVERIFY(terminal.hasSelection());
    QGuiApplication::clipboard()->setText(QStringLiteral("clipboard sentinel"));
    QTest::keyClick(&window, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("READY")));
    auto bindings = terminal.keyBindings();
    bindings[QStringLiteral("copy")] = QStringLiteral("Ctrl+Insert");
    terminal.setKeyBindings(bindings);
    QGuiApplication::clipboard()->setText(QStringLiteral("clipboard sentinel"));
    QTest::keyClick(&window, Qt::Key_Insert, Qt::ControlModifier);
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("READY")));

    QGuiApplication::clipboard()->setText(QStringLiteral("not automatic"));
    QVERIFY(!terminal.copyOnSelect());
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(12, 9));
    QTest::mouseMove(&window, QPoint(160, 9), 20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(160, 9));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("not automatic"));
    terminal.setCopyOnSelect(true);
    QTest::mousePress(&window, Qt::LeftButton, Qt::NoModifier, QPoint(12, 9));
    QTest::mouseMove(&window, QPoint(160, 9), 20);
    QTest::mouseRelease(&window, Qt::LeftButton, Qt::NoModifier, QPoint(160, 9));
    QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("READY")));

    for (bool insert : {false, true}) {
        QTRY_VERIFY(!terminal.isRunning());
        output.clear();
        qputenv("SHELL", path.toUtf8());
        QVERIFY(terminal.startLocalShell());
        qputenv("SHELL", previous);
        QTRY_VERIFY(output.contains("READY"));
        bindings[QStringLiteral("paste")] = insert ? QStringLiteral("Shift+Insert") : QStringLiteral("Ctrl+Shift+V");
        terminal.setKeyBindings(bindings);
        QGuiApplication::clipboard()->setText(QStringLiteral("abc"));
        terminal.forceActiveFocus();
        QTest::keyClick(&window, insert ? Qt::Key_Insert : Qt::Key_V,
                        insert ? Qt::ShiftModifier : Qt::ControlModifier | Qt::ShiftModifier);
        QTest::keyClick(&window, Qt::Key_Return);
        QTRY_VERIFY(output.simplified().contains("1b 5b 32 30 30 7e 61 62 63 1b 5b 32 30 31 7e 0d"));
    }
}

void TerminalViewTest::hostileTerminalOutputCannotTouchClipboard() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("output-probe"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray script = "#!/bin/sh\nstty -echo\nprintf '\\033]52;c;aW5qZWN0ZWQ=\\007\\033]0;'\n"
        "/usr/bin/head -c 100000 /dev/zero | /usr/bin/tr '\\000' t\n"
        "printf '\\007OUTPUT_READY\\r\\n'\nexec /bin/cat\n";
    QCOMPARE(file.write(script), script.size()); file.close();
    QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    const auto oldShell = qgetenv("SHELL"); qputenv("SHELL", path.toUtf8());
    TerminalView terminal;
    terminal.setWidth(640); terminal.setHeight(360); terminal.setLogEnabled(false);
    QGuiApplication::clipboard()->setText(QStringLiteral("unchanged clipboard"));
    QByteArray output;
    connect(&terminal, &TerminalView::rawOutputReceived, this, [&output](const QByteArray& bytes) { output += bytes; });
    const bool started = terminal.startLocalShell();
    qputenv("SHELL", oldShell);
    QVERIFY(started);
    QTRY_VERIFY_WITH_TIMEOUT(output.contains("OUTPUT_READY"), 4000);
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("unchanged clipboard"));
    QVERIFY(terminal.title().size() <= 4096);
}
void TerminalViewTest::x11NeedsExplicitConfirmation() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    QSignalSpy spy(&terminal, &TerminalView::x11ConfirmationRequested);
    terminal.startSshProfile({{QStringLiteral("host"), QStringLiteral("untrusted.invalid")},
                             {QStringLiteral("forwardX11"), true}});
    QCOMPARE(spy.size(), 1);
    QVERIFY(!terminal.isRunning());
    terminal.confirmX11(false);
    QVERIFY(!terminal.isRunning());
}

void TerminalViewTest::localCommandPreservesLiteralArguments() {
    TerminalView terminal; terminal.setLogEnabled(false);
    terminal.setWidth(800); terminal.setHeight(400);
    QByteArray output;
    connect(&terminal, &TerminalView::rawOutputReceived, this, [&output](const QByteArray& bytes) { output += bytes; });
    QVERIFY(!terminal.startLocalCommand({}));
    QVERIFY(terminal.startLocalCommand({QStringLiteral("/usr/bin/printf"), QStringLiteral("%s\\n"),
        QStringLiteral("argument with spaces"), QStringLiteral("$(not-executed);still-text"), QStringLiteral("--screenshot")}));
    QTRY_VERIFY_WITH_TIMEOUT(output.contains("$(not-executed);still-text"), 4000);
    QVERIFY(output.contains("argument with spaces")); QVERIFY(output.contains("--screenshot"));
    QTRY_VERIFY(!terminal.isRunning());
    output.clear(); terminal.restartSession();
    QTRY_VERIFY_WITH_TIMEOUT(output.contains("argument with spaces"), 4000);
}

QTEST_MAIN(TerminalViewTest)

#include "TerminalViewTest.moc"
