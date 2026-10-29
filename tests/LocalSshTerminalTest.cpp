#include <QtTest>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <algorithm>
#include <bitshell/core/Platform.h>
#include <bitshell/terminal/LocalSshCommand.h>
#include "TerminalTestAccess.h"

using bitshell::terminal::LocalSshCommand;
using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

class LocalSshTerminalTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_profile;
    static QByteArray marker(const TerminalView& terminal, const char* phase) {
        return QByteArray("\x1b]133;") + phase + ';' + TerminalTestAccess::integrationToken(terminal).toLatin1() + '\a';
    }
    static QByteArray prompt(const TerminalView& terminal) {
        return QByteArray("\r\nPS> \x1b]777;8bitshell;") + TerminalTestAccess::integrationToken(terminal).toLatin1()
            + ";powershell\a" + marker(terminal, "A") + marker(terminal, "B");
    }
    static void key(TerminalView& terminal, int code, QString text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QKeyEvent event(QEvent::KeyPress, code, modifiers, text);
        QCoreApplication::sendEvent(&terminal, &event);
    }
    static bool startIdle(TerminalView& terminal) {
        terminal.setWidth(1200); terminal.setHeight(500);
#ifdef Q_OS_WIN
        return TerminalTestAccess::startIntegratedLocal(terminal,
            QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe"), {QStringLiteral("broadcast")});
#else
        return TerminalTestAccess::startIntegratedLocal(terminal, QStringLiteral("/bin/sh"),
            {QStringLiteral("-c"), QStringLiteral("stty -echo -isig; printf 'READY\\n'; while IFS= read -r line; do :; done")});
#endif
    }
private slots:
    void initTestCase() {
        QVERIFY(m_profile.isValid());
        qputenv("BIT8SHELL_PROFILE", m_profile.path().toUtf8());
    }
    void simpleGrammar_data() {
        QTest::addColumn<QString>("command");
        QTest::addColumn<QString>("host");
        QTest::addColumn<QString>("user");
        QTest::addColumn<int>("port");
        QTest::newRow("ip") << QStringLiteral("ssh 10.0.0.4") << QStringLiteral("10.0.0.4") << QString{} << 0;
        QTest::newRow("account") << QStringLiteral("ssh user@host.example") << QStringLiteral("host.example") << QStringLiteral("user") << 0;
        QTest::newRow("port") << QStringLiteral("ssh -p 2222 -l admin host.example") << QStringLiteral("host.example") << QStringLiteral("admin") << 2222;
        QTest::newRow("ipv6") << QStringLiteral("ssh.exe -p2222 bob@[2001:db8::1]") << QStringLiteral("[2001:db8::1]") << QStringLiteral("bob") << 2222;
        QTest::newRow("dense") << QStringLiteral(" ssh -ladmin -- host ") << QStringLiteral("host") << QStringLiteral("admin") << 0;
    }
    void simpleGrammar() {
        QFETCH(QString, command); QFETCH(QString, host); QFETCH(QString, user); QFETCH(int, port);
        const auto query = LocalSshCommand::parse(command);
        QCOMPARE(query.value(QStringLiteral("host")).toString(), host);
        QCOMPARE(query.value(QStringLiteral("user")).toString(), user);
        QCOMPARE(query.value(QStringLiteral("port")).toInt(), port);
        QCOMPARE(query.value(QStringLiteral("command")).toString(), command);
    }
    void complexInputIsUntouched_data() {
        QTest::addColumn<QString>("command");
        const QStringList values{QStringLiteral("ssh host uptime"), QStringLiteral("ssh -i key host"),
            QStringLiteral("ssh -oProxyCommand=evil host"), QStringLiteral("ssh host; whoami"),
            QStringLiteral("ssh host | cat"), QStringLiteral("ssh $(host)"), QStringLiteral("ssh `host`"),
            QStringLiteral("ssh 'host'"), QStringLiteral("ssh host > file"), QStringLiteral("ssh -p 0 host"),
            QStringLiteral("ssh -p 65536 host"), QStringLiteral("ssh -l user other@host"),
            QStringLiteral("ssh -p 22 -p 23 host"), QStringLiteral("ssh\nhost"),
            QStringLiteral("ssh host && true"), QStringLiteral("ssh"), QStringLiteral("password: ssh host")};
        for (int i = 0; i < values.size(); ++i) QTest::newRow(qPrintable(QString::number(i))) << values[i];
    }
    void complexInputIsUntouched() { QFETCH(QString, command); QVERIFY(LocalSshCommand::parse(command).isEmpty()); }
    void authenticatedPromptAndKeyboardSelection() {
        TerminalView terminal; QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(startIdle(terminal));
        QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
        TerminalTestAccess::resetLocalScreen(terminal);
        TerminalTestAccess::feed(terminal, "PS> ssh ordinary.host");
        QTest::qWait(70); QVERIFY(terminal.localSshQuery().isEmpty());
        auto bytes = prompt(terminal) + "ssh user@10.0.0.4";
        // OSC sequences may be split across arbitrary ConPTY read boundaries.
        for (char c : bytes) TerminalTestAccess::feed(terminal, QByteArray(1, c));
        QTRY_COMPARE(terminal.localSshQuery().value(QStringLiteral("host")).toString(), QStringLiteral("10.0.0.4"));
        QCOMPARE(terminal.localSshQuery().value(QStringLiteral("user")).toString(), QStringLiteral("user"));
        terminal.setProperty("localSshSuggestionsActive", true);
        terminal.setProperty("localSshSelectionReady", true);
        QSignalSpy navigate(&terminal, &TerminalView::localSshNavigate), accept(&terminal, &TerminalView::localSshAccept);
        key(terminal, Qt::Key_Down); QCOMPARE(navigate.size(), 1);
        QVERIFY(TerminalTestAccess::localKeyConsumed(terminal, Qt::Key_Down));
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Down, Qt::NoModifier);
        QCoreApplication::sendEvent(&terminal, &release);
        QVERIFY(!TerminalTestAccess::localKeyConsumed(terminal, Qt::Key_Down));
        key(terminal, Qt::Key_Return, QStringLiteral("\r")); QCOMPARE(accept.size(), 1);
        key(terminal, Qt::Key_Escape); QVERIFY(terminal.localSshQuery().isEmpty());
        TerminalTestAccess::feed(terminal, ""); QTest::qWait(70);
        QVERIFY(terminal.localSshQuery().isEmpty());
        TerminalTestAccess::feed(terminal, "5");
        QTRY_COMPARE(terminal.localSshQuery().value(QStringLiteral("host")).toString(), QStringLiteral("10.0.0.45"));
        TerminalTestAccess::stopFixture(terminal);
    }
    void runningApplicationsAndAmbiguousInputStayDisabled() {
        TerminalView terminal; QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(startIdle(terminal)); QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
        TerminalTestAccess::resetLocalScreen(terminal);
        auto wrong = prompt(terminal); wrong.replace(TerminalTestAccess::integrationToken(terminal).toLatin1(), "untrusted-remote-token");
        TerminalTestAccess::feed(terminal, wrong + "ssh host"); QTest::qWait(70);
        QVERIFY(terminal.localSshQuery().isEmpty());
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh host");
        QTRY_VERIFY(!terminal.localSshQuery().isEmpty());
        const auto old = terminal.localSshQuery();
        TerminalTestAccess::feed(terminal, marker(terminal, "C") + "\r\nPassword: ssh host");
        QTRY_VERIFY(terminal.localSshQuery().isEmpty());
        QSignalSpy errors(&terminal, &TerminalView::localSshError);
        QVERIFY(!terminal.runLocalSsh({{QStringLiteral("host"), QStringLiteral("host")}}, old)); QCOMPARE(errors.size(), 1);
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh host\x1b[?1049h");
        QTest::qWait(70); QVERIFY(terminal.localSshQuery().isEmpty());
        TerminalTestAccess::feed(terminal, "\x1b[?1049l" + prompt(terminal) + "ssh host\x1b[3D");
        QTest::qWait(70); QVERIFY(terminal.localSshQuery().isEmpty());
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh host");
        QTRY_VERIFY(!terminal.localSshQuery().isEmpty());
        terminal.setWidth(1300); QVERIFY(terminal.localSshQuery().isEmpty());
        TerminalTestAccess::stopFixture(terminal);
    }
    void inputApisInvalidatePreviousSuggestion_data() {
        QTest::addColumn<QString>("api");
        QTest::newRow("sendText") << QStringLiteral("text");
        QTest::newRow("paste") << QStringLiteral("paste");
        QTest::newRow("macro-enter") << QStringLiteral("macro");
        QTest::newRow("keypress") << QStringLiteral("key");
    }
    void inputApisInvalidatePreviousSuggestion() {
        QFETCH(QString, api);
        TerminalView terminal; QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(startIdle(terminal)); QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
        TerminalTestAccess::resetLocalScreen(terminal);
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh host");
        QTRY_VERIFY(!terminal.localSshQuery().isEmpty());
        const auto previous = terminal.localSshQuery();
        // There is deliberately no echo between invalidation and activation.
        if (api == QStringLiteral("text")) terminal.sendText(QStringLiteral("x"));
        else if (api == QStringLiteral("paste")) terminal.pasteText(QStringLiteral("x"));
        else if (api == QStringLiteral("macro")) { terminal.sendMacro(QStringLiteral("echo harmless"), true); terminal.confirmPaste(true); }
        else key(terminal, Qt::Key_X, QStringLiteral("x"));
        QVERIFY(terminal.localSshQuery().isEmpty());
        QSignalSpy errors(&terminal, &TerminalView::localSshError);
        QVERIFY(!terminal.runLocalSsh({{QStringLiteral("host"), QStringLiteral("host")}}, previous));
        QCOMPARE(errors.size(), 1);
        QVERIFY(!TerminalTestAccess::localSshPending(terminal));
        TerminalTestAccess::stopFixture(terminal);
    }
    void manuallyTypedConnectionMetadataEndsAtTheAuthenticatedPrompt() {
        TerminalView terminal; QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(startIdle(terminal)); QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
        TerminalTestAccess::resetLocalScreen(terminal);
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh -p 2222 ops@192.0.2.44");
        QTRY_VERIFY(!terminal.localSshQuery().isEmpty());
        QVERIFY(terminal.currentSshConnection().isEmpty());
        key(terminal, Qt::Key_Return, QStringLiteral("\r"));
        auto connection = terminal.currentSshConnection();
        QCOMPARE(connection.value(QStringLiteral("host")).toString(), QStringLiteral("192.0.2.44"));
        QCOMPARE(connection.value(QStringLiteral("user")).toString(), QStringLiteral("ops"));
        QCOMPARE(connection.value(QStringLiteral("port")).toInt(), 2222);
        QCOMPARE(connection.value(QStringLiteral("mode")).toString(), QStringLiteral("local"));
#ifdef Q_OS_WIN
        // Enter is sent to the live fixture. Drain its asynchronous reply before
        // placing another synthetic prompt on the same terminal screen.
        QTRY_VERIFY2_WITH_TIMEOUT(output.count("ACK:") >= 1, output.right(3000).constData(), 10000);
#endif
        TerminalTestAccess::feed(terminal, marker(terminal, "C") + "\r\nPassword: ");
        QCOMPARE(terminal.currentSshConnection(), connection);
        auto remotePrompt = prompt(terminal);
        remotePrompt.replace(TerminalTestAccess::integrationToken(terminal).toLatin1(), "another-shell-token");
        TerminalTestAccess::feed(terminal, remotePrompt);
        QCOMPARE(terminal.currentSshConnection(), connection);
        TerminalTestAccess::feed(terminal, prompt(terminal));
        QVERIFY(terminal.currentSshConnection().isEmpty());
        TerminalTestAccess::feed(terminal, "ssh ignored.example.test | cat");
        QTest::qWait(70);
        key(terminal, Qt::Key_Return, QStringLiteral("\r"));
        QVERIFY(terminal.currentSshConnection().isEmpty());
#ifdef Q_OS_WIN
        QTRY_VERIFY2_WITH_TIMEOUT(output.count("ACK:") >= 2, output.right(3000).constData(), 10000);
#endif
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh 192.0.2.45");
        QTRY_VERIFY2(!terminal.localSshQuery().isEmpty(),
            (QJsonDocument::fromVariant(TerminalTestAccess::localSshDiagnostics(terminal)).toJson(QJsonDocument::Compact)
             + " OUTPUTHEX=" + output.right(3000).toHex()).constData());
        key(terminal, Qt::Key_Return, QStringLiteral("\r"));
        QVERIFY(!terminal.currentSshConnection().isEmpty());
        TerminalTestAccess::stopFixture(terminal);
        QTRY_VERIFY(terminal.currentSshConnection().isEmpty());
    }
    void assistedMetadataWaitsForDispatchAndCancellationLeavesNoDestination() {
        TerminalView terminal; QByteArray output;
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        QVERIFY(startIdle(terminal)); QTRY_VERIFY_WITH_TIMEOUT(output.contains("READY"), 10000);
        TerminalTestAccess::resetLocalScreen(terminal);
        const QVariantMap profile{{QStringLiteral("host"), QStringLiteral("192.0.2.55")},
            {QStringLiteral("user"), QStringLiteral("saved-user")}, {QStringLiteral("port"), 2200},
            {QStringLiteral("identityKey"), QStringLiteral("none")}};
        TerminalTestAccess::feed(terminal, prompt(terminal) + "ssh 192.0.2.55");
        QTRY_VERIFY(!terminal.localSshQuery().isEmpty());
        QVERIFY(terminal.runLocalSsh(profile, terminal.localSshQuery()));
        QVERIFY(terminal.currentSshConnection().isEmpty());
        key(terminal, Qt::Key_Escape);
        TerminalTestAccess::feed(terminal, prompt(terminal));
        QVERIFY(terminal.currentSshConnection().isEmpty());
        QVERIFY(!TerminalTestAccess::localSshPending(terminal));
        TerminalTestAccess::feed(terminal, "ssh 192.0.2.55");
        QTRY_VERIFY(!terminal.localSshQuery().isEmpty());
        QVERIFY(terminal.runLocalSsh(profile, terminal.localSshQuery()));
        QVERIFY(terminal.currentSshConnection().isEmpty());
        TerminalTestAccess::feed(terminal, prompt(terminal));
        QCOMPARE(terminal.currentSshConnection().value(QStringLiteral("host")).toString(), QStringLiteral("192.0.2.55"));
        QCOMPARE(terminal.currentSshConnection().value(QStringLiteral("user")).toString(), QStringLiteral("saved-user"));
        QCOMPARE(terminal.currentSshConnection().value(QStringLiteral("port")).toInt(), 2200);
        QVERIFY(!TerminalTestAccess::localSshPending(terminal));
        TerminalTestAccess::resetLocalScreen(terminal);
        QVERIFY(terminal.currentSshConnection().isEmpty());
        TerminalTestAccess::stopFixture(terminal);
    }
    void managedMetadataAndCyberArkDisplayStaySeparateFromPrivateProfileData() {
        TerminalView terminal;
        QVERIFY(TerminalTestAccess::startBroadcastFixture(terminal, QStringLiteral("managed-target")));
        QCOMPARE(terminal.currentSshConnection().value(QStringLiteral("host")).toString(), QStringLiteral("managed-target.example.test"));
        QCOMPARE(terminal.currentSshConnection().value(QStringLiteral("mode")).toString(), QStringLiteral("managed"));
        QVERIFY(startIdle(terminal));
        QVERIFY(terminal.currentSshConnection().isEmpty());
        TerminalTestAccess::stopFixture(terminal);
        const QVariantMap profile{{QStringLiteral("id"), QStringLiteral("catalog-id")},
            {QStringLiteral("name"), QStringLiteral("Production")}, {QStringLiteral("host"), QStringLiteral("192.0.2.9")},
            {QStringLiteral("identityFile"), QStringLiteral("/private/key")}, {QStringLiteral("identityKey"), QStringLiteral("secret-key-id")},
            {QStringLiteral("psmp"), QVariantMap{{QStringLiteral("routeId"), QStringLiteral("psmp-prod")},
                {QStringLiteral("operator"), QStringLiteral("employee")},
                {QStringLiteral("account"), QVariantMap{{QStringLiteral("source"), QStringLiteral("identity")},
                    {QStringLiteral("suffix"), QStringLiteral("SYS")}, {QStringLiteral("domain"), QStringLiteral("corp")}}},
                {QStringLiteral("route"), QVariantMap{{QStringLiteral("host"), QStringLiteral("gateway.example.test")}}}}}};
        const auto summary = TerminalTestAccess::sshConnectionSummary(profile, QStringLiteral("managed"));
        QCOMPARE(summary.value(QStringLiteral("user")).toString(), QStringLiteral("employeeSYS@corp"));
        QCOMPARE(summary.value(QStringLiteral("gateway")).toString(), QStringLiteral("gateway.example.test"));
        QCOMPARE(summary.value(QStringLiteral("connectionMode")).toString(), QStringLiteral("cyberark"));
        QCOMPARE(summary.value(QStringLiteral("catalogId")).toString(), QStringLiteral("catalog-id"));
        QVERIFY(!summary.contains(QStringLiteral("identityFile")));
        QVERIFY(!summary.contains(QStringLiteral("identityKey")));
        QVERIFY(!summary.contains(QStringLiteral("psmp")));
    }
#ifdef Q_OS_WIN
    void nativePowerShellExecutesInTheExistingProcess() {
        QTemporaryDir isolated;
        QVERIFY(isolated.isValid());
        QVERIFY(QFile::copy(QCoreApplication::applicationDirPath() + QStringLiteral("/local_ssh_fixture.exe"), isolated.filePath(QStringLiteral("ssh.exe"))));
        const auto originalPath = qgetenv("PATH");
        qputenv("PATH", QDir::toNativeSeparators(isolated.path()).toUtf8() + ';' + originalPath);
        TerminalView terminal; terminal.setWidth(1500); terminal.setHeight(700); QByteArray output;
        QVariantList observedConnections;
        connect(&terminal, &TerminalView::currentSshConnectionChanged, this,
            [&] { observedConnections.append(terminal.currentSshConnection()); });
        connect(&terminal, &TerminalView::rawOutputReceived, this, [&](const QByteArray& bytes) { output += bytes; });
        const bool started = TerminalTestAccess::startIsolatedPowerShell(terminal, bitshell::core::Platform::windowsSystemDirectory()
            + QStringLiteral("/WindowsPowerShell/v1.0/powershell.exe"), isolated.filePath(QStringLiteral("history.txt")));
        qputenv("PATH", originalPath);
        QVERIFY(started);
        QTRY_VERIFY2_WITH_TIMEOUT(TerminalTestAccess::localPromptGeneration(terminal) > 0, output.right(1800).constData(), 15000);
        // Maximize-like resize at an empty authenticated prompt must retain
        // assistance; the nonempty resize case above deliberately disables it.
        const auto promptBeforeResize = TerminalTestAccess::localPromptGeneration(terminal);
        terminal.setWidth(1600);
        QVERIFY(TerminalTestAccess::localPromptGeneration(terminal) > promptBeforeResize);
        QTest::qWait(100);
        const auto pid = TerminalTestAccess::processId(terminal);
        for (QChar character : QStringLiteral("ssh 192.0.2.123")) {
            key(terminal, Qt::Key_unknown, QString(character));
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, QString(character));
            QCoreApplication::sendEvent(&terminal, &release);
        }
        QTRY_VERIFY2_WITH_TIMEOUT(terminal.localSshQuery().value(QStringLiteral("host")).toString() == QStringLiteral("192.0.2.123"),
            (QJsonDocument::fromVariant(TerminalTestAccess::localSshDiagnostics(terminal)).toJson(QJsonDocument::Compact)
             + " OUTPUTHEX=" + output.right(6000).toHex()).constData(), 10000);
        const auto query = terminal.localSshQuery();
        QSignalSpy errors(&terminal, &TerminalView::localSshError);
        QVERIFY(terminal.runLocalSsh({{QStringLiteral("host"), QStringLiteral("192.0.2.123")},
            {QStringLiteral("user"), QStringLiteral("saved-account")}, {QStringLiteral("port"), 2222},
            {QStringLiteral("identityKey"), QStringLiteral("none")}}, query));
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("LOCAL_SSH_STUB_EXECUTED"), (output.right(3000) + QByteArray(" ERRORS=") + QByteArray::number(errors.size())).constData(), 15000);
        QCOMPARE(errors.size(), 0);
        QVERIFY(terminal.isRunning()); QCOMPARE(TerminalTestAccess::processId(terminal), pid);
        QTRY_VERIFY_WITH_TIMEOUT(!TerminalTestAccess::localSshPending(terminal), 10000);
        // The executable marker and argument lines can arrive in separate PTY
        // reads. Wait for the argv evidence instead of racing the reader.
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("00730061007600650064002d006100630063006f0075006e0074"),
            output.right(5000).constData(), 10000);
        QTRY_VERIFY2_WITH_TIMEOUT(output.contains("0032003200320032"), output.right(5000).constData(), 10000);
        QVERIFY(std::any_of(observedConnections.cbegin(), observedConnections.cend(), [](const QVariant& value) {
            const auto connection = value.toMap();
            return connection.value(QStringLiteral("host")) == QStringLiteral("192.0.2.123")
                && connection.value(QStringLiteral("user")) == QStringLiteral("saved-account")
                && connection.value(QStringLiteral("port")).toInt() == 2222
                && connection.value(QStringLiteral("mode")) == QStringLiteral("local");
        }));
        QTRY_VERIFY_WITH_TIMEOUT(terminal.currentSshConnection().isEmpty(), 10000);
        TerminalTestAccess::stopFixture(terminal);
    }
#endif
};
QTEST_MAIN(LocalSshTerminalTest)
#include "LocalSshTerminalTest.moc"
