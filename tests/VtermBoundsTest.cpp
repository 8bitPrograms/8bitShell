#include <QtTest>
#include <vterm.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <memory>
#include <vector>

namespace {
// Mirrors TerminalView::initializeTerminal: altscreen, reflow, scroll damage
// merging and a real scrollback store, flushing damage after every write.
class Terminal {
public:
    Terminal(int rows, int columns) : m_vt(vterm_new(rows, columns), vterm_free) {
        if (!m_vt) return;
        vterm_set_utf8(m_vt.get(), 1);
        m_screen = vterm_obtain_screen(m_vt.get());
        m_callbacks.damage = [](VTermRect, void*) { return 1; };
        m_callbacks.sb_pushline = [](int cols, const VTermScreenCell* cells, void* user) {
            static_cast<Terminal*>(user)->m_scrollback.emplace_back(cells, cells + cols);
            return 1;
        };
        m_callbacks.sb_popline = [](int cols, VTermScreenCell* cells, void* user) {
            auto* terminal = static_cast<Terminal*>(user);
            if (terminal->m_scrollback.empty()) return 0;
            const auto line = std::move(terminal->m_scrollback.back());
            terminal->m_scrollback.pop_back();
            const int available = std::min(cols, static_cast<int>(line.size()));
            if (available > 0)
                std::memcpy(cells, line.data(), static_cast<std::size_t>(available) * sizeof(VTermScreenCell));
            if (available < cols) {
                VTermScreenCell blank{};
                blank.width = 1;
                vterm_state_get_default_colors(vterm_obtain_state(terminal->m_vt.get()), &blank.fg, &blank.bg);
                std::fill(cells + available, cells + cols, blank);
            }
            return 1;
        };
        vterm_screen_set_callbacks(m_screen, &m_callbacks, this);
        vterm_screen_enable_altscreen(m_screen, 1);
        vterm_screen_enable_reflow(m_screen, true);
        vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);
        vterm_screen_reset(m_screen, 1);
    }
    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    bool valid() const { return m_vt != nullptr; }
    VTerm* vt() const { return m_vt.get(); }
    size_t write(const QByteArray& bytes) {
        const size_t written = vterm_input_write(m_vt.get(), bytes.constData(), size_t(bytes.size()));
        vterm_screen_flush_damage(m_screen);
        return written;
    }
    void resize(int rows, int columns) {
        vterm_set_size(m_vt.get(), rows, columns);
        vterm_screen_flush_damage(m_screen);
    }
    uint32_t charAt(int row, int column) const {
        VTermScreenCell cell{};
        return vterm_screen_get_cell(m_screen, {row, column}, &cell) ? cell.chars[0] : 0;
    }

private:
    std::deque<std::vector<VTermScreenCell>> m_scrollback;
    VTermScreenCallbacks m_callbacks{};
    VTermScreen* m_screen = nullptr;
    std::unique_ptr<VTerm, decltype(&vterm_free)> m_vt;
};
}

class VtermBoundsTest : public QObject {
    Q_OBJECT
private slots:
    void repWideGlyphAtRowEnd() {
        // Repeating a wide glyph must keep its continuation cells inside the row.
        Terminal terminal(24, 80);
        QVERIFY(terminal.valid());
        terminal.write("]" "\xF0\x9F\x98\x80" "\x1b[200b");
        terminal.write("\x1b[H");
        terminal.write("OK");
        QCOMPARE(terminal.charAt(0, 0), uint32_t('O'));
        int rows = 0, columns = 0;
        vterm_get_size(terminal.vt(), &rows, &columns);
        QCOMPARE(rows, 24);
        QCOMPARE(columns, 80);
    }
    void repBeforeAnyGlyph() {
        // REP with no preceding graphic character leaves the cursor in place.
        Terminal terminal(24, 80);
        QVERIFY(terminal.valid());
        terminal.write("\x1b[b");
        terminal.write("X\x1b[4b");
        for (int column = 0; column < 5; ++column)
            QCOMPARE(terminal.charAt(0, column), uint32_t('X'));
    }
    void resizeAfterAlignmentPattern() {
        // The cursor stays inside the screen across reflowing resizes.
        Terminal terminal(48, 18);
        QVERIFY(terminal.valid());
        terminal.write("\x1b#8");
        terminal.resize(3, 141);
        terminal.write("\x1b#8");
        terminal.resize(45, 17);
        terminal.write("\x1b[r6152QK\xe2" "h#40795T914[2004h3129001h$777;8bitshell;x;bash555d999999999999");
        terminal.resize(19, 77);
        VTermPos cursor{};
        vterm_state_get_cursorpos(vterm_obtain_state(terminal.vt()), &cursor);
        QVERIFY(cursor.row >= 0 && cursor.row < 19);
        QVERIFY(cursor.col >= 0 && cursor.col < 77);
        terminal.write("\x1b[HOK");
        QCOMPARE(terminal.charAt(0, 0), uint32_t('O'));
    }
    void eraseAfterZeroWidthControl() {
        // Erasing characters after a C1 control leaves the screen usable.
        Terminal terminal(24, 80);
        QVERIFY(terminal.valid());
        terminal.write("\xc2\x83" "\x1b[X");
        terminal.write("\x1b[HOK");
        QCOMPARE(terminal.charAt(0, 0), uint32_t('O'));
        QCOMPARE(terminal.charAt(0, 1), uint32_t('K'));
        int rows = 0, columns = 0;
        vterm_get_size(terminal.vt(), &rows, &columns);
        QCOMPARE(rows, 24);
        QCOMPARE(columns, 80);
    }
};
QTEST_APPLESS_MAIN(VtermBoundsTest)
#include "VtermBoundsTest.moc"
