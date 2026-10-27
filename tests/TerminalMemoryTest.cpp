#include "TerminalTestAccess.h"
#include <QtTest>

using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;
using bitshell::terminal::scrollbackMemoryBudget;

class TerminalMemoryTest final : public QObject {
    Q_OBJECT
private slots:
    void historyStaysWithinBudgetAndKeepsNewestLines();
    void resizeRestoresUnicodeAndWidthPadding();
};

void TerminalMemoryTest::historyStaysWithinBudgetAndKeepsNewestLines() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setVisible(false);
    terminal.setWidth(800); terminal.setHeight(450);
    terminal.setScrollbackLimit(100000);
    for (int index = 0; index < 10000; ++index)
        TerminalTestAccess::feed(terminal, QStringLiteral("row-%1: plain text and styled \x1b[31mERROR\x1b[0m e\u0301 \U0001f642\r\n").arg(index).toUtf8());
    QVERIFY(TerminalTestAccess::historyBytes(terminal) <= scrollbackMemoryBudget);
    QVERIFY(terminal.scrollbackLines() > 500);
    QVERIFY(terminal.scrollbackLines() < 10000);
    terminal.setSearchText(QStringLiteral("row-9999:"));
    QCOMPARE(terminal.searchMatchCount(), 1);
    terminal.setSearchText(QStringLiteral("row-0:"));
    QCOMPARE(terminal.searchMatchCount(), 0);
    terminal.setScrollbackLimit(100);
    QCOMPARE(terminal.scrollbackLines(), 100);
    QVERIFY(TerminalTestAccess::historyBytes(terminal) <= scrollbackMemoryBudget);
    terminal.clearScrollback();
    QCOMPARE(TerminalTestAccess::historyBytes(terminal), std::size_t(0));
    QCOMPARE(terminal.scrollbackLines(), 0);
}

void TerminalMemoryTest::resizeRestoresUnicodeAndWidthPadding() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setWidth(320); terminal.setHeight(180);
    for (int index = 0; index < 40; ++index)
        TerminalTestAccess::feed(terminal, QStringLiteral("\x1b[1;31mI\u0130 e\u0301 \U0001f642\x1b[0m\r\n").toUtf8());
    auto cells = TerminalTestAccess::popScrollbackRow(terminal, 200);
    QCOMPARE(cells.size(), std::size_t(200));
    bool combining = false, wide = false;
    for (const auto& cell : cells) {
        combining |= cell.chars[0] == 'e' && cell.chars[1] == 0x301;
        wide |= cell.chars[0] == 0x1f642 && cell.width == 2;
    }
    QVERIFY(combining && wide);
    QCOMPARE(cells.back().width, char(1));
    QCOMPARE(cells.back().chars[0], uint32_t(0));
}

QTEST_MAIN(TerminalMemoryTest)
#include "TerminalMemoryTest.moc"
