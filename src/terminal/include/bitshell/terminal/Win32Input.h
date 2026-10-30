#pragma once
#include <QByteArray>
#include <QByteArrayView>
#include <QKeyEvent>
#include <bitshell/core/SecureMemory.h>

#include <utility>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace bitshell::terminal {
// ConPTY can request full Win32 key records (DEC private mode 9001).
// Observe the request without interpreting anything inside OSC/DCS strings.
class Win32Input {
public:
    bool enabled() const { return m_enabled; }
    void reset() { m_state = Ground; m_enabled = false; m_parameters.clear(); }
    void feed(QByteArrayView bytes) {
        for (const char raw : bytes) {
            const auto c = static_cast<unsigned char>(raw);
            switch (m_state) {
            case Ground: if (c == 0x1b) m_state = Escape; break;
            case Escape:
                if (c == '[') { m_state = Csi; m_parameters.clear(); }
                else if (c == ']' || c == 'P' || c == '^' || c == '_') { m_osc = c == ']'; m_state = String; }
                else m_state = c == 0x1b ? Escape : Ground;
                break;
            case Csi:
                if (c >= 0x40 && c <= 0x7e) {
                    if ((c == 'h' || c == 'l') && m_parameters.startsWith('?'))
                        for (const auto &value : m_parameters.mid(1).split(';'))
                            if (value == QByteArrayLiteral("9001")) m_enabled = c == 'h';
                    m_state = Ground; m_parameters.clear();
                } else if (c == 0x1b) { m_state = Escape; m_parameters.clear(); }
                else if (m_parameters.size() < 96) m_parameters += char(c);
                else { m_state = Ground; m_parameters.clear(); }
                break;
            case String:
                if (c == 0x07 && m_osc) m_state = Ground;
                else if (c == 0x1b) m_state = StringEscape;
                break;
            case StringEscape:
                if (c == '\\' || (c == 0x07 && m_osc)) m_state = Ground;
                else if (c != 0x1b) m_state = String;
                break;
            }
        }
    }
    static QByteArray encode(const QKeyEvent &event, bool pressed) {
        unsigned vk = 0, scan = 0;
#ifdef Q_OS_WIN
        vk = event.nativeVirtualKey(); scan = event.nativeScanCode() & 0xffU;
#endif
        if (!vk) {
            if ((event.key() >= Qt::Key_A && event.key() <= Qt::Key_Z)
                || (event.key() >= Qt::Key_0 && event.key() <= Qt::Key_9)) vk = unsigned(event.key());
            else if (event.key() >= Qt::Key_F1 && event.key() <= Qt::Key_F24) vk = 0x70U + unsigned(event.key() - Qt::Key_F1);
            else switch (event.key()) {
            case Qt::Key_Control: vk = 17; break; case Qt::Key_Shift: vk = 16; break;
            case Qt::Key_Alt: vk = 18; break; case Qt::Key_Meta: vk = 91; break;
            case Qt::Key_Return: case Qt::Key_Enter: vk = 13; break;
            case Qt::Key_Tab: case Qt::Key_Backtab: vk = 9; break;
            case Qt::Key_Backspace: vk = 8; break; case Qt::Key_Escape: vk = 27; break;
            case Qt::Key_Space: vk = 32; break; case Qt::Key_Left: vk = 37; break;
            case Qt::Key_Up: vk = 38; break; case Qt::Key_Right: vk = 39; break;
            case Qt::Key_Down: vk = 40; break; case Qt::Key_Home: vk = 36; break;
            case Qt::Key_End: vk = 35; break; case Qt::Key_PageUp: vk = 33; break;
            case Qt::Key_PageDown: vk = 34; break; case Qt::Key_Insert: vk = 45; break;
            case Qt::Key_Delete: vk = 46; break; default: break;
            }
        }
#ifdef Q_OS_WIN
        if (!scan && vk) scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) & 0xffU;
#endif
        unsigned state = 0;
        if (event.modifiers().testFlag(Qt::ControlModifier)) state |= 8;
        if (event.modifiers().testFlag(Qt::AltModifier)) state |= 2;
        if (event.modifiers().testFlag(Qt::ShiftModifier)) state |= 16;
        // Qt modifier-key events can include the key being released. Win32's
        // state describes the state after this record, so clear it on key-up.
        const unsigned ownModifier = event.key() == Qt::Key_Control ? 8U
            : event.key() == Qt::Key_Alt ? 2U : event.key() == Qt::Key_Shift ? 16U : 0U;
        if (ownModifier) state = pressed ? state | ownModifier : state & ~ownModifier;
        if (event.key() == Qt::Key_Left || event.key() == Qt::Key_Right || event.key() == Qt::Key_Up
            || event.key() == Qt::Key_Down || event.key() == Qt::Key_Home || event.key() == Qt::Key_End
            || event.key() == Qt::Key_Insert || event.key() == Qt::Key_Delete || event.key() == Qt::Key_PageUp
            || event.key() == Qt::Key_PageDown) state |= 256;
        QString text = event.text();
        if (text.isEmpty()) {
            if (vk == 13) text = QString(QChar(13));
            else if (vk == 9) text = QString(QChar(9));
            else if (vk == 8) text = QString(QChar(8));
            else if (vk == 27) text = QString(QChar(27));
        }
        if (text.isEmpty()) text = QString(QChar(0));
        // A typed secret passes through here: one block that never grows, and
        // the character code's temporary is zeroed (the caller zeroes text's).
        QByteArray result;
        result.reserve(text.size() * 32);
        for (const QChar ch : std::as_const(text)) { // non-const iteration would detach a copy
            QByteArray code = QByteArray::number(ch.unicode());
            result.append("\x1b[").append(QByteArray::number(vk)).append(';').append(QByteArray::number(scan))
                .append(';').append(code).append(pressed ? ";1;" : ";0;").append(QByteArray::number(state)).append(";1_");
            core::secureWipe(code);
        }
        return result;
    }
private:
    enum State { Ground, Escape, Csi, String, StringEscape } m_state = Ground;
    bool m_enabled = false;
    bool m_osc = false;
    QByteArray m_parameters;
};
}
