#pragma once

#include <QRegularExpression>
#include <QString>
#include <QUrl>

namespace bitshell::terminal {
// Bounded, on-demand matching; ordinary text output does no URL indexing.
inline QUrl terminalUrlAt(const QString& text, qsizetype offset) {
    if (offset < 0 || offset >= text.size() || text.size() > 8192) return {};
    static const QRegularExpression pattern(
        QStringLiteral(R"((?<![\p{L}\p{N}_@])(?:https?://|www\.)[^\s<>"\x00-\x1f\x7f]+)"),
        QRegularExpression::CaseInsensitiveOption);
    auto matches = pattern.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        QString candidate = match.captured();
        // Sentence punctuation is not part of a printed link. Balanced path
        // parentheses and IPv6 brackets remain part of the destination.
        while (!candidate.isEmpty()) {
            const auto last = candidate.back();
            bool trim = QStringLiteral(".,;:!?'").contains(last);
            for (const auto pair : {QStringLiteral("()"), QStringLiteral("[]"), QStringLiteral("{}")})
                trim |= last == pair[1] && candidate.count(pair[1]) > candidate.count(pair[0]);
            if (!trim) break;
            candidate.chop(1);
        }
        if (offset < match.capturedStart() || offset >= match.capturedStart() + candidate.size()) continue;
        if (candidate.startsWith(QStringLiteral("www."), Qt::CaseInsensitive)) candidate.prepend(QStringLiteral("https://"));
        const QUrl url(candidate, QUrl::StrictMode);
        if (url.isValid() && !url.host().isEmpty()
            && (url.scheme() == QStringLiteral("https") || url.scheme() == QStringLiteral("http"))) return url;
    }
    return {};
}
} // namespace bitshell::terminal
