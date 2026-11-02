#include <QtTest>
#include <QBuffer>
#include <QPainter>
#include <bitshell/terminal/TerminalGraphics.h>
#include <bitshell/terminal/TerminalView.h>
#include "TerminalTestAccess.h"

using namespace bitshell::terminal;

class TerminalGraphicsTest : public QObject {
    Q_OBJECT
    QByteArray png() {
        QImage image(2, 2, QImage::Format_ARGB32);
        image.fill(Qt::red);
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return buffer.data().toBase64();
    }
private slots:
    void inlinePngPaintQueryAndDelete() {
        TerminalGraphics graphics;
        QCOMPARE(graphics.imageBytes(), 0);
        QCOMPARE(graphics.command("Ga=q,i=3,f=100;" + png(), {}, {8,16}), QByteArray("\x1b_Gi=3;OK\x1b\\"));
        QCOMPARE(graphics.imageBytes(), 0);
        QCOMPARE(graphics.command("Ga=T,i=3,f=100,c=2,r=2,C=1;" + png(), {2,1}, {8,16}), QByteArray("\x1b_Gi=3;OK\x1b\\"));
        QCOMPARE(graphics.placementCount(), 1);
        QImage canvas(100,100,QImage::Format_ARGB32); canvas.fill(Qt::blue);
        QPainter painter(&canvas);
        graphics.paint(&painter, {}, {8,16}, 0);
        painter.end();
        QCOMPARE(canvas.pixelColor(18,20), QColor(Qt::red));
        QCOMPARE(canvas.pixelColor(15,20), QColor(Qt::blue));
        graphics.command("Ga=d,d=I,i=3,q=2;", {}, {8,16});
        QCOMPARE(graphics.placementCount(), 0);
        QCOMPARE(graphics.imageBytes(), 0);
    }
    void chunkingRawPixelsAndQuietErrors() {
        TerminalGraphics graphics;
        const auto data = png();
        for (int split = 1; split < data.size(); ++split) {
            graphics.clear();
            QVERIFY(graphics.command("Ga=T,i=4,f=100,C=1,m=1;" + data.left(split), {}, {8,16}).isEmpty());
            QCOMPARE(graphics.placementCount(), 0);
            QCOMPARE(graphics.command("Gm=0;" + data.mid(split), {}, {8,16}), QByteArray("\x1b_Gi=4;OK\x1b\\"));
            QCOMPARE(graphics.placementCount(), 1);
        }
        graphics.clear();
        QCOMPARE(graphics.command("Ga=T,i=1,f=24,s=1,v=1,C=1;/wAA", {}, {8,16}), QByteArray("\x1b_Gi=1;OK\x1b\\"));
        QVERIFY(graphics.command("Ga=q,i=8,t=f,q=2;L2V0Yy9wYXNzd2Q=", {}, {8,16}).isEmpty());
        QVERIFY(graphics.command("Ga=q,i=8,t=f;L2V0Yy9wYXNzd2Q=", {}, {8,16}).contains("ENOTSUP"));
        QVERIFY(graphics.command("Ga=q,i=8,f=32,s=999999999,v=999999999;AAAA", {}, {8,16}).contains("E2BIG"));
        QVERIFY(graphics.command("Ga=q,i=8,f=100;!not-base64!", {}, {8,16}).contains("EINVAL"));
        QCOMPARE(graphics.placementCount(), 1);
    }
    void storageIsBoundedAndScrolledPlacementsExpire() {
        TerminalGraphics graphics;
        for (int id=1; id<=64; ++id)
            QVERIFY(graphics.command("Ga=t,i=" + QByteArray::number(id) + ",f=100;" + png(), {}, {8,16}).contains("OK"));
        QVERIFY(graphics.command("Ga=t,i=65,f=100;" + png(), {}, {8,16}).contains("ENOSPC"));
        QVERIFY(graphics.imageBytes() <= TerminalGraphics::ImageBudget);
        graphics.command("Ga=p,i=1,c=1,r=1,C=1;", {0,1}, {8,16});
        QCOMPARE(graphics.placementCount(), 1);
        graphics.scroll(0,24,-3);
        QCOMPARE(graphics.placementCount(), 0);
        graphics.clear();
        QCOMPARE(graphics.imageBytes(), 0);
    }
    void invalidTransfersAndPartialScrollCannotDamageOtherPlacements() {
        TerminalGraphics graphics;
        graphics.command("Ga=T,i=1,f=100,c=1,r=1,C=1;" + png(),{0,0},{8,16});
        graphics.command("Ga=T,i=2,f=100,c=1,r=1,C=1;" + png(),{0,10},{8,16});
        graphics.scroll(5,24,-1);
        QCOMPARE(graphics.placementCount(),2);
        QVERIFY(graphics.command("Ga=T,i=3,f=100,C=0;" + png(),{},{8,16}).contains("ENOTSUP"));
        QVERIFY(graphics.command("Ga=T,i=3,f=100,C=1,m=1;" + QByteArray(TerminalGraphics::PayloadLimit,'A'),{},{8,16}).isEmpty());
        QVERIFY(graphics.imageBytes() <= TerminalGraphics::ImageBudget);
        QCOMPARE(graphics.placementCount(),2);
        QVERIFY(graphics.command("Ga=q,i=4,f=100;" + png(),{},{8,16}).contains("OK"));
    }
    void fullUnsignedImageAndPlacementIds() {
        TerminalGraphics graphics;
        QVERIFY(graphics.command("Ga=T,i=4294967295,p=4294967295,f=100,C=1;" + png(),{},{8,16})
            .contains("i=4294967295,p=4294967295;OK"));
        QCOMPARE(graphics.placementCount(),1);
        graphics.command("Ga=d,d=I,i=4294967295,q=2;",{},{8,16});
        QCOMPARE(graphics.placementCount(),0);
        QCOMPARE(graphics.imageBytes(),0);
    }
    void terminalParsesEveryFragmentBoundaryAndClearsOnAlternateScreen() {
        const auto sequence = QByteArray("\x1b_Ga=T,i=1,f=100,c=2,r=2,q=2,C=1;") + png() + "\x1b\\";
        for (int split=0; split<=sequence.size(); ++split) {
            TerminalView terminal;
            terminal.setLogEnabled(false);
            TerminalTestAccess::feed(terminal, sequence.left(split));
            TerminalTestAccess::feed(terminal, sequence.mid(split));
            QCOMPARE(TerminalTestAccess::graphicsCount(terminal), 1);
            QVERIFY(TerminalTestAccess::graphicsBytes(terminal) > 0);
            TerminalTestAccess::feed(terminal, "\x1b[?1049h");
            QCOMPARE(TerminalTestAccess::graphicsCount(terminal), 0);
            QCOMPARE(TerminalTestAccess::graphicsBytes(terminal), 0);
        }
    }
};
QTEST_MAIN(TerminalGraphicsTest)
#include "TerminalGraphicsTest.moc"
