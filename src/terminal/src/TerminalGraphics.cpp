#include <bitshell/terminal/TerminalGraphics.h>

#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <algorithm>
#include <limits>
#include <utility>

namespace bitshell::terminal {
namespace {
int number(const QHash<char, QByteArray>& keys, char key, int fallback = 0) {
    if (!keys.contains(key)) return fallback;
    bool ok = false;
    const int result = keys.value(key).toInt(&ok);
    return ok ? result : -1;
}
char letter(const QHash<char, QByteArray>& keys, char key, char fallback) {
    const auto value = keys.value(key);
    return value.size() == 1 ? value.front() : fallback;
}
quint32 identifier(const QHash<char, QByteArray>& keys, char key, bool* valid = nullptr) {
    if (!keys.contains(key)) { if (valid) *valid = true; return 0; }
    bool ok = false;
    const auto value = keys.value(key).toULongLong(&ok);
    ok = ok && value <= std::numeric_limits<quint32>::max();
    if (valid) *valid = ok;
    return ok ? quint32(value) : 0;
}
}

qsizetype TerminalGraphics::imageBytes() const {
    qsizetype bytes = 0;
    for (const auto& image : m_images) bytes += image.sizeInBytes();
    return bytes;
}
void TerminalGraphics::clearPlacements() {
    m_changed |= !m_placements.isEmpty();
    m_placements.clear();
}
void TerminalGraphics::clear() {
    clearPlacements();
    m_images.clear();
    m_payload.clear();
    m_transferKeys.clear();
    m_transferring = false;
}

QByteArray TerminalGraphics::command(const QByteArray& bytes, QPoint cursor, QSize cellSize) {
    if (!bytes.startsWith('G') || bytes.size() > PayloadLimit) return {};
    const auto separator = bytes.indexOf(';');
    const auto control = bytes.mid(1, separator < 0 ? -1 : separator - 1);
    QHash<char, QByteArray> keys;
    if (control.size() > 1024) { clear(); return {}; }
    for (const auto& field : control.split(',')) {
        if (field.isEmpty()) continue;
        if (field.size() < 3 || field[1] != '=' || keys.contains(field[0])) return {};
        keys.insert(field[0], field.mid(2));
    }
    if (m_transferring) {
        // Continuations carry only m and q. An unrelated new command cancels
        // an unfinished transfer instead of inheriting its image metadata.
        bool continuation = true;
        for (auto it = keys.cbegin(); it != keys.cend(); ++it)
            if (it.key() != 'm' && it.key() != 'q') continuation = false;
        if (continuation) {
            auto combined = m_transferKeys;
            for (auto it = keys.cbegin(); it != keys.cend(); ++it) combined.insert(it.key(), it.value());
            keys = combined;
        } else { m_payload.clear(); m_transferKeys.clear(); m_transferring = false; }
    }
    bool validId = false, validPlacement = false;
    const quint32 requestedId = identifier(keys, 'i', &validId);
    const quint32 placementId = identifier(keys, 'p', &validPlacement);
    if (!validId || !validPlacement) return {};
    const char action = letter(keys, 'a', 't');
    const auto reply = [&](const QByteArray& message) -> QByteArray {
        const int quiet = number(keys, 'q');
        if (quiet == 2 || (quiet == 1 && message == "OK") || requestedId <= 0) return {};
        QByteArray result("\x1b_Gi=");
        result += QByteArray::number(requestedId);
        if (keys.contains('p')) result += ",p=" + QByteArray::number(placementId);
        return result + ';' + message + "\x1b\\";
    };
    if (number(keys, 'q') < 0 || number(keys, 'q') > 2) return {};
    if (action == 'd') {
        const char mode = letter(keys, 'd', 'a');
        if (mode == 'a' || mode == 'A') {
            clearPlacements();
            if (mode == 'A') m_images.clear();
        } else if (mode == 'i' || mode == 'I') {
            const quint32 placement = placementId;
            m_placements.removeIf([&](const Placement& p) {
                return p.image == quint32(requestedId) && (!placement || p.id == quint32(placement));
            });
            if (mode == 'I' && !placement) m_images.remove(quint32(requestedId));
            m_changed = true;
        } else return reply("EINVAL:unsupported delete selector");
        return reply("OK");
    }
    if ((action == 'p' || action == 'T') && (number(keys, 'C') != 1 || number(keys, 'z') < 0))
        return reply("ENOTSUP:use cursor-preserving placement with a nonnegative layer");
    if (action == 'p') return reply(place(keys, quint32(requestedId), cursor, cellSize)
        ? QByteArray("OK") : QByteArray("ENOENT:invalid image or placement"));
    if (action != 't' && action != 'T' && action != 'q') return reply("EINVAL:unsupported action");
    if (letter(keys, 't', 'd') != 'd' || keys.contains('o') || number(keys, 'U') != 0)
        return reply("ENOTSUP:only inline uncompressed images are supported");
    const int more = number(keys, 'm');
    if (more != 0 && more != 1) return reply("EINVAL:invalid chunk flag");
    const auto payload = separator < 0 ? QByteArray{} : bytes.mid(separator + 1);
    if (m_payload.size() + payload.size() > PayloadLimit) {
        m_payload.clear(); m_transferKeys.clear(); m_transferring = false;
        return reply("E2BIG:image payload exceeds limit");
    }
    m_payload += payload;
    if (more) { m_transferring = true; m_transferKeys = keys; return {}; }
    m_transferring = false;
    m_transferKeys.clear();
    const auto decoded = QByteArray::fromBase64Encoding(m_payload, QByteArray::AbortOnBase64DecodingErrors);
    m_payload.clear();
    if (!decoded) return reply("EINVAL:invalid base64");
    const int format = number(keys, 'f', 32);
    QImage image;
    if (format == 100) {
        QBuffer buffer;
        buffer.setData(decoded.decoded);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer, "PNG");
        const QSize size = reader.size();
        if (size.width() < 1 || size.height() < 1 || size.width() > DimensionLimit || size.height() > DimensionLimit)
            return reply("E2BIG:invalid image dimensions");
        image = reader.read();
    } else if (format == 24 || format == 32) {
        const int width = number(keys, 's'), height = number(keys, 'v');
        if (width < 1 || height < 1 || width > DimensionLimit || height > DimensionLimit)
            return reply("E2BIG:invalid image dimensions");
        const int stride = width * (format / 8);
        if (decoded.decoded.size() != qsizetype(stride) * height) return reply("EINVAL:invalid pixel count");
        image = QImage(reinterpret_cast<const uchar*>(decoded.decoded.constData()), width, height, stride,
            format == 24 ? QImage::Format_RGB888 : QImage::Format_RGBA8888).copy();
    } else return reply("ENOTSUP:unsupported pixel format");
    if (image.isNull()) return reply("EINVAL:invalid image");
    if (action == 'q') return reply("OK");
    quint32 id = quint32(requestedId);
    if (!id) {
        while (m_images.contains(m_nextId) || !m_nextId) ++m_nextId;
        id = m_nextId++;
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const qsizetype previous = m_images.value(id).sizeInBytes();
    if (imageBytes() - previous + image.sizeInBytes() > ImageBudget || (!m_images.contains(id) && m_images.size() >= 64))
        return reply("ENOSPC:image storage limit reached");
    m_images.insert(id, image);
    if (action == 'T' && !place(keys, id, cursor, cellSize)) return reply("EINVAL:invalid placement");
    return reply("OK");
}

bool TerminalGraphics::place(const QHash<char, QByteArray>& keys, quint32 id, QPoint cursor, QSize cellSize) {
    const auto image = m_images.value(id);
    if (image.isNull()) return false;
    const int x = number(keys, 'x'), y = number(keys, 'y');
    const int width = number(keys, 'w', image.width() - x), height = number(keys, 'h', image.height() - y);
    if (x < 0 || y < 0 || width < 1 || height < 1 || x > image.width() - width || y > image.height() - height) return false;
    int columns = number(keys, 'c'), rows = number(keys, 'r');
    const int px = number(keys, 'X'), py = number(keys, 'Y');
    bool validPlacement = false;
    const quint32 placement = identifier(keys, 'p', &validPlacement);
    if (columns < 0 || rows < 0 || columns > 1000 || rows > 500 || px < 0 || py < 0
        || px >= cellSize.width() || py >= cellSize.height() || !validPlacement) return false;
    if (!columns && !rows) {
        columns = (width + cellSize.width() - 1) / cellSize.width();
        rows = (height + cellSize.height() - 1) / cellSize.height();
    } else if (!rows) rows = std::max(1, qRound(double(height) * columns * cellSize.width() / (width * cellSize.height())));
    else if (!columns) columns = std::max(1, qRound(double(width) * rows * cellSize.height() / (height * cellSize.width())));
    if (columns > 1000 || rows > 500) return false;
    m_placements.removeIf([&](const Placement& p) { return p.image == id && p.id == quint32(placement); });
    if (m_placements.size() >= 128) return false;
    m_placements.append({id, quint32(placement), cursor, {columns, rows}, {x, y, width, height}, {px, py}, number(keys, 'z')});
    std::stable_sort(m_placements.begin(), m_placements.end(), [](const Placement& a, const Placement& b) { return a.z < b.z; });
    m_changed = true;
    return true;
}

void TerminalGraphics::scroll(int top, int bottom, int rows) {
    if (!rows) return;
    for (qsizetype index = m_placements.size(); index-- > 0;) {
        auto& p = m_placements[index];
        if (p.cell.y() >= top && p.cell.y() < bottom) {
            p.cell.ry() += rows;
            m_changed = true;
            if (p.cell.y() + p.cells.height() <= top || p.cell.y() >= bottom)
                m_placements.removeAt(index);
        }
    }
}
void TerminalGraphics::paint(QPainter* painter, QPointF origin, QSizeF cellSize, int scrollOffset) const {
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (const auto& p : m_placements) {
        const QRectF target(origin.x() + p.cell.x() * cellSize.width() + p.offset.x(),
            origin.y() + (p.cell.y() + scrollOffset) * cellSize.height() + p.offset.y(),
            p.cells.width() * cellSize.width(), p.cells.height() * cellSize.height());
        painter->drawImage(target, m_images.value(p.image), p.source);
    }
    painter->restore();
}
} // namespace bitshell::terminal
