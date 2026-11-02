#include <QtTest>
#include <QDesktopServices>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QScopeGuard>
#include <bitshell/terminal/TerminalUrl.h>
#include <bitshell/terminal/TerminalView.h>
#include "TerminalTestAccess.h"

using namespace bitshell::terminal;
class UrlCapture : public QObject {
    Q_OBJECT
public:
    QList<QUrl> opened;
public slots:
    void capture(const QUrl& url) { opened.append(url); }
};
class TerminalLinkTest : public QObject {
    Q_OBJECT
    void mouse(TerminalView& terminal, QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers) {
        const auto button = type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
        const auto buttons = type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton;
        QMouseEvent event(type,point,point,button,buttons,modifiers);
        QCoreApplication::sendEvent(&terminal,&event);
    }
private slots:
    void printableUrls_data() {
        QTest::addColumn<QString>("text"); QTest::addColumn<QString>("expected");
        QTest::newRow("https") << QStringLiteral("Visit https://example.test/a?q=1&b=2#part now") << QStringLiteral("https://example.test/a?q=1&b=2#part");
        QTest::newRow("http-port") << QStringLiteral("http://localhost:8080/status") << QStringLiteral("http://localhost:8080/status");
        QTest::newRow("www") << QStringLiteral("www.example.test/docs.") << QStringLiteral("https://www.example.test/docs");
        QTest::newRow("balanced-path") << QStringLiteral("Read (https://example.test/a_(b)).") << QStringLiteral("https://example.test/a_(b)");
        QTest::newRow("ipv6") << QStringLiteral("[http://[::1]:8000/a]") << QStringLiteral("http://[::1]:8000/a");
        QTest::newRow("uppercase") << QStringLiteral("HTTPS://EXAMPLE.TEST/a") << QStringLiteral("https://example.test/a");
    }
    void printableUrls() {
        QFETCH(QString,text); QFETCH(QString,expected);
        auto offset = text.indexOf(QStringLiteral("http"),0,Qt::CaseInsensitive);
        if (offset < 0) offset=text.indexOf(QStringLiteral("www."));
        QCOMPARE(terminalUrlAt(text,offset+2),QUrl(expected));
        QVERIFY(terminalUrlAt(text,-1).isEmpty());
        QVERIFY(terminalUrlAt(text,text.size()).isEmpty());
    }
    void unsupportedSchemesAndInvalidHostsDoNotOpen() {
        for(const auto& text : {QStringLiteral("file:///C:/Windows/System32/cmd.exe"),
                QStringLiteral("javascript:alert(1)"),QStringLiteral("ms-settings:privacy"),
                QStringLiteral("https:///missing-host"),QStringLiteral("emailhttps://example.test"),
                QStringLiteral("mailto:test@example.test")})
            QVERIFY(terminalUrlAt(text,2).isEmpty());
        QVERIFY(terminalUrlAt(QString(8193,u'a'),0).isEmpty());
        const QString text=QStringLiteral("https://example.test.");
        QVERIFY(terminalUrlAt(text,text.size()-1).isEmpty());
    }
    void ctrlClickOpensOnReleaseAndNeverOnDragOrPlainClick() {
        UrlCapture capture;
        QDesktopServices::setUrlHandler(QStringLiteral("https"),&capture,"capture");
        const auto restore=qScopeGuard([]{QDesktopServices::unsetUrlHandler(QStringLiteral("https"));});
        TerminalView terminal;
        terminal.setWidth(800); terminal.setHeight(450); terminal.setLogEnabled(false);
        TerminalTestAccess::feed(terminal,"Read https://example.test/docs now");
        const auto point=TerminalTestAccess::cellCenter(terminal,0,12);
        mouse(terminal,QEvent::MouseButtonPress,point,Qt::ControlModifier);
        QVERIFY(capture.opened.isEmpty());
        mouse(terminal,QEvent::MouseButtonRelease,point,Qt::ControlModifier);
        QCOMPARE(capture.opened,QList<QUrl>{QUrl(QStringLiteral("https://example.test/docs"))});
        mouse(terminal,QEvent::MouseButtonPress,point,Qt::NoModifier);
        mouse(terminal,QEvent::MouseButtonRelease,point,Qt::NoModifier);
        QCOMPARE(capture.opened.size(),1);
        mouse(terminal,QEvent::MouseButtonPress,point,Qt::ControlModifier);
        mouse(terminal,QEvent::MouseMove,point+QPointF(80,0),Qt::ControlModifier);
        mouse(terminal,QEvent::MouseMove,point,Qt::ControlModifier);
        mouse(terminal,QEvent::MouseButtonRelease,point,Qt::ControlModifier);
        QCOMPARE(capture.opened.size(),1);
        mouse(terminal,QEvent::MouseButtonPress,point,Qt::ControlModifier);
        mouse(terminal,QEvent::MouseButtonRelease,point,Qt::NoModifier);
        QCOMPARE(capture.opened.size(),1);
        mouse(terminal,QEvent::MouseButtonPress,point,Qt::ControlModifier);
        TerminalTestAccess::feed(terminal,"\x1b[2J\x1b[Hhttps://different.test");
        mouse(terminal,QEvent::MouseButtonRelease,point,Qt::ControlModifier);
        QCOMPARE(capture.opened.size(),1);
    }
    void ansiWideCharactersSoftWrapsAndScrollback() {
        TerminalView terminal;
        terminal.setWidth(800); terminal.setHeight(250); terminal.setLogEnabled(false);
        const auto first=QStringLiteral("\u754c https://example.test/colored");
        TerminalTestAccess::feed(terminal,"\x1b[32m"+first.toUtf8()+"\x1b[0m");
        QCOMPARE(TerminalTestAccess::urlAt(terminal,TerminalTestAccess::cellCenter(terminal,0,6)),
            QUrl(QStringLiteral("https://example.test/colored")));
        QVERIFY(TerminalTestAccess::urlAt(terminal,{1,1}).isEmpty());
        TerminalTestAccess::resetLocalScreen(terminal);
        const auto address=QStringLiteral("https://example.test/")+QString(terminal.columns()+10,u'a');
        TerminalTestAccess::feed(terminal,address.toUtf8());
        QCOMPARE(TerminalTestAccess::urlAt(terminal,TerminalTestAccess::cellCenter(terminal,1,3)),QUrl(address));
        TerminalTestAccess::resetLocalScreen(terminal);
        TerminalTestAccess::feed(terminal,"https://example.test/history\r\n");
        TerminalTestAccess::feed(terminal,QByteArray("filler\r\n").repeated(terminal.rows()+3));
        TerminalTestAccess::scrollHistory(terminal,terminal.scrollbackLines());
        QCOMPARE(TerminalTestAccess::urlAt(terminal,TerminalTestAccess::cellCenter(terminal,0,5)),
            QUrl(QStringLiteral("https://example.test/history")));
    }
};
QTEST_MAIN(TerminalLinkTest)
#include "TerminalLinkTest.moc"
