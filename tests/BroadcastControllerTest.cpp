#include <QtTest>
#include <bitshell/terminal/BroadcastController.h>
#include "TerminalTestAccess.h"
using namespace bitshell::terminal;

class BroadcastControllerTest : public QObject {
    Q_OBJECT
    static QVariant object(TerminalView& terminal) { return QVariant::fromValue(static_cast<QObject*>(&terminal)); }
    static QByteArray output(const QSignalSpy& spy) { QByteArray bytes; for (const auto& sample : spy) bytes += sample[0].toByteArray(); return bytes; }
private slots:
    void onlyExplicitReviewedTargetsReceiveBytes() {
        TerminalView one, two, local;
        QSignalSpy first(&one, &TerminalView::rawOutputReceived), second(&two, &TerminalView::rawOutputReceived);
        QVERIFY(TerminalTestAccess::startBroadcastFixture(one, QStringLiteral("one")));
        QVERIFY(TerminalTestAccess::startBroadcastFixture(two, QStringLiteral("two")));
        QTRY_VERIFY(output(first).contains("READY")); QTRY_VERIFY(output(second).contains("READY"));
        local.setLogEnabled(false); QVERIFY(local.startLocalShell()); QVERIFY(!local.setBroadcastArmed(true));
        BroadcastController sender;
        QVERIFY(!sender.prepare(QStringLiteral("PAYLOAD"), {}));
        QVERIFY(!sender.prepare(QStringLiteral("PAYLOAD"), QVariantList(65, object(one))));
        QVERIFY(!sender.prepare(QStringLiteral("PAYLOAD"), {object(one)}));
        QVERIFY(one.setBroadcastArmed(true)); QVERIFY(two.setBroadcastArmed(true));
        QVERIFY(!sender.prepare(QStringLiteral("PAYLOAD"), {object(one), object(local)}));
        QVERIFY(!sender.prepare(QStringLiteral("PAYLOAD"), {object(one), object(one)}));
        for (const auto& command : {QString{}, QString(17000, u'a'), QStringLiteral("hello\x1b[31m"), QStringLiteral("hello\u202e")})
            QVERIFY(!sender.prepare(command, {object(one)}));
        QVERIFY(sender.prepare(QStringLiteral("FIRST"), {object(one)}));
        QTest::qWait(50); QVERIFY(!output(first).contains("ACK:FIRST"));
        sender.cancel(); QVERIFY(!sender.commit());
        QVERIFY(sender.prepare(QStringLiteral("FIRST"), {object(one)}));
        QVERIFY(sender.commit()); QVERIFY(!sender.commit());
        QTRY_VERIFY(output(first).contains("ACK:FIRST")); QVERIFY(!output(second).contains("ACK:FIRST"));
        QVERIFY(sender.prepare(QStringLiteral("SECOND"), {object(one), object(two)}));
        QVERIFY(sender.commit());
        QTRY_VERIFY(output(first).contains("ACK:SECOND")); QTRY_VERIFY(output(second).contains("ACK:SECOND"));
        QVERIFY(sender.command().isEmpty()); QVERIFY(sender.targets().isEmpty());
    }
    void changedOrDeletedTargetsBlockTheEntirePreparedSend() {
        TerminalView one; auto two = std::make_unique<TerminalView>();
        QSignalSpy first(&one, &TerminalView::rawOutputReceived);
        QVERIFY(TerminalTestAccess::startBroadcastFixture(one, QStringLiteral("one")));
        QVERIFY(TerminalTestAccess::startBroadcastFixture(*two, QStringLiteral("two")));
        QTRY_VERIFY(output(first).contains("READY"));
        QVERIFY(one.setBroadcastArmed(true)); QVERIFY(two->setBroadcastArmed(true));
        BroadcastController sender;
        QVERIFY(sender.prepare(QStringLiteral("STALE"), {object(one), object(*two)}));
        two.reset(); QVERIFY(!sender.commit());
        QTest::qWait(50); QVERIFY(!output(first).contains("ACK:STALE"));
        QVERIFY(sender.prepare(QStringLiteral("OLD"), {object(one)}));
        QVERIFY(TerminalTestAccess::startBroadcastFixture(one, QStringLiteral("one")));
        QVERIFY(!one.broadcastArmed()); QVERIFY(one.setBroadcastArmed(true));
        QVERIFY(!sender.commit()); QVERIFY(!output(first).contains("ACK:OLD"));
        QVERIFY(sender.prepare(QStringLiteral("CHANGED"), {object(one)}));
        QVERIFY(one.setBroadcastArmed(false)); QVERIFY(one.setBroadcastArmed(true));
        QVERIFY(!sender.commit());
    }
    void normalizedPreviewAndPendingPasteFailClosed() {
        TerminalView one, two;
        QSignalSpy first(&one, &TerminalView::rawOutputReceived), second(&two, &TerminalView::rawOutputReceived);
        QVERIFY(TerminalTestAccess::startBroadcastFixture(one, QStringLiteral("one")));
        QVERIFY(TerminalTestAccess::startBroadcastFixture(two, QStringLiteral("two")));
        QTRY_VERIFY(output(first).contains("READY")); QTRY_VERIFY(output(second).contains("READY"));
        QVERIFY(one.setBroadcastArmed(true)); QVERIFY(two.setBroadcastArmed(true));
        BroadcastController sender;
        QVERIFY(sender.prepare(QStringLiteral("LINE_ONE\r\nLINE_TWO"), {object(one), object(two)}));
        QCOMPARE(sender.command(), QStringLiteral("LINE_ONE\nLINE_TWO"));
        QVERIFY(sender.commit());
        QTRY_VERIFY(output(first).contains("ACK:LINE_ONE")); QTRY_VERIFY(output(first).contains("ACK:LINE_TWO"));
        QTRY_VERIFY(output(second).contains("ACK:LINE_ONE")); QTRY_VERIFY(output(second).contains("ACK:LINE_TWO"));
        QVERIFY(sender.prepare(QStringLiteral("PROMPT_BLOCKED"), {object(one), object(two)}));
        two.pasteText(QStringLiteral("REVIEW_THIS\nBEFORE_PASTING"));
        QVERIFY(!sender.commit()); // A prompt on the second target must also protect the first.
        QVERIFY(!sender.prepare(QStringLiteral("STILL_BLOCKED"), {object(one), object(two)}));
        QTest::qWait(50);
        QVERIFY(!output(first).contains("ACK:PROMPT_BLOCKED")); QVERIFY(!output(second).contains("ACK:PROMPT_BLOCKED"));
        QVERIFY(!output(first).contains("ACK:STILL_BLOCKED"));
        two.confirmPaste(false);
        QVERIFY(sender.prepare(QStringLiteral("READY_AGAIN"), {object(one), object(two)}));
        sender.cancel();
    }
};
QTEST_MAIN(BroadcastControllerTest)
#include "BroadcastControllerTest.moc"
