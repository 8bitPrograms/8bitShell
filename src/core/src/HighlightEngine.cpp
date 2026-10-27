#include <bitshell/core/HighlightEngine.h>
#include <bitshell/core/BoundedRegex.h>

#include <limits>

#include <QVector>

namespace bitshell::core {

QList<HighlightSpan> HighlightEngine::highlight(
    QStringView line,
    const QList<HighlightRule>& rules) const {
    if (line.isEmpty() || rules.isEmpty() || line.size() > 16384) {
        return {};
    }

    QVector<int> owners(line.size(), -1);
    QVector<int> priorities(line.size(), std::numeric_limits<int>::min());

    const QString ownedLine = line.toString();
    for (qsizetype ruleIndex = 0; ruleIndex < std::min(qsizetype(64), rules.size()); ++ruleIndex) {
        const auto& rule = rules.at(ruleIndex);
        if (!rule.enabled || !rule.pattern.isValid()) {
            continue;
        }

        const auto expression = boundedRegex(rule.pattern.pattern(), rule.pattern.patternOptions());
        if (!expression.isValid()) continue;
        auto matches = expression.globalMatch(ownedLine);
        while (matches.hasNext()) {
            const auto match = matches.next();
            if (!match.hasMatch() || match.capturedLength() <= 0) {
                continue;
            }

            const qsizetype begin = match.capturedStart();
            const qsizetype end = begin + match.capturedLength();
            for (qsizetype offset = begin; offset < end && offset < owners.size(); ++offset) {
                if (rule.priority > priorities.at(offset)
                    || (rule.priority == priorities.at(offset)
                        && (owners.at(offset) < 0 || ruleIndex < owners.at(offset)))) {
                    owners[offset] = static_cast<int>(ruleIndex);
                    priorities[offset] = rule.priority;
                }
            }
        }
    }

    QList<HighlightSpan> spans;
    qsizetype offset = 0;
    while (offset < owners.size()) {
        const int owner = owners.at(offset);
        if (owner < 0) {
            ++offset;
            continue;
        }

        const qsizetype start = offset;
        while (offset < owners.size() && owners.at(offset) == owner) {
            ++offset;
        }

        const auto& rule = rules.at(owner);
        spans.append(HighlightSpan{
            .start = start,
            .length = offset - start,
            .ruleId = rule.id,
            .foreground = rule.foreground,
            .background = rule.background,
            .bold = rule.bold,
            .underline = rule.underline,
        });
    }

    return spans;
}

QList<HighlightRule> HighlightEngine::defaultRules() {
    return {
        HighlightRule{
            .id = QStringLiteral("error"),
            .displayName = QStringLiteral("Errors"),
            .pattern = QRegularExpression(QStringLiteral(R"(\b(ERROR|FAILED|FATAL)\b)"),
                                          QRegularExpression::CaseInsensitiveOption),
            .foreground = QColor(QStringLiteral("#f26d78")),
            .background = QColor(Qt::transparent),
            .bold = true,
            .priority = 100,
        },
        HighlightRule{
            .id = QStringLiteral("warning"),
            .displayName = QStringLiteral("Warnings"),
            .pattern = QRegularExpression(QStringLiteral(R"(\b(WARN|WARNING)\b)"),
                                          QRegularExpression::CaseInsensitiveOption),
            .foreground = QColor(QStringLiteral("#e5b567")),
            .background = QColor(Qt::transparent),
            .bold = true,
            .priority = 90,
        },
        HighlightRule{
            .id = QStringLiteral("ipv4"),
            .displayName = QStringLiteral("IPv4 addresses"),
            .pattern = QRegularExpression(
                QStringLiteral(R"(\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b)")),
            .foreground = QColor(QStringLiteral("#70a5eb")),
            .background = QColor(Qt::transparent),
            .underline = true,
            .priority = 50,
        },
        HighlightRule{
            .id = QStringLiteral("success"),
            .displayName = QStringLiteral("Success states"),
            .pattern = QRegularExpression(QStringLiteral(R"(\b(active|running|success)\b)"),
                                          QRegularExpression::CaseInsensitiveOption),
            .foreground = QColor(QStringLiteral("#7bd88f")),
            .background = QColor(Qt::transparent),
            .bold = true,
            .priority = 40,
        },
    };
}

} // namespace bitshell::core
