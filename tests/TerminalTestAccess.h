#pragma once
#include <bitshell/terminal/TerminalView.h>
#include <QCoreApplication>

namespace bitshell::terminal {
class TerminalTestAccess {
public:
    static bool startBroadcastFixture(TerminalView& terminal, const QString& name) {
        terminal.setLogEnabled(false);
        terminal.m_lastSshProfile = {{QStringLiteral("host"), name + QStringLiteral(".example.test")}};
#ifdef Q_OS_WIN
        return terminal.launchProcess(QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe"),
            {QStringLiteral("broadcast")}, name, QStringLiteral("Fixture ready"), true);
#else
        return terminal.launchProcess(QStringLiteral("/bin/sh"), {QStringLiteral("-c"),
            QStringLiteral("stty -echo; printf 'READY\\n'; while IFS= read -r line; do printf 'ACK:%s\\n' \"$line\"; done")}, name, QStringLiteral("Fixture ready"), true);
#endif
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
};
}
