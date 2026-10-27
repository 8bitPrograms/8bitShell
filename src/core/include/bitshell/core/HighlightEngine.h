#pragma once

#include <QColor>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

namespace bitshell::core {

struct HighlightRule {
    QString id;
    QString displayName;
    QRegularExpression pattern;
    QColor foreground;
    QColor background;
    bool bold = false;
    bool underline = false;
    bool enabled = true;
    int priority = 0;
};

struct HighlightSpan {
    qsizetype start = 0;
    qsizetype length = 0;
    QString ruleId;
    QColor foreground;
    QColor background;
    bool bold = false;
    bool underline = false;

    friend bool operator==(const HighlightSpan&, const HighlightSpan&) = default;
};

class HighlightEngine {
public:
    [[nodiscard]] QList<HighlightSpan> highlight(QStringView line,
                                                 const QList<HighlightRule>& rules) const;

    [[nodiscard]] static QList<HighlightRule> defaultRules();
};

} // namespace bitshell::core

