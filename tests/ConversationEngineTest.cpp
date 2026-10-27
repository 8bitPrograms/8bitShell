#include <QtTest>

#include <bitshell/core/ConversationEngine.h>

using namespace bitshell::core;

class ConversationEngineTest : public QObject {
    Q_OBJECT

private slots:
    void matchesPromptAcrossChunks();
    void advancesThroughOrderedConversation();
    void resetStartsFromFirstStep();
};

void ConversationEngineTest::matchesPromptAcrossChunks() {
    ConversationEngine engine({ConversationStep{
        .id = QStringLiteral("password"),
        .expect = QRegularExpression(QStringLiteral("Password:\\s*$"),
                                     QRegularExpression::CaseInsensitiveOption),
        .action = ConversationActionType::AskSecret,
        .value = QStringLiteral("CyberArk password"),
    }});

    QVERIFY(!engine.feed(QStringLiteral("Pass")).has_value());
    const auto action = engine.feed(QStringLiteral("word: "));

    QVERIFY(action.has_value());
    QCOMPARE(action->stepId, QStringLiteral("password"));
    QCOMPARE(action->type, ConversationActionType::AskSecret);
    QVERIFY(action->sensitive);
    QVERIFY(engine.isComplete());
}

void ConversationEngineTest::advancesThroughOrderedConversation() {
    ConversationEngine engine({
        ConversationStep{
            .id = QStringLiteral("reason"),
            .expect = QRegularExpression(QStringLiteral("Reason:"),
                                         QRegularExpression::CaseInsensitiveOption),
            .action = ConversationActionType::Ask,
            .value = QStringLiteral("Connection reason"),
        },
        ConversationStep{
            .id = QStringLiteral("account"),
            .expect = QRegularExpression(QStringLiteral("Select account:"),
                                         QRegularExpression::CaseInsensitiveOption),
            .action = ConversationActionType::Choose,
            .choices = {QStringLiteral("sys"), QStringLiteral("opc")},
        },
    });

    const auto reason = engine.feed(QStringLiteral("Banner\nReason:"));
    QVERIFY(reason.has_value());
    QCOMPARE(reason->type, ConversationActionType::Ask);
    QCOMPARE(engine.currentStepId(), QStringLiteral("account"));

    const auto account = engine.feed(QStringLiteral("\nSelect account:"));
    QVERIFY(account.has_value());
    QCOMPARE(account->type, ConversationActionType::Choose);
    QCOMPARE(account->choices.size(), 2);
    QVERIFY(engine.isComplete());
}

void ConversationEngineTest::resetStartsFromFirstStep() {
    ConversationEngine engine({ConversationStep{
        .id = QStringLiteral("ready"),
        .expect = QRegularExpression(QStringLiteral("ready")),
        .action = ConversationActionType::Notify,
    }});

    QVERIFY(engine.feed(QStringLiteral("ready")).has_value());
    QVERIFY(engine.isComplete());
    engine.reset();
    QVERIFY(!engine.isComplete());
    QCOMPARE(engine.currentStepIndex(), qsizetype{0});
}

QTEST_APPLESS_MAIN(ConversationEngineTest)
#include "ConversationEngineTest.moc"

