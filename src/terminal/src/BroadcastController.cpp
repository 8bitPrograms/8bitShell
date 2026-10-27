#include <bitshell/terminal/BroadcastController.h>
#include <QSet>
#include <bitshell/core/ConnectionRouting.h>
#include <algorithm>

namespace bitshell::terminal {

void BroadcastController::cancel() {
    m_entries.clear();
    m_command.clear();
    m_targets.clear();
    m_reviewAge.invalidate();
    emit changed();
}

bool BroadcastController::prepare(const QString& command, const QVariantList& terminals) {
    cancel();
    m_status.clear();
    if (command.trimmed().isEmpty() || command.size() > 16384 || command.toUtf8().size() > 65536
        || std::any_of(command.cbegin(), command.cend(), [](QChar c) {
            return (c.category() == QChar::Other_Control && c != u'\n' && c != u'\r' && c != u'\t')
                || c.category() == QChar::Other_Format || c == QChar(0x2028) || c == QChar(0x2029);
        })) {
        m_status = QStringLiteral("Use a nonempty command up to 16,384 characters, without terminal controls or invisible formatting.");
        emit changed();
        return false;
    }
    if (terminals.isEmpty() || terminals.size() > 64) {
        m_status = QStringLiteral("Explicitly select 1–64 running SSH terminals first. Local shells are excluded.");
        emit changed();
        return false;
    }
    QSet<TerminalView*> unique;
    for (const auto& value : terminals) {
        auto* terminal = qobject_cast<TerminalView*>(value.value<QObject*>());
        if (!terminal || unique.contains(terminal)) {
            m_status = QStringLiteral("Invalid or duplicate target.");
            break;
        }
        const auto error = terminal->broadcastError(command, terminal->m_broadcastGeneration);
        if (!error.isEmpty()) {
            m_status = error;
            break;
        }
        unique.insert(terminal);
        m_entries << Entry{terminal, terminal->m_broadcastGeneration};
        const auto profile = terminal->m_lastSshProfile;
        QString endpoint = profile.value(QStringLiteral("host")).toString();
        const auto plan = profile.value(QStringLiteral("psmp")).toMap();
        if (!plan.isEmpty()) {
            const auto route = plan.value(QStringLiteral("route")).toMap();
            endpoint += QStringLiteral(" via ") + route.value(QStringLiteral("host")).toString()
                + u':' + QString::number(route.value(QStringLiteral("port"), 22).toInt());
            const auto login = core::ConnectionRouting::login(profile);
            if (login) endpoint += QStringLiteral(" · ") + *login;
        } else {
            if (!profile.value(QStringLiteral("user")).toString().isEmpty())
                endpoint.prepend(profile.value(QStringLiteral("user")).toString() + u'@');
            endpoint += u':' + QString::number(profile.value(QStringLiteral("port"), 22).toInt());
        }
        QString label;
        for (const auto c : terminal->activeTarget().left(120)) {
            if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format)
                label += QStringLiteral("[U+%1]").arg(static_cast<uint>(c.unicode()), 4, 16,
                                                     QLatin1Char('0'));
            else label += c;
        }
        m_targets << endpoint + QStringLiteral(" — ") + label;
    }
    if (!m_status.isEmpty()) {
        cancel();
        return false;
    }
    // CRLF/CR become a single newline; preview exactly the normalized paste.
    m_command = command;
    m_command.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    m_command.replace(u'\r', u'\n');
    m_reviewAge.start();
    emit changed();
    return true;
}

bool BroadcastController::commit() {
    if (!pending() || !m_reviewAge.isValid() || m_reviewAge.elapsed() > 120000) {
        m_status = QStringLiteral("Review expired. Nothing was sent.");
        cancel();
        return false;
    }
    for (const auto& entry : m_entries) {
        const auto error = entry.terminal ? entry.terminal->broadcastError(m_command, entry.generation)
                                          : QStringLiteral("A selected terminal was closed. Nothing was sent.");
        if (!error.isEmpty()) {
            m_status = error;
            cancel();
            return false;
        }
    }
    // No event loop/retries between preflight and sends. Remote execution is not atomic:
    // the server can still disconnect after data has been queued to another target.
    int sent = 0;
    for (const auto& entry : m_entries) {
        if (!entry.terminal->broadcastError(m_command, entry.generation).isEmpty()) break;
        entry.terminal->writePaste(m_command, true);
        ++sent;
    }
    const bool complete = sent == m_entries.size();
    m_status = QStringLiteral("Queued to %1/%2 SSH terminals. Check each terminal's output; no automatic retry.")
        .arg(sent).arg(m_entries.size());
    cancel();
    return complete;
}

} // namespace bitshell::terminal
