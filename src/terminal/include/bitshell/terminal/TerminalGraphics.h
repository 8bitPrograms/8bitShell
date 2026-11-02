#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QList>
#include <QPoint>
#include <QSize>
#include <utility>

class QPainter;

namespace bitshell::terminal {
// The inline PNG/RGB subset of the Kitty graphics protocol. No paths, shared
// memory, subprocesses or external image codecs are consulted. Storage is lazy
// and bounded independently of text scrollback.
class TerminalGraphics {
public:
    static constexpr qsizetype PayloadLimit = 8 * 1024 * 1024;
    static constexpr qsizetype ImageBudget = 16 * 1024 * 1024;
    static constexpr int DimensionLimit = 2048;
    QByteArray command(const QByteArray& bytes, QPoint cursor, QSize cellSize);
    void clear();
    void clearPlacements();
    void scroll(int top, int bottom, int rows);
    void paint(QPainter* painter, QPointF origin, QSizeF cellSize, int scrollOffset) const;
    qsizetype imageBytes() const;
    int placementCount() const { return int(m_placements.size()); }
    bool takeChanged() { return std::exchange(m_changed, false); }
private:
    struct Placement {
        quint32 image = 0, id = 0;
        QPoint cell;
        QSize cells;
        QRect source;
        QPoint offset;
        int z = 0;
    };
    bool place(const QHash<char, QByteArray>& keys, quint32 id, QPoint cursor, QSize cellSize);
    QHash<quint32, QImage> m_images;
    QList<Placement> m_placements;
    QHash<char, QByteArray> m_transferKeys;
    QByteArray m_payload;
    bool m_transferring = false, m_changed = false;
    quint32 m_nextId = 1;
};
} // namespace bitshell::terminal
