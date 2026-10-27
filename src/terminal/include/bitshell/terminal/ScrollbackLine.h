#pragma once

#include <vterm.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bitshell::terminal {

// Lossless in-memory history. Glyphs retain their width and all combining
// codepoints; adjacent cells share attributes/colors. No terminal bytes are
// interpreted here, and nothing is serialized to disk.
class ScrollbackLine final {
public:
    ScrollbackLine(int columns, const VTermScreenCell* cells)
        : m_columns(std::max(0, columns)) {
        std::size_t glyphBytes = 0, runs = 0;
        for (int column = 0; column < m_columns; ++column) {
            glyphBytes += 2 + 4 * characterCount(cells[column]);
            if (column == 0 || !sameStyle(cells[column - 1], cells[column])) ++runs;
        }
        m_glyphs.reserve(glyphBytes);
        m_styles.reserve(runs);
        for (int column = 0; column < m_columns; ++column) {
            const auto& cell = cells[column];
            if (column == 0 || !sameStyle(cells[column - 1], cell))
                m_styles.push_back({column + 1, cell.attrs, cell.fg, cell.bg});
            else m_styles.back().endColumn = column + 1;
            m_glyphs.push_back(static_cast<std::uint8_t>(cell.width));
            const auto count = characterCount(cell);
            m_glyphs.push_back(static_cast<std::uint8_t>(count));
            for (std::size_t character = 0; character < count; ++character) {
                const auto codepoint = cell.chars[character];
                for (unsigned shift = 0; shift < 32; shift += 8)
                    m_glyphs.push_back(static_cast<std::uint8_t>(codepoint >> shift));
            }
        }
    }

    [[nodiscard]] std::size_t memoryBytes() const {
        // Charge capacities, the row object, and conservative allocation/deque
        // overhead. The process allocator and the deque's map remain shared
        // overhead, not a claim about exact resident process memory.
        return sizeof(*this) + m_glyphs.capacity()
            + m_styles.capacity() * sizeof(StyleRun) + 64;
    }

    [[nodiscard]] std::vector<VTermScreenCell> decoded() const {
        std::vector<VTermScreenCell> cells(static_cast<std::size_t>(m_columns));
        std::size_t offset = 0, run = 0;
        for (int column = 0; column < m_columns; ++column) {
            while (column >= m_styles[run].endColumn) ++run;
            auto& cell = cells[static_cast<std::size_t>(column)];
            cell.attrs = m_styles[run].attrs;
            cell.fg = m_styles[run].fg;
            cell.bg = m_styles[run].bg;
            std::memcpy(&cell.width, &m_glyphs[offset++], 1);
            const auto count = m_glyphs[offset++];
            for (unsigned character = 0; character < count; ++character) {
                std::uint32_t codepoint = 0;
                for (unsigned shift = 0; shift < 32; shift += 8)
                    codepoint |= std::uint32_t(m_glyphs[offset++]) << shift;
                cell.chars[character] = codepoint;
            }
        }
        return cells;
    }

private:
    struct StyleRun {
        int endColumn;
        VTermScreenCellAttrs attrs;
        VTermColor fg, bg;
    };

    static std::size_t characterCount(const VTermScreenCell& cell) {
        std::size_t count = 0;
        while (count < VTERM_MAX_CHARS_PER_CELL && cell.chars[count] != 0) ++count;
        return count;
    }
    static bool sameColor(const VTermColor& left, const VTermColor& right) {
        if (left.type != right.type) return false;
        if (VTERM_COLOR_IS_INDEXED(&left)) return left.indexed.idx == right.indexed.idx;
        return left.rgb.red == right.rgb.red && left.rgb.green == right.rgb.green
            && left.rgb.blue == right.rgb.blue;
    }
    static bool sameStyle(const VTermScreenCell& left, const VTermScreenCell& right) {
        const auto& a = left.attrs;
        const auto& b = right.attrs;
        return a.bold == b.bold && a.underline == b.underline && a.italic == b.italic
            && a.blink == b.blink && a.reverse == b.reverse && a.conceal == b.conceal
            && a.strike == b.strike && a.font == b.font && a.dwl == b.dwl
            && a.dhl == b.dhl && a.small == b.small && a.baseline == b.baseline
            && sameColor(left.fg, right.fg) && sameColor(left.bg, right.bg);
    }

    int m_columns;
    std::vector<std::uint8_t> m_glyphs;
    std::vector<StyleRun> m_styles;
};

inline constexpr std::size_t scrollbackMemoryBudget = 1024 * 1024;

} // namespace bitshell::terminal
