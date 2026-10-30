#pragma once
#include <bitshell/terminal/TerminalView.h>
#include <bitshell/core/Platform.h>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QUuid>

namespace bitshell::terminal {
class TerminalTestAccess {
public:
    static bool startIntegratedLocal(TerminalView& terminal, const QString& program, const QStringList& arguments) {
        terminal.setLogEnabled(false);
        return terminal.launchProcess(program, arguments, QStringLiteral("Local test"),
            QStringLiteral("Starting local profile."), false, true);
    }
    static bool startIsolatedPowerShell(TerminalView& terminal, const QString& program, QString historyPath) {
        terminal.setLogEnabled(false);
        terminal.resetTerminal();
        terminal.m_managedSsh = false;
        terminal.m_lastInteractiveShell = true;
        terminal.m_shellIntegrationToken = QUuid::createUuid().toString(QUuid::Id128);
        auto arguments = core::Platform::persistentHistoryArguments(program,
            {QStringLiteral("-NoLogo"), QStringLiteral("-NoProfile")}, terminal.m_shellIntegrationToken);
        historyPath.replace(u'\'', QStringLiteral("''"));
        // Configure history before the first ReadLine, so neither the startup
        // commands nor the fake SSH invocation touch desktop-user history.
        arguments.last() += QStringLiteral("; Set-PSReadLineOption -HistorySavePath '%1' -HistorySaveStyle SaveNothing;").arg(historyPath);
        terminal.m_process.setInteractiveShell(false); // Arguments already have the production integration bootstrap.
        QString error;
        return terminal.m_process.start(program, arguments, terminal.m_columns, terminal.m_rows, &error);
    }
    static QString integrationToken(const TerminalView& terminal) { return terminal.m_shellIntegrationToken; }
    static quint64 localPromptGeneration(const TerminalView& terminal) { return terminal.m_localPromptGeneration; }
    static qint64 processId(const TerminalView& terminal) { return terminal.m_process.processId(); }
    static bool localSshPending(const TerminalView& terminal) { return !terminal.m_pendingLocalSshCommand.isEmpty(); }
    // A saved secret is waiting to answer this tab's login prompt.
    static bool passwordHandoffActive(const TerminalView& terminal) {
        return terminal.m_credential != nullptr || terminal.m_passwordBroker != nullptr;
    }
    static void resetLocalScreen(TerminalView& terminal) { terminal.resetTerminal(); }
    static bool localKeyConsumed(const TerminalView& terminal, int key) { return terminal.m_consumedLocalSshKeys.contains(key); }
    static QVariantMap sshConnectionSummary(const QVariantMap& profile, const QString& mode) {
        return TerminalView::sshConnectionSummary(profile, mode);
    }
    static QVariantMap localSshDiagnostics(const TerminalView& terminal) {
        VTermPos cursor{};
        vterm_state_get_cursorpos(vterm_obtain_state(terminal.m_terminal), &cursor);
        return {{QStringLiteral("active"), terminal.m_localPromptActive},
            {QStringLiteral("shell"), terminal.m_integratedShellKind},
            {QStringLiteral("generation"), QVariant::fromValue(terminal.m_localPromptGeneration)},
            {QStringLiteral("row"), cursor.row}, {QStringLiteral("column"), cursor.col},
            {QStringLiteral("inputRow"), QVariant::fromValue(terminal.m_localInputRow)},
            {QStringLiteral("inputColumn"), terminal.m_localInputColumn},
            {QStringLiteral("input"), terminal.localInputTextLocked()},
            {QStringLiteral("lastInput"), terminal.m_lastLocalInput},
            {QStringLiteral("win32"), terminal.m_win32Input.enabled()}};
    }
    static bool startBroadcastFixture(TerminalView& terminal, const QString& name) {
        terminal.setLogEnabled(false);
        terminal.m_lastSshProfile = {{QStringLiteral("host"), name + QStringLiteral(".example.test")}};
#ifdef Q_OS_WIN
        const bool started = terminal.launchProcess(QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe"),
            {QStringLiteral("broadcast")}, name, QStringLiteral("Fixture ready"), true);
#else
        const bool started = terminal.launchProcess(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
            QStringLiteral("stty -echo; printf 'READY\\n'; while IFS= read -r line; do printf 'ACK:%s\\n' \"$line\"; done")}, name, QStringLiteral("Fixture ready"), true);
#endif
        if (started) terminal.setCurrentSshConnection(TerminalView::sshConnectionSummary(
            terminal.m_lastSshProfile, QStringLiteral("managed")));
        return started;
    }
    static bool startManagedProfileFixture(TerminalView& terminal, const QVariantMap& profile) {
        if (!startBroadcastFixture(terminal, QStringLiteral("managed-fixture"))) return false;
        terminal.m_lastSshProfile = profile;
        terminal.setCurrentSshConnection(TerminalView::sshConnectionSummary(profile, QStringLiteral("managed")));
        return true;
    }
    static void stopFixture(TerminalView& terminal) { terminal.m_process.stop(); }
    static void feed(TerminalView& terminal, const QByteArray& bytes) { terminal.handleProcessData(bytes); }
    static std::vector<VTermScreenCell> popScrollbackRow(TerminalView& terminal, int columns) {
        std::vector<VTermScreenCell> cells(static_cast<size_t>(columns));
        if (!TerminalView::scrollbackPopCallback(columns, cells.data(), &terminal)) return {};
        return cells;
    }
    static void repaintContent(TerminalView& terminal, QPainter* painter) { terminal.paintContent(painter); }
    static quint64 contentPaints(const TerminalView& terminal) { return terminal.m_contentPaintCount; }
    static quint64 sceneUpdates(const TerminalView& terminal) { return terminal.m_sceneUpdateCount; }
    static qsizetype cachedBytes(const TerminalView& terminal) { return terminal.m_contentCache.sizeInBytes(); }
    static std::size_t historyBytes(const TerminalView& terminal) { return terminal.m_scrollbackBytes; }
    static QStringList matches(const TerminalView& terminal) {
        QStringList result;
        for (const auto& match : terminal.m_searchMatches)
            result << QStringLiteral("%1:%2-%3:%4").arg(match.start.line).arg(match.start.column).arg(match.end.line).arg(match.end.column);
        return result;
    }
    static void fullSearch(TerminalView& terminal) { terminal.rebuildSearchMatchesLocked(); }
    static QString cellText(const TerminalView& terminal, const VTermScreenCell& cell) { return terminal.textForCell(cell); }
    static QFont font(const TerminalView& terminal) { return terminal.terminalFont(); }
    static bool startFixture(TerminalView& terminal, const QString& script, QString& error) {
        return terminal.m_process.start(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), script}, terminal.m_columns, terminal.m_rows, &error);
    }
    static void useCredentials(TerminalView& terminal, core::SecretVault& vault, const QString& path) {
        terminal.m_credentialVault = &vault;
        terminal.m_credentialPath = path;
    }
    // The saved-session path of launchSshProfile, with a script in place of ssh.
    // notice is the non-blocking message shown when a secret cannot be used.
    static void prepareCredential(TerminalView& terminal, const QVariantMap& profile, QVariantMap& effective, QString& notice) {
        terminal.prepareCredential(profile, effective, notice);
    }
    static bool startCredentialFixture(TerminalView& terminal, const QVariantMap& profile, const QString& script,
                                       QString& error, QVariantMap* effective = nullptr, QString* notice = nullptr) {
        QVariantMap prepared;
        QString message;
        terminal.prepareCredential(profile, prepared, message);
        if (effective) *effective = prepared;
        if (notice) *notice = message;
        if (!startFixture(terminal, script, error)) { terminal.releaseCredential(); return false; }
        terminal.armPreparedCredential();
        return true;
    }
    static QVariantMap lastSshProfile(const TerminalView& terminal) { return terminal.m_lastSshProfile; }
    static bool credentialArmed(const TerminalView& terminal) { return terminal.m_credential != nullptr; }
    // What the shell-integration path records when the user types ssh in a tab.
    static void noteTypedSsh(TerminalView& terminal, const QVariantMap& requested) {
        terminal.setCurrentSshConnection(TerminalView::sshConnectionSummary(requested, QStringLiteral("local")));
    }
    // A key press through the view's real input path.
    static void press(TerminalView& terminal, Qt::Key key, const QString& text = {}) {
        QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier, text);
        terminal.keyPressEvent(&event);
    }
    static int boundCredentialMs(const TerminalView& terminal) { return terminal.m_boundCredentialMs; }
    static void setBoundCredentialMs(TerminalView& terminal, int ms) { terminal.m_boundCredentialMs = ms; }
};
}
