#include <QtTest>
#include <bitshell/terminal/Win32Input.h>

using bitshell::terminal::Win32Input;
class Win32InputTest : public QObject {
    Q_OBJECT
private slots:
    void splitModes() {
        Win32Input input;
        input.feed(QByteArrayLiteral("\x1b[?90"));
        QVERIFY(!input.enabled());
        input.feed(QByteArrayLiteral("01;1004h"));
        QVERIFY(input.enabled());
        input.feed(QByteArrayLiteral("\x1b[?1004l"));
        QVERIFY(input.enabled());
        input.feed(QByteArrayLiteral("\x1b[?9001l"));
        QVERIFY(!input.enabled());
        input.feed(QByteArrayLiteral("\x1b[?9001h"));
        input.reset();
        QVERIFY(!input.enabled());
    }
    void ignoresStringsAndOversizedSequences() {
        Win32Input input;
        input.feed(QByteArrayLiteral("\x1b]0;title\x1b[?9001h\x07"));
        QVERIFY(!input.enabled());
        input.feed(QByteArrayLiteral("\x1bPdata\x07\x1b[?9001h\x1b\\"));
        QVERIFY(!input.enabled());
        input.feed(QByteArrayLiteral("\x1b[?") + QByteArray(512, '0') + QByteArrayLiteral(";9001h"));
        QVERIFY(!input.enabled());
        input.feed(QByteArrayLiteral("\x1b[?9001h"));
        QVERIFY(input.enabled());
    }
    void keyDownAndUp() {
        QKeyEvent down(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, 46, 67, 8, QString(QChar(3)));
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier, 46, 67, 8, QString(QChar(3)));
        const auto downBytes = Win32Input::encode(down, true);
        const auto upBytes = Win32Input::encode(up, false);
#ifdef Q_OS_WIN
        QCOMPARE(downBytes, QByteArrayLiteral("\x1b[67;46;3;1;8;1_"));
        QCOMPARE(upBytes, QByteArrayLiteral("\x1b[67;46;3;0;8;1_"));
#else
        QVERIFY(downBytes.startsWith(QByteArrayLiteral("\x1b[67;")));
        QVERIFY(downBytes.endsWith(QByteArrayLiteral(";3;1;8;1_")));
        QVERIFY(upBytes.endsWith(QByteArrayLiteral(";3;0;8;1_")));
#endif
        QKeyEvent releaseControl(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
        const auto released = Win32Input::encode(releaseControl, false);
        QVERIFY2(released.endsWith(QByteArrayLiteral(";0;0;0;1_")), released.constData());
        QKeyEvent pressControl(QEvent::KeyPress, Qt::Key_Control, Qt::NoModifier);
        QVERIFY(Win32Input::encode(pressControl, true).endsWith(QByteArrayLiteral(";0;1;8;1_")));
    }
};
QTEST_MAIN(Win32InputTest)
#include "Win32InputTest.moc"
