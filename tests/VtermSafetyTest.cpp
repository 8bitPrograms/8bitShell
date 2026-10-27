#include <QtTest>
#include <vterm.h>
#include <cstdlib>
#include <memory>
class VtermSafetyTest : public QObject {
    Q_OBJECT
private slots:
    void reflowKeepsFirstVisibleContinuationWithinBuffer() {
        // Prefix allocations with readable zeroes so an erroneous row -1 read
        // is a deterministic content failure, not a platform-dependent crash.
        VTermAllocatorFunctions allocator{
            [](size_t size, void*) -> void* {
                auto* base = static_cast<unsigned char*>(std::calloc(1, size + 4096));
                return base ? base + 4096 : nullptr;
            },
            [](void* pointer, void*) {
                if (pointer) std::free(static_cast<unsigned char*>(pointer) - 4096);
            }
        };
        std::unique_ptr<VTerm, decltype(&vterm_free)> vt(vterm_new_with_allocator(3, 5, &allocator, nullptr), vterm_free);
        QVERIFY(vt);
        auto* screen = vterm_obtain_screen(vt.get());
        vterm_screen_enable_altscreen(screen, 1);
        vterm_screen_enable_reflow(screen, true);
        vterm_screen_reset(screen, 1);
        const QByteArray text("abcdefghijklmnop");
        QCOMPARE(vterm_input_write(vt.get(), text.constData(), size_t(text.size())), size_t(text.size()));
        QVERIFY(vterm_state_get_lineinfo(vterm_obtain_state(vt.get()), 0)->continuation);
        VTermScreenCell cell{};
        QVERIFY(vterm_screen_get_cell(screen, {0, 0}, &cell));
        QCOMPARE(cell.chars[0], uint32_t('f'));
        vterm_set_size(vt.get(), 4, 5);
        QVERIFY(vterm_screen_get_cell(screen, {0, 0}, &cell));
        QCOMPARE(cell.chars[0], uint32_t('f'));
        VTermPos cursor{};
        vterm_state_get_cursorpos(vterm_obtain_state(vt.get()), &cursor);
        QCOMPARE(cursor.row, 2);
        QCOMPARE(cursor.col, 1);
    }
    void hostileChunkedControlSequences() {
        const QList<QByteArray> sequences{
            "\x1b[" + QByteArray("1;").repeated(5000) + "m",
            "\x1b[" + QByteArray("1:").repeated(5000) + "m",
            "\x1b[" + QByteArray(10000,'9') + "H",
            "\x1b]" + QByteArray(10000,'9') + ";title\x07",
            QByteArray("\x1b]12\0;title\x07", 12),
            QByteArray("\x1bP\x00\x01\x1b\\", 6),
            "\x1b[2147483647;2147483647H\x1b[8;50000;200t",
            "\x1b[38:2:255:100:0mhello\x1b[0m"
        };
        for (const auto& sequence : sequences) {
            for (const int chunk : {1, 7, 4096}) {
                std::unique_ptr<VTerm, decltype(&vterm_free)> vt(vterm_new(24,80), vterm_free); QVERIFY(vt);
                vterm_set_utf8(vt.get(), 1);
                auto* screen = vterm_obtain_screen(vt.get()); vterm_screen_reset(screen, 1);
                for (qsizetype at=0; at<sequence.size(); at+=chunk) {
                    const auto part=sequence.mid(at,chunk);
                    QCOMPARE(vterm_input_write(vt.get(),part.constData(),size_t(part.size())),size_t(part.size()));
                }
                const QByteArray recovery("\x18\x1b[0m\x1b[HOK");
                vterm_input_write(vt.get(),recovery.constData(),size_t(recovery.size()));
                VTermScreenCell cell{};
                QVERIFY(vterm_screen_get_cell(screen,{0,0},&cell)); QCOMPARE(cell.chars[0],uint32_t('O'));
                int rows=0,columns=0; vterm_get_size(vt.get(),&rows,&columns);
                QCOMPARE(rows,24); QCOMPARE(columns,80);
            }
        }
    }
};
QTEST_APPLESS_MAIN(VtermSafetyTest)
#include "VtermSafetyTest.moc"
