#include "TerminalTestAccess.h"
#include <bitshell/core/SessionStore.h>
#include <bitshell/core/LocalFileModel.h>
#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QQuickWindow>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <cmath>
#include <memory>

using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

class PerformanceRegressionTest final : public QObject {
    Q_OBJECT
private slots:
    void cacheMatchesFreshRenderingAndInvalidates();
    void cursorMotionDoesNotRequestSceneUpdates();
    void incrementalSearchMatchesFullScan();
    void hiddenTerminalsStopBlinkingAndReleaseCache();
    void historyNotificationsAreBatched();
    void catalogIndexTracksEditsAndRejectedReloads();
    void fileWatcherCoalescesAndResumes();
    void concurrentHiddenPtysKeepCompleteLogs();
    void fastGlyphConversionPreservesUnicodeValidation();
    void renderingModesUseSameMetricsAndInvalidateCache();
};

static QImage render(TerminalView& terminal, qreal ratio, bool fresh = false) {
    QImage image(int(std::ceil(terminal.width() * ratio)), int(std::ceil(terminal.height() * ratio)), QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    if (fresh) TerminalTestAccess::repaintContent(terminal, &painter);
    else terminal.paint(&painter);
    return image;
}

void PerformanceRegressionTest::cacheMatchesFreshRenderingAndInvalidates() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setWidth(800); terminal.setHeight(450);
    TerminalTestAccess::feed(terminal, QByteArray("\x1b[?25l\x1b[31mERROR\x1b[0m 192.0.2.8 active\r\n")
        + QStringLiteral("Türkçe İı, 界, e\u0301, 😀\r\n").toUtf8());
    for (const qreal ratio : {1.0, 1.25, 2.0}) {
        QCOMPARE(render(terminal, ratio), render(terminal, ratio, true));
        const auto count = TerminalTestAccess::contentPaints(terminal);
        render(terminal, ratio);
        render(terminal, ratio);
        QCOMPARE(TerminalTestAccess::contentPaints(terminal), count);
        TerminalTestAccess::feed(terminal, QByteArray("\x1b[1;1H\x1b[1;3;4mFAIL\x1b[0m\x1b[2;8Habc"));
        QCOMPARE(render(terminal, ratio), render(terminal, ratio, true));
        terminal.setSearchText(QStringLiteral("abc"));
        QCOMPARE(render(terminal, ratio), render(terminal, ratio, true));
        terminal.selectAll();
        QCOMPARE(render(terminal, ratio), render(terminal, ratio, true));
        terminal.setFontPixelSize(16 + ratio);
        terminal.setColorScheme({{QStringLiteral("background"), QStringLiteral("#fffaf0")},
            {QStringLiteral("foreground"), QStringLiteral("#111111")}});
        QCOMPARE(render(terminal, ratio), render(terminal, ratio, true));
        QVERIFY(TerminalTestAccess::cachedBytes(terminal) <= 16 * 1024 * 1024);
    }
    terminal.setWidth(2800); terminal.setHeight(1600);
    render(terminal, 1.0);
    QCOMPARE(TerminalTestAccess::cachedBytes(terminal), qsizetype(0));
}

void PerformanceRegressionTest::cursorMotionDoesNotRequestSceneUpdates() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setWidth(800);
    terminal.setHeight(450);
    render(terminal, 1.0);
    const auto idle = TerminalTestAccess::sceneUpdates(terminal);
    const auto painted = TerminalTestAccess::contentPaints(terminal);
    QTest::qWait(1200);
    QCOMPARE(TerminalTestAccess::sceneUpdates(terminal), idle);
    QCOMPARE(TerminalTestAccess::contentPaints(terminal), painted);

    TerminalTestAccess::feed(terminal, QByteArray("\x1b[5;8H"));
    const qreal x = terminal.cursorX();
    const qreal y = terminal.cursorY();
    const qreal width = terminal.cursorWidth();
    const qreal height = terminal.cursorHeight();
    QVERIFY(terminal.cursorOverlayVisible());
    QVERIFY(width > 2.0);
    QVERIFY(height > 2.0);
    TerminalTestAccess::feed(terminal, QByteArray("\x1b[5;9H"));
    QCOMPARE(TerminalTestAccess::sceneUpdates(terminal), idle);
    QCOMPARE(terminal.cursorY(), y);
    QCOMPARE(terminal.cursorX(), x + width);
    QCoreApplication::processEvents();
    QVERIFY(terminal.cursorEpoch() > 0);

    TerminalTestAccess::feed(terminal, QByteArray("\x1b[4 q"));
    QCOMPARE(TerminalTestAccess::sceneUpdates(terminal), idle);
    QCOMPARE(terminal.cursorBlinkEnabled(), false);
    QCOMPARE(terminal.cursorWidth(), width);
    QCOMPARE(terminal.cursorHeight(), 2.0);
    QCOMPARE(terminal.cursorY(), y + height - 2.0);

    TerminalTestAccess::feed(terminal, QByteArray("\x1b[6 q"));
    QCOMPARE(TerminalTestAccess::sceneUpdates(terminal), idle);
    QCOMPARE(terminal.cursorWidth(), 2.0);
    QCOMPARE(terminal.cursorHeight(), height);
    QCOMPARE(terminal.cursorX(), x + width);

    TerminalTestAccess::feed(terminal, QByteArray("\x1b[?25l"));
    QCOMPARE(terminal.cursorOverlayVisible(), false);
    QCOMPARE(TerminalTestAccess::sceneUpdates(terminal), idle);

    TerminalTestAccess::feed(terminal, QByteArray("X"));
    QVERIFY(TerminalTestAccess::sceneUpdates(terminal) > idle);
}

