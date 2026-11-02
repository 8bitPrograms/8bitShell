#include <QtTest>
#include <QQuickWindow>
#include <QQuickItem>
#include "../src/app/WindowsFrame.h"

class WindowsFrameTest : public QObject {
    Q_OBJECT
private slots:
    void nativeStyleAndHitTests() {
        QQuickWindow window;
        window.setFlags(Qt::Window | Qt::FramelessWindowHint);
        window.setGeometry(100,100,800,600);
        QQuickItem maximize(window.contentItem());
        maximize.setObjectName(QStringLiteral("windowMaximizeButton"));
        maximize.setPosition({700,0}); maximize.setSize({40,34});
        bitshell::app::WindowsFrame frame(&window);
        const auto hwnd = reinterpret_cast<HWND>(window.winId());
        const auto style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        QVERIFY(style & WS_THICKFRAME);
        QVERIFY(style & WS_MAXIMIZEBOX);
        QVERIFY(style & WS_MINIMIZEBOX);
        const auto hit = [&](QPoint point) {
            MSG message{}; message.hwnd=hwnd; message.message=WM_NCHITTEST;
            message.lParam=MAKELPARAM(point.x(),point.y());
            qintptr result = 0;
            if (!frame.nativeEventFilter({}, &message, &result)) return qintptr(-1);
            return result;
        };
        POINT client{qRound(720 * window.devicePixelRatio()), qRound(20 * window.devicePixelRatio())};
        ClientToScreen(hwnd, &client);
        QCOMPARE(hit({client.x,client.y}), qintptr(HTMAXBUTTON));
        RECT rect{}; GetWindowRect(hwnd,&rect);
        QCOMPARE(hit({rect.left+1,rect.top+1}), qintptr(HTTOPLEFT));
        QCOMPARE(hit({rect.right-1,rect.bottom-1}), qintptr(HTBOTTOMRIGHT));
        QCOMPARE(hit({rect.left+100,rect.top+150}), qintptr(HTCLIENT));
        MSG other{}; other.hwnd = nullptr; qintptr ignored=0;
        QVERIFY(!frame.nativeEventFilter({}, &other, &ignored));
    }
};
QTEST_MAIN(WindowsFrameTest)
#include "WindowsFrameTest.moc"
