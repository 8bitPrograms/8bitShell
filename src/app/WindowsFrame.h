#pragma once

#ifdef Q_OS_WIN
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

namespace bitshell::app {
// Retain a real resizable Win32 frame while QML paints the caption. Windows
// then owns moving, edge/corner snapping, Win+Arrow and the maximize flyout.
class WindowsFrame final : public QObject, public QAbstractNativeEventFilter {
public:
    explicit WindowsFrame(QQuickWindow* window) : QObject(window), m_window(window) {
        m_hwnd = reinterpret_cast<HWND>(window->winId());
        const auto style = GetWindowLongPtrW(m_hwnd, GWL_STYLE);
        SetWindowLongPtrW(m_hwnd, GWL_STYLE, style | WS_CAPTION | WS_THICKFRAME
            | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
        QCoreApplication::instance()->installNativeEventFilter(this);
        SetWindowPos(m_hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        const MARGINS margins{1, 1, 1, 1};
        DwmExtendFrameIntoClientArea(m_hwnd, &margins);
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(m_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
    }
    ~WindowsFrame() override {
        if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
        const auto* msg = static_cast<MSG*>(message);
        if (!m_window || msg->hwnd != m_hwnd) return false;
        if (msg->message == WM_NCCALCSIZE && msg->wParam) {
            // No system title bar; keep maximized content inside the monitor's
            // work area, including taskbars on secondary/negative-origin screens.
            if (IsZoomed(m_hwnd)) {
                MONITORINFO monitor{sizeof(MONITORINFO)};
                if (GetMonitorInfoW(MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                    reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam)->rgrc[0] = monitor.rcWork;
            }
            *result = 0;
            return true;
        }
        if (msg->message == WM_NCHITTEST) {
            POINT point{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
            RECT rect{};
            GetWindowRect(m_hwnd, &rect);
            const UINT dpi = GetDpiForWindow(m_hwnd);
            const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi)
                + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
            if (!IsZoomed(m_hwnd)) {
                const bool left = point.x < rect.left + border;
                const bool right = point.x >= rect.right - border;
                const bool top = point.y < rect.top + border;
                const bool bottom = point.y >= rect.bottom - border;
                const LRESULT edge = top ? (left ? HTTOPLEFT : right ? HTTOPRIGHT : HTTOP)
                    : bottom ? (left ? HTBOTTOMLEFT : right ? HTBOTTOMRIGHT : HTBOTTOM)
                    : left ? HTLEFT : right ? HTRIGHT : HTNOWHERE;
                if (edge != HTNOWHERE) { *result = edge; return true; }
            }
            ScreenToClient(m_hwnd, &point);
            if (auto* button = maximizeButton(); button && button->isVisible() && button->isEnabled()) {
                const qreal scale = m_window->devicePixelRatio();
                const auto local = button->mapFromScene(QPointF(point.x / scale, point.y / scale));
                if (button->contains(local)) { *result = HTMAXBUTTON; return true; }
            }
            *result = HTCLIENT;
            return true;
        }
        if (msg->message == WM_NCMOUSEMOVE && msg->wParam == HTMAXBUTTON) {
            if (auto* button = maximizeButton()) button->setProperty("nativeHovered", true);
            TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, m_hwnd, 0};
            TrackMouseEvent(&tracking);
        } else if (msg->message == WM_NCMOUSELEAVE || msg->message == WM_MOUSEMOVE) {
            if (auto* button = maximizeButton()) button->setProperty("nativeHovered", false);
        }
        if ((msg->message == WM_NCLBUTTONDOWN || msg->message == WM_NCLBUTTONUP
                || msg->message == WM_NCLBUTTONDBLCLK || msg->message == WM_NCMOUSEMOVE)
            && msg->wParam == HTMAXBUTTON) {
            *result = DefWindowProcW(m_hwnd, msg->message, msg->wParam, msg->lParam);
            return true;
        }
        return false;
    }
private:
    QQuickItem* maximizeButton() const {
        return m_window->findChild<QQuickItem*>(QStringLiteral("windowMaximizeButton"));
    }
    QPointer<QQuickWindow> m_window;
    HWND m_hwnd = nullptr;
};
} // namespace bitshell::app
#endif