void PerformanceRegressionTest::renderingModesUseSameMetricsAndInvalidateCache() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setSyntaxHighlightEnabled(false);
    terminal.setFontFamily(QStringLiteral("DejaVu Sans Mono"));
    terminal.setWidth(640); terminal.setHeight(360);
    TerminalTestAccess::feed(terminal, QByteArray("\x1b[?25l") + QStringLiteral("AaBb 01234 Türkçe 界 e\u0301 😀\r\n").toUtf8());
    for (const auto& smoothing : {QStringLiteral("system"), QStringLiteral("grayscale"), QStringLiteral("aliased")}) {
        for (const auto& hinting : {QStringLiteral("default"), QStringLiteral("none"), QStringLiteral("vertical"), QStringLiteral("full")}) {
            terminal.setTextAntialiasing(smoothing);
            terminal.setFontHinting(hinting);
            QCOMPARE(terminal.textAntialiasing(), smoothing);
            QCOMPARE(terminal.fontHinting(), hinting);
            const auto font = TerminalTestAccess::font(terminal);
            const int hint = hinting == QStringLiteral("none") ? QFont::PreferNoHinting
                : hinting == QStringLiteral("vertical") ? QFont::PreferVerticalHinting
                : hinting == QStringLiteral("full") ? QFont::PreferFullHinting : QFont::PreferDefaultHinting;
            QCOMPARE(int(font.hintingPreference()), hint);
            QCOMPARE(bool(font.styleStrategy() & QFont::NoAntialias), smoothing == QStringLiteral("aliased"));
            QCOMPARE(bool(font.styleStrategy() & QFont::NoSubpixelAntialias), smoothing == QStringLiteral("grayscale"));
            for (const qreal ratio : {1.0, 1.25, 2.0})
                QCOMPARE(render(terminal, ratio), render(terminal, ratio, true));
        }
    }
    terminal.setFontHinting(QStringLiteral("default"));
    terminal.setTextAntialiasing(QStringLiteral("grayscale"));
    const auto smooth = render(terminal, 1);
    terminal.setTextAntialiasing(QStringLiteral("aliased"));
    const auto aliased = render(terminal, 1);
#ifdef Q_OS_WIN
    if (aliased == smooth)
        qWarning("Aliased and grayscale rasters match; this Qt build has no shipped fonts.");
    else
        QVERIFY(aliased != smooth);
#else
    QVERIFY(aliased != smooth);
#endif
    terminal.setTextAntialiasing(QStringLiteral("invalid"));
    terminal.setFontHinting(QStringLiteral("invalid"));
    QCOMPARE(terminal.textAntialiasing(), QStringLiteral("aliased"));
    QCOMPARE(terminal.fontHinting(), QStringLiteral("default"));
    terminal.setSearchText(QStringLiteral("Türkçe"));
    QCOMPARE(terminal.searchMatchCount(), 1);
}

void PerformanceRegressionTest::incrementalSearchMatchesFullScan() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setWidth(640); terminal.setHeight(360);
    terminal.setScrollbackLimit(100);
    terminal.setSearchText(QStringLiteral("error"));
    QRandomGenerator random(42);
    for (int iteration = 0; iteration < 200; ++iteration) {
        QByteArray bytes;
        const int lines = int(random.bounded(15U)) + 1;
        for (int i = 0; i < lines; ++i)
            bytes += QStringLiteral("%1 ERROR Türkçe 界 e\u0301 ready 192.0.2.%2\r\n").arg(iteration).arg(i).toUtf8();
        if (iteration % 11 == 0) bytes += "\x1b[1;1H\x1b[2Kmodified error\x1b[999;1H";
        if (iteration % 19 == 0) bytes += "\x1b[3J";
        if (iteration % 23 == 0) bytes += "\x1b[?1049hERROR alternate\x1b[?1049l";
        TerminalTestAccess::feed(terminal, bytes);
        const auto incremental = TerminalTestAccess::matches(terminal);
        TerminalTestAccess::fullSearch(terminal);
        QCOMPARE(incremental, TerminalTestAccess::matches(terminal));
        if (iteration % 7 == 0) {
            terminal.setHeight(iteration % 2 ? 440 : 320);
            const auto resized = TerminalTestAccess::matches(terminal);
            TerminalTestAccess::fullSearch(terminal);
            QCOMPARE(resized, TerminalTestAccess::matches(terminal));
        }
    }
}

