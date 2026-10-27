#include <bitshell/terminal/ScrollbackLine.h>
#include <QtTest>
#include <QRandomGenerator>
#include <array>

using bitshell::terminal::ScrollbackLine;

class ScrollbackLineTest final : public QObject {
    Q_OBJECT
private slots:
    void emptyAndUniformRows();
    void styledUnicodeAndWideCells();
    void randomizedRoundTrips();
};

static void compareCells(const VTermScreenCell& actual, const VTermScreenCell& expected) {
    QCOMPARE(actual.width, expected.width);
    for (int index = 0; index < VTERM_MAX_CHARS_PER_CELL; ++index)
        QCOMPARE(actual.chars[index], expected.chars[index]);
    QCOMPARE(int(actual.attrs.bold), int(expected.attrs.bold));
    QCOMPARE(int(actual.attrs.underline), int(expected.attrs.underline));
    QCOMPARE(int(actual.attrs.italic), int(expected.attrs.italic));
    QCOMPARE(int(actual.attrs.blink), int(expected.attrs.blink));
    QCOMPARE(int(actual.attrs.reverse), int(expected.attrs.reverse));
    QCOMPARE(int(actual.attrs.conceal), int(expected.attrs.conceal));
    QCOMPARE(int(actual.attrs.strike), int(expected.attrs.strike));
    QCOMPARE(int(actual.attrs.font), int(expected.attrs.font));
    QCOMPARE(int(actual.attrs.dwl), int(expected.attrs.dwl));
    QCOMPARE(int(actual.attrs.dhl), int(expected.attrs.dhl));
    QCOMPARE(int(actual.attrs.small), int(expected.attrs.small));
    QCOMPARE(int(actual.attrs.baseline), int(expected.attrs.baseline));
    QCOMPARE(actual.fg.type, expected.fg.type);
    QCOMPARE(actual.bg.type, expected.bg.type);
    QVERIFY(vterm_color_is_equal(&actual.fg, &expected.fg));
    QVERIFY(vterm_color_is_equal(&actual.bg, &expected.bg));
}

void ScrollbackLineTest::emptyAndUniformRows() {
    QVERIFY(ScrollbackLine(0, nullptr).decoded().empty());
    std::vector<VTermScreenCell> cells(120);
    for (auto& cell : cells) { cell.chars[0] = 'A'; cell.width = 1; }
    ScrollbackLine row(int(cells.size()), cells.data());
    QVERIFY(row.memoryBytes() < cells.size() * sizeof(VTermScreenCell) / 3);
    const auto decoded = row.decoded();
    QCOMPARE(decoded.size(), cells.size());
    for (std::size_t index = 0; index < cells.size(); ++index) compareCells(decoded[index], cells[index]);
}

void ScrollbackLineTest::styledUnicodeAndWideCells() {
    std::array<VTermScreenCell, 8> cells{};
    for (auto& cell : cells) {
        cell.width = 1;
        vterm_color_rgb(&cell.fg, 10, 120, 240);
        vterm_color_indexed(&cell.bg, 3);
        cell.fg.type |= VTERM_COLOR_DEFAULT_FG;
    }
    cells[0].chars[0] = 'e'; cells[0].chars[1] = 0x301;
    cells[1].chars[0] = 0x1f642; cells[1].width = 2;
    cells[2].chars[0] = 0xffffffffU; cells[2].width = 0;
    cells[3].chars[0] = 0x0130; cells[3].attrs.underline = 2;
    cells[4].attrs.bold = 1; cells[4].attrs.reverse = 1;
    cells[5].fg.type &= ~VTERM_COLOR_DEFAULT_FG;
    cells[6].attrs.dwl = 1; cells[6].attrs.dhl = 2;
    for (int index = 0; index < VTERM_MAX_CHARS_PER_CELL; ++index)
        cells[7].chars[index] = index == 0 ? 'a' : 0x300 + index;
    const auto decoded = ScrollbackLine(int(cells.size()), cells.data()).decoded();
    for (std::size_t index = 0; index < cells.size(); ++index) compareCells(decoded[index], cells[index]);
}

void ScrollbackLineTest::randomizedRoundTrips() {
    QRandomGenerator random(42);
    for (int iteration = 0; iteration < 100; ++iteration) {
        std::vector<VTermScreenCell> cells(random.bounded(1, 1001));
        for (auto& cell : cells) {
            cell.width = char(random.bounded(3));
            const auto count = random.bounded(VTERM_MAX_CHARS_PER_CELL + 1);
            for (int index = 0; index < count; ++index) cell.chars[index] = random.generate() | 1U;
            cell.attrs.bold = random.bounded(2); cell.attrs.underline = random.bounded(4);
            cell.attrs.italic = random.bounded(2); cell.attrs.blink = random.bounded(2);
            cell.attrs.reverse = random.bounded(2); cell.attrs.conceal = random.bounded(2);
            cell.attrs.strike = random.bounded(2); cell.attrs.font = random.bounded(10);
            cell.attrs.dwl = random.bounded(2); cell.attrs.dhl = random.bounded(3);
            cell.attrs.small = random.bounded(2); cell.attrs.baseline = random.bounded(4);
            if (random.bounded(2)) vterm_color_rgb(&cell.fg, random.bounded(256), random.bounded(256), random.bounded(256));
            else vterm_color_indexed(&cell.fg, random.bounded(256));
            vterm_color_indexed(&cell.bg, random.bounded(256));
        }
        const auto decoded = ScrollbackLine(int(cells.size()), cells.data()).decoded();
        QCOMPARE(decoded.size(), cells.size());
        for (std::size_t index = 0; index < cells.size(); ++index) compareCells(decoded[index], cells[index]);
    }
}

QTEST_APPLESS_MAIN(ScrollbackLineTest)
#include "ScrollbackLineTest.moc"
