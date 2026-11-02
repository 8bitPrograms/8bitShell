#include <bitshell/terminal/TerminalView.h>
#include <bitshell/terminal/TerminalUrl.h>

#include <QCursor>
#include <QHoverEvent>
#include <QMutexLocker>
#include <algorithm>

namespace bitshell::terminal {
QUrl TerminalView::urlAtLocked(const QPointF& position) const {
    if (position.x() < m_horizontalPadding || position.y() < m_verticalPadding
        || position.x() >= m_horizontalPadding + m_columns * m_cellWidth
        || position.y() >= m_verticalPadding + m_rows * m_cellHeight) return {};
    const auto point = textPointAt(position);
    const int history = int(m_scrollback.size());
    const auto continuation = [&](int line) {
        const int row = line - history;
        if (row < 0 || row >= m_rows) return false;
        const auto* info = vterm_state_get_lineinfo(vterm_obtain_state(m_terminal), row);
        return info && info->continuation != 0;
    };
    int first = point.line, last = point.line;
    // Join only explicit soft wraps in the live screen. Hard newlines (including
    // unrelated history rows) must never turn two destinations into one URL.
    while (first > history && point.line - first < 7 && continuation(first)) --first;
    while (last + 1 < history + m_rows && last - first < 7 && continuation(last + 1)) ++last;
    if ((first >= history && continuation(first))
        || (last + 1 < history + m_rows && continuation(last + 1))) return {};
    QString text;
    qsizetype clickedOffset = -1;
    for (int line = first; line <= last; ++line) {
        const auto cells = lineAt(line);
        for (int column = 0; column < int(cells.size()); ++column) {
            const auto& cell = cells[size_t(column)];
            if (cell.width == 0) continue;
            if (line == point.line && point.column >= column
                && point.column < column + std::max(1, int(cell.width))) clickedOffset = text.size();
            const auto glyph = textForCell(cell);
            text += glyph.isEmpty() ? QStringLiteral(" ") : glyph;
        }
        if (text.size() > 8192) return {};
    }
    return terminalUrlAt(text, clickedOffset);
}

void TerminalView::updateLinkCursor(const QPointF& position, Qt::KeyboardModifiers modifiers) {
    QMutexLocker locker(&m_terminalMutex);
    if (modifiers.testFlag(Qt::ControlModifier) && !urlAtLocked(position).isEmpty())
        setCursor(QCursor(Qt::PointingHandCursor));
    else unsetCursor();
}
void TerminalView::hoverMoveEvent(QHoverEvent* event) {
    updateLinkCursor(event->position(), event->modifiers());
    QQuickPaintedItem::hoverMoveEvent(event);
}
void TerminalView::hoverLeaveEvent(QHoverEvent* event) {
    unsetCursor();
    QQuickPaintedItem::hoverLeaveEvent(event);
}
} // namespace bitshell::terminal