void PerformanceRegressionTest::hiddenTerminalsStopBlinkingAndReleaseCache() {
    QQuickWindow window;
    window.resize(640, 360);
    TerminalView terminal(window.contentItem());
    terminal.setWidth(640); terminal.setHeight(360);
    terminal.setLogEnabled(false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    render(terminal, 1);
    QVERIFY(TerminalTestAccess::cachedBytes(terminal) > 0);
    terminal.setVisible(false);
    QCOMPARE(TerminalTestAccess::cachedBytes(terminal), qsizetype(0));
    const auto hiddenUpdates = TerminalTestAccess::sceneUpdates(terminal);
    TerminalTestAccess::feed(terminal, QByteArray("hidden output still processed\r\n"));
    terminal.setSearchText(QStringLiteral("still processed"));
    QCOMPARE(terminal.searchMatchCount(), 1);
    const auto afterOutput = TerminalTestAccess::sceneUpdates(terminal);
    QTest::qWait(1100);
    QCOMPARE(TerminalTestAccess::sceneUpdates(terminal), afterOutput);
    QVERIFY(afterOutput >= hiddenUpdates);
    QCOMPARE(TerminalTestAccess::cachedBytes(terminal), qsizetype(0));
}

void PerformanceRegressionTest::historyNotificationsAreBatched() {
    TerminalView terminal;
    terminal.setLogEnabled(false);
    // Stay on the minimum column count. A 640px viewport with a narrow CI font
    // stores more cells than the history budget and drops the lines under test.
    terminal.setWidth(40); terminal.setHeight(360);
    QByteArray data;
    for (int i = 0; i < 4000; ++i) data += "line\r\n";
    QSignalSpy notifications(&terminal, &TerminalView::scrollbackChanged);
    TerminalTestAccess::feed(terminal, data);
    QVERIFY(terminal.scrollbackLines() > 3900);
    QCOMPARE(notifications.size(), 1);
    terminal.setSearchText(QStringLiteral("line"));
    QVERIFY(terminal.searchMatchCount() >= 4000);
}

void PerformanceRegressionTest::catalogIndexTracksEditsAndRejectedReloads() {
    QTemporaryDir config;
    QVERIFY(config.isValid());
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    bitshell::core::SessionStore catalog;
    const int first = catalog.addSession({{QStringLiteral("name"), QStringLiteral("node-10")},
        {QStringLiteral("host"), QStringLiteral("node.example.test")}, {QStringLiteral("group"), QStringLiteral("Fleet/Oracle/DC1")}});
    QVERIFY(first >= 0);
    QVERIFY(catalog.addFolder(QStringLiteral("Fleet/Oracle/DC1"), QStringLiteral("Empty")));
    const auto tree = catalog.treeRows(QStringLiteral("oracle"), {});
    QCOMPARE(tree.size(), 5); // All matching descendants, including the empty folder.
    QCOMPARE(catalog.treeRows(QStringLiteral("node-10"), {}).size(), 4);
    QVERIFY(catalog.addFolder(QStringLiteral("Fleet"), QStringLiteral("Other")));
    QVERIFY(catalog.moveSession(first, QStringLiteral("Fleet/Other")));
    const auto moved = catalog.treeRows(QStringLiteral("node-10"), {});
    QCOMPARE(moved.size(), 3);
    QCOMPARE(moved.last().toMap().value(QStringLiteral("group")).toString(), QStringLiteral("Fleet/Other"));
    QVERIFY(catalog.updateSession(first, {{QStringLiteral("name"), QStringLiteral("renamed")}}));
    QVERIFY(catalog.treeRows(QStringLiteral("node-10"), {}).isEmpty());
    const auto stable = catalog.treeRows(QStringLiteral("renamed"), {});
    QFile invalid(catalog.storagePath());
    QVERIFY(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalid.write("version: [broken"); invalid.close();
    QVERIFY(!catalog.reload());
    QCOMPARE(catalog.treeRows(QStringLiteral("renamed"), {}), stable);
}

void PerformanceRegressionTest::fileWatcherCoalescesAndResumes() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    bitshell::core::LocalFileModel model;
    model.changeDirectory(directory.path());
    QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
    for (int index = 0; index < 200; ++index) {
        QFile file(directory.filePath(QString::number(index)));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.close();
        if (index % 10 == 0) QCoreApplication::processEvents();
    }
    QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 200, 2000);
    QVERIFY(resets.size() <= 3);
    model.setActive(false);
    resets.clear();
    QFile extra(directory.filePath(QStringLiteral("extra")));
    QVERIFY(extra.open(QIODevice::WriteOnly)); extra.close();
    QTest::qWait(150);
    QCOMPARE(resets.size(), 0);
    model.setActive(true);
    QCOMPARE(model.rowCount(), 201);
}

