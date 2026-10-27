#include "TerminalTestAccess.h"
#include <bitshell/core/SessionStore.h>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <sys/resource.h>

using bitshell::terminal::TerminalView;
using bitshell::terminal::TerminalTestAccess;

template<class Function>
QJsonObject measure(const char* name, int iterations, Function operation) {
    QJsonArray samples;
    QList<double> values;
    for (int repeat = 0; repeat < 3; ++repeat) {
        QElapsedTimer timer;
        timer.start();
        for (int index = 0; index < iterations; ++index) operation(index);
        const double ms = double(timer.nsecsElapsed()) / 1000000.0 / iterations;
        values << ms;
        samples.append(ms);
    }
    std::sort(values.begin(), values.end());
    return {{QStringLiteral("name"), QString::fromLatin1(name)}, {QStringLiteral("iterations_per_sample"), iterations},
        {QStringLiteral("samples_ms_per_op"), samples}, {QStringLiteral("median_ms_per_op"), values[1]}};
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir config;
    if (!config.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
    QCoreApplication::setApplicationName(QStringLiteral("bitshell-performance-fixture"));
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QJsonArray results;
    TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setFontFamily(QStringLiteral("DejaVu Sans Mono"));
    terminal.setWidth(1000);
    terminal.setHeight(600);
    const QByteArray line("\x1b[31mERROR\x1b[0m host 192.0.2.5 status running: deterministic terminal output 0123456789\r\n");
    QByteArray history;
    for (int i = 0; i < 2000; ++i) history += line;
    TerminalTestAccess::feed(terminal, history);
    QImage frame(1000, 600, QImage::Format_ARGB32_Premultiplied);
    const auto paint = [&](int) { QPainter painter(&frame); terminal.paint(&painter); };
    paint(0);
    results.append(measure("terminal_static_paint", 25, paint));
    results.append(measure("terminal_single_cell_update_paint", 25, [&](int i) {
        TerminalTestAccess::feed(terminal, QByteArray("\x1b[1;1H") + char('a' + i % 26));
        paint(i);
    }));
    results.append(measure("terminal_search_2000_lines", 3, [&](int i) {
        terminal.setSearchText(i % 2 ? QStringLiteral("host") : QStringLiteral("ERROR"));
    }));
    terminal.setSearchText(QStringLiteral("ERROR"));
    terminal.scrollToBottom();
    results.append(measure("terminal_search_paint", 5, paint));
    TerminalTestAccess::feed(terminal, QByteArray("\x1b[999;1H"));
    results.append(measure("terminal_search_open_append", 20, [&](int) { TerminalTestAccess::feed(terminal, line); }));
    terminal.clearSearch();
    QByteArray chunk;
    while (chunk.size() < 4096) chunk += line;
    results.append(measure("terminal_parse_4k_chunk", 128, [&](int) { TerminalTestAccess::feed(terminal, chunk); }));

    bitshell::core::SessionStore catalog;
    QFile fixture(config.filePath(QStringLiteral("catalog.yaml")));
    if (!fixture.open(QIODevice::WriteOnly)) return 2;
    fixture.write("version: 1\nkind: 8bitshell/sessions\nfolders: []\nsessions:\n");
    for (int index = 0; index < 5000; ++index) {
        fixture.write(QStringLiteral("  - id: s%1\n    name: node-%1\n    host: node%1.example.test\n    folder: Fleet/Rack%2\n").arg(index).arg(index / 10).toUtf8());
    }
    fixture.close();
    if (catalog.importYaml(fixture.fileName()) != 5000) { qCritical("Catalog fixture failed"); return 2; }
    qsizetype count = 0;
    results.append(measure("catalog_5000_collapsed", 10, [&](int) { count = catalog.treeRows({}, {}).size(); }));
    results.append(measure("catalog_5000_expand_root", 10, [&](int) { count = catalog.treeRows({}, {QStringLiteral("Fleet")}).size(); }));
    results.append(measure("catalog_5000_common_filter", 1, [&](int) { count = catalog.treeRows(QStringLiteral("Rack"), {}).size(); }));
    results.append(measure("catalog_5000_selective_filter", 5, [&](int i) { count = catalog.treeRows(QStringLiteral("node-%1").arg(i), {}).size(); }));
    results.append(measure("catalog_5000_ranked_dropdown_common", 10, [&](int) { count = catalog.search(QStringLiteral("node"))[QStringLiteral("results")].toList().size(); }));
    results.append(measure("catalog_5000_ranked_dropdown_selective", 10, [&](int i) { count = catalog.search(QStringLiteral("node-%1 Rack0").arg(i))[QStringLiteral("results")].toList().size(); }));

    std::vector<std::unique_ptr<TerminalView>> hidden;
    quint64 updates = 0;
    for (int i = 0; i < 20; ++i) {
        auto item = std::make_unique<TerminalView>();
        item->setVisible(false);
        updates += TerminalTestAccess::sceneUpdates(*item);
        hidden.push_back(std::move(item));
    }
    QElapsedTimer idle;
    idle.start();
    while (idle.elapsed() < 1200) { QCoreApplication::processEvents(); QThread::msleep(10); }
    quint64 wakeups = 0;
    for (const auto& item : hidden) wakeups += TerminalTestAccess::sceneUpdates(*item);
    wakeups -= updates;
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    const QJsonObject output{{QStringLiteral("schema"), 1}, {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("fixture"), QStringLiteral("isolated synthetic, no network; logging off; 1000x600 software paint")},
        {QStringLiteral("results"), results}, {QStringLiteral("hidden_20_scene_updates_1200ms"), qint64(wakeups)},
        {QStringLiteral("max_rss_kib_whole_fixture"), qint64(usage.ru_maxrss)},
        {QStringLiteral("last_catalog_result_count"), qint64(count)}, {QStringLiteral("terminal_columns"), terminal.columns()},
        {QStringLiteral("terminal_rows"), terminal.rows()}};
    const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Indented);
    fwrite(bytes.data(), 1, size_t(bytes.size()), stdout);
    return 0;
}
