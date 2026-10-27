#include <QtTest>

#include <bitshell/core/HighlightEngine.h>

using namespace bitshell::core;

class HighlightEngineTest : public QObject {
    Q_OBJECT

private slots:
    void defaultRulesHighlightOperationalOutput();
    void higherPriorityRuleOwnsOverlap();
    void disabledAndInvalidRulesAreIgnored();
};

void HighlightEngineTest::defaultRulesHighlightOperationalOutput() {
    const auto spans = HighlightEngine{}.highlight(
        QStringLiteral("ERROR from 10.20.30.40 but service is active"),
        HighlightEngine::defaultRules());

    QCOMPARE(spans.size(), 3);
    QCOMPARE(spans.at(0).ruleId, QStringLiteral("error"));
    QCOMPARE(spans.at(1).ruleId, QStringLiteral("ipv4"));
    QCOMPARE(spans.at(2).ruleId, QStringLiteral("success"));
    QVERIFY(spans.at(0).bold);
    QVERIFY(spans.at(1).underline);
}

void HighlightEngineTest::higherPriorityRuleOwnsOverlap() {
    const QList<HighlightRule> rules{
        HighlightRule{
            .id = QStringLiteral("word"),
            .pattern = QRegularExpression(QStringLiteral("ERROR")),
            .foreground = QColor(QStringLiteral("#aaaaaa")),
            .priority = 10,
        },
        HighlightRule{
            .id = QStringLiteral("specific"),
            .pattern = QRegularExpression(QStringLiteral("ERR")),
            .foreground = QColor(QStringLiteral("#ff0000")),
            .priority = 100,
        },
    };

    const auto spans = HighlightEngine{}.highlight(QStringLiteral("ERROR"), rules);

    QCOMPARE(spans.size(), 2);
    QCOMPARE(spans.at(0).ruleId, QStringLiteral("specific"));
    QCOMPARE(spans.at(0).length, qsizetype{3});
    QCOMPARE(spans.at(1).ruleId, QStringLiteral("word"));
    QCOMPARE(spans.at(1).length, qsizetype{2});
}

void HighlightEngineTest::disabledAndInvalidRulesAreIgnored() {
    const QList<HighlightRule> rules{
        HighlightRule{
            .id = QStringLiteral("disabled"),
            .pattern = QRegularExpression(QStringLiteral("ERROR")),
            .enabled = false,
        },
        HighlightRule{
            .id = QStringLiteral("invalid"),
            .pattern = QRegularExpression(QStringLiteral("[")),
        },
    };

    QVERIFY(HighlightEngine{}.highlight(QStringLiteral("ERROR"), rules).isEmpty());
}

QTEST_APPLESS_MAIN(HighlightEngineTest)
#include "HighlightEngineTest.moc"