void PerformanceRegressionTest::concurrentHiddenPtysKeepCompleteLogs() {
#ifdef Q_OS_WIN
    QSKIP("The hidden PTY log fixture uses a POSIX shell.");
#endif
    QTemporaryDir logs;
    QVERIFY(logs.isValid());
    std::vector<std::unique_ptr<TerminalView>> terminals;
    QStringList paths;
    for (int index = 0; index < 20; ++index) {
        auto terminal = std::make_unique<TerminalView>();
        terminal->setLogEnabled(false);
        terminal->setVisible(false);
        terminal->setWidth(640); terminal->setHeight(360);
        terminal->setLogRootDirectory(logs.path());
        terminal->setLogFileNameTemplate(QStringLiteral("pty-%1.log").arg(index));
        QString error;
        const auto script = QStringLiteral("i=0; while [ \"$i\" -lt 600 ]; do printf 'PTY%1 row %%s\\n' \"$i\"; i=$((i+1)); done; printf 'END:%1\\n'")
            .arg(index).replace(QStringLiteral("%%s"), QStringLiteral("%s"));
        QVERIFY2(TerminalTestAccess::startFixture(*terminal, script, error), qPrintable(error));
        terminal->setLogEnabled(true);
        QVERIFY2(terminal->logError().isEmpty(), qPrintable(terminal->logError()));
        paths << terminal->logFilePath();
        terminals.push_back(std::move(terminal));
    }
    QTRY_VERIFY_WITH_TIMEOUT(std::none_of(terminals.begin(), terminals.end(), [](const auto& terminal) { return terminal->isRunning(); }), 10000);
    QList<quint64> settledUpdates;
    for (int index = 0; index < 20; ++index) {
        QFile log(paths.at(index));
        QVERIFY(log.open(QIODevice::ReadOnly));
        QByteArray expected;
        for (int row = 0; row < 600; ++row) expected += QStringLiteral("PTY%1 row %2\r\n").arg(index).arg(row).toUtf8();
        expected += QStringLiteral("END:%1\r\n").arg(index).toUtf8();
        const QByteArray actual = log.readAll();
        // QCOMPARE truncates long values; a lost tail shows only in the end.
        QVERIFY2(actual == expected, qPrintable(QStringLiteral("PTY%1 log: %2 bytes, expected %3; ends %4, expected end %5")
            .arg(index).arg(actual.size()).arg(expected.size())
            .arg(QString::fromLatin1(actual.right(64).toPercentEncoding(" ")),
                 QString::fromLatin1(expected.right(64).toPercentEncoding(" ")))));
        auto& terminal = *terminals[std::size_t(index)];
        terminal.setSearchText(QStringLiteral("END:%1").arg(index));
        QCOMPARE(terminal.searchMatchCount(), 1);
        settledUpdates << TerminalTestAccess::sceneUpdates(terminal);
        QCOMPARE(TerminalTestAccess::cachedBytes(terminal), qsizetype(0));
        QVERIFY2(terminal.logError().isEmpty(), qPrintable(terminal.logError()));
    }
    QTest::qWait(1100);
    for (int index = 0; index < 20; ++index)
        QCOMPARE(TerminalTestAccess::sceneUpdates(*terminals[std::size_t(index)]), settledUpdates.at(index));
}

void PerformanceRegressionTest::fastGlyphConversionPreservesUnicodeValidation() {
    TerminalView terminal;
    for (const char32_t codepoint : {U'A', U'İ', U'界', U'😀', char32_t(0xd800), char32_t(0xdfff), char32_t(0x110000)}) {
        VTermScreenCell cell{};
        cell.width = 1; cell.chars[0] = quint32(codepoint);
        QCOMPARE(TerminalTestAccess::cellText(terminal, cell), QString::fromUcs4(&codepoint, 1));
    }
    VTermScreenCell combining{};
    combining.chars[0] = 'e'; combining.chars[1] = 0x301;
    QCOMPARE(TerminalTestAccess::cellText(terminal, combining), QStringLiteral("e\u0301"));
}

QTEST_MAIN(PerformanceRegressionTest)
#include "PerformanceRegressionTest.moc"
