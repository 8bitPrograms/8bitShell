#pragma once
#include <QCache>
#include <QRegularExpression>

namespace bitshell::core {
inline QRegularExpression boundedRegex(const QString& pattern, QRegularExpression::PatternOptions options = {}) {
    if (pattern.size() > 1024) return QRegularExpression(QStringLiteral("(")); // invalid, fails closed
    // PCRE2 uses the lower value if a pattern supplies another limit. Disable
    // JIT here so depth/heap limits apply as well as the match-work limit.
    static const QString limits = QStringLiteral("(*NO_JIT)(*LIMIT_MATCH=10000)(*LIMIT_DEPTH=64)(*LIMIT_HEAP=1024)");
    const QString bounded = pattern.startsWith(limits) ? pattern : limits + pattern;
    const QString key = QString::number(options.toInt()) + u':' + bounded;
    thread_local QCache<QString, QRegularExpression> cache(128);
    if (const auto* cached = cache.object(key)) return *cached;
    QRegularExpression expression(bounded, options);
    cache.insert(key, new QRegularExpression(expression));
    return expression;
}
} // namespace bitshell::core
