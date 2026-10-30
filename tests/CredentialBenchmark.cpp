// Opt-in measurement: cost of credential prompt matching on the output path.
// Prints JSON. Builds without the feature too (base comparison): the armed
// rows only exist when CredentialFlow is present.
#include "TerminalTestAccess.h"
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <cstdio>
#if __has_include(<bitshell/core/CredentialFlow.h>)
#define BENCH_CREDENTIALS 1
#include <bitshell/core/CredentialFlow.h>
#include "FakeSecretVault.h"
using Vault = bitshell::core::SecretVault;
#else
using Vault = void;
#endif

using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

namespace {
template<class Function>
QJsonObject perOp(const char* name, int iterations, int repeats, Function operation) {
    QList<double> values;
    QJsonArray samples;
    for (int repeat = 0; repeat < repeats; ++repeat) {
        QElapsedTimer timer; timer.start();
        for (int index = 0; index < iterations; ++index) operation(index);
        const double ns = double(timer.nsecsElapsed()) / iterations;
        values << ns; samples.append(ns);
    }
    std::sort(values.begin(), values.end());
    return {{QStringLiteral("name"), QString::fromLatin1(name)}, {QStringLiteral("ns_per_op_samples"), samples},
        {QStringLiteral("ns_per_op_median"), values[values.size() / 2]},
        {QStringLiteral("ns_per_op_min"), values.first()}, {QStringLiteral("ns_per_op_max"), values.last()}};
}

void configure(TerminalView& view) {
    view.setLogEnabled(false);
    view.setFontFamily(QStringLiteral("DejaVu Sans Mono"));
    view.setWidth(1000); view.setHeight(600);
}

// Wall time for a script's whole output to pass through a real PTY and
// TerminalView, until the process exits.
double floodMs(const QString& script, bool armed, const QString& credential, Vault* vault, const QString& path, bool* stayedArmed) {
    TerminalView view; configure(view);
    QString error;
    QElapsedTimer timer; timer.start();
    if (!TerminalTestAccess::startFixture(view, script, error)) { fprintf(stderr, "%s\n", qPrintable(error)); return -1; }
#ifdef BENCH_CREDENTIALS
    if (armed) {
        TerminalTestAccess::useCredentials(view, *vault, path);
        if (!view.bindCredential(credential)) { fprintf(stderr, "bind failed: %s\n", qPrintable(view.statusText())); return -1; }
    }
#else
    Q_UNUSED(armed); Q_UNUSED(credential); Q_UNUSED(vault); Q_UNUSED(path);
#endif
    while (view.isRunning()) {
#ifdef BENCH_CREDENTIALS
        // Checked between event batches; exit releases the matcher, so only while running.
        if (stayedArmed && !TerminalTestAccess::credentialArmed(view)) *stayedArmed = false;
#else
        Q_UNUSED(stayedArmed);
#endif
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return double(timer.nsecsElapsed()) / 1e6;
}
}

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    qputenv("XDG_CONFIG_HOME", root.path().toUtf8());
    qputenv("BIT8SHELL_PROFILE", root.filePath(QStringLiteral("profile")).toUtf8());
    // Usage: credential_benchmark [flood runs] [yes MB] [cat MiB]
    const int floodRuns = argc > 1 ? std::max(1, atoi(argv[1])) : 5;
    const int yesMb = argc > 2 ? std::max(1, atoi(argv[2])) : 200;
    const int catMb = argc > 3 ? std::max(1, atoi(argv[3])) : 200;
    QJsonArray results;
    QElapsedTimer total; total.start();
    const auto add = [&](const QJsonObject& row) {
        results.append(row);
        fprintf(stderr, "[%6.1fs] %s\n", total.elapsed() / 1000.0, qPrintable(row.value(QStringLiteral("name")).toString()));
    };

    // A realistic 4 KiB chunk: coloured log lines, ending mid-line.
    QByteArray chunk;
    const QByteArray line("\x1b[32mINFO\x1b[0m 2026-10-30 worker-7 processed request id=0123456789 in 12ms\r\n");
    while (chunk.size() < 4096) chunk += line;
    chunk.truncate(4096);
    // Worst case for the matcher: every chunk ends like a prompt.
    QByteArray promptChunk = chunk.left(4096 - 24) + "\r\nAre you sure (yes/no)? ";

    QString credential, path;
    Vault* vault = nullptr;
#ifdef BENCH_CREDENTIALS
    using bitshell::core::CredentialFlow;
    using bitshell::core::CredentialSecrets;
    static FakeSecretVault fake;
    vault = &fake;
    path = root.filePath(QStringLiteral("credentials.yaml"));
    {
        bitshell::core::CredentialStore store(fake, path);
        credential = store.saveCredential({{QStringLiteral("name"), QStringLiteral("bench")},
            {QStringLiteral("type"), QStringLiteral("password")}, {QStringLiteral("username"), QStringLiteral("jdoe")},
            {QStringLiteral("password"), QStringLiteral("bench-secret")}});
        if (credential.isEmpty()) { fprintf(stderr, "%s\n", qPrintable(store.lastError())); return 3; }
    }
    {
        CredentialFlow flow(CredentialSecrets{"jdoe", "pw", {}, {}}, CredentialFlow::Scope::Shell);
        add((perOp("flow_feed_4k_log_chunk", 200000, 7, [&](int) { (void)flow.feed(QByteArrayView(chunk)); })));
        add((perOp("flow_feed_4k_prompt_like_chunk", 200000, 7, [&](int) { (void)flow.feed(QByteArrayView(promptChunk)); })));
        add((perOp("flow_feed_64b_chunk", 1000000, 7, [&](int) { (void)flow.feed(QByteArrayView(line)); })));
    }
#endif

    // TerminalView output path, same synthetic chunk, matcher off vs armed.
    for (int armed = 0; armed < 2; ++armed) {
#ifndef BENCH_CREDENTIALS
        if (armed) break;
#endif
        TerminalView view; configure(view);
        QString error;
        if (!TerminalTestAccess::startFixture(view, QStringLiteral("sleep 600"), error)) return 4;
#ifdef BENCH_CREDENTIALS
        if (armed) {
            TerminalTestAccess::useCredentials(view, *vault, path);
            if (!view.bindCredential(credential)) return 5;
        }
#endif
        for (int i = 0; i < 2000; ++i) TerminalTestAccess::feed(view, chunk); // warm, fill scrollback
        add((perOp(armed ? "terminal_feed_4k_armed" : "terminal_feed_4k_off", 4000, 7,
            [&](int) { TerminalTestAccess::feed(view, chunk); })));
        TerminalTestAccess::stopFixture(view);
    }

    // Floods through a real PTY: `yes | head -c 200M` and cat of a 200 MB file.
    const QString big = root.filePath(QStringLiteral("big.log"));
    {
        QFile file(big);
        if (!file.open(QIODevice::WriteOnly)) return 6;
        QByteArray block;
        while (block.size() < (1 << 20)) block += line;
        for (int i = 0; i < catMb; ++i) file.write(block.left(1 << 20));
    }
    const QList<QPair<const char*, QString>> floods{
        {"yes_head", QStringLiteral("stty raw -echo; yes | head -c %1").arg(qint64(yesMb) * 1000000)},
        {"cat_log", QStringLiteral("stty raw -echo; cat '%1'").arg(big)}};
    // Off and armed runs alternate so drift on a shared box hits both alike.
    for (const auto& [name, script] : floods) {
        const int megabytes = QByteArrayView(name) == "yes_head" ? yesMb : catMb;
        QJsonArray samples[2]; QList<double> values[2];
        bool stayedArmed = true;
        for (int run = 0; run < floodRuns; ++run) {
            for (int armed = 0; armed < 2; ++armed) {
#ifndef BENCH_CREDENTIALS
                if (armed) break;
#endif
                const double ms = floodMs(script, armed, credential, vault, path, armed ? &stayedArmed : nullptr);
                samples[armed].append(ms); values[armed] << ms;
            }
        }
        for (int armed = 0; armed < 2; ++armed) {
            if (values[armed].isEmpty()) continue;
            std::sort(values[armed].begin(), values[armed].end());
            QJsonObject row{{QStringLiteral("name"), QString::fromLatin1(name) + (armed ? QStringLiteral("_armed") : QStringLiteral("_off"))},
                {QStringLiteral("ms_samples"), samples[armed]}, {QStringLiteral("ms_median"), values[armed][values[armed].size() / 2]},
                {QStringLiteral("ms_min"), values[armed].first()}, {QStringLiteral("ms_max"), values[armed].last()},
                {QStringLiteral("megabytes"), megabytes}};
            if (armed) row.insert(QStringLiteral("matcher_armed_throughout"), stayedArmed);
            add(row);
        }
    }

    const QJsonObject output{{QStringLiteral("qt"), QString::fromLatin1(qVersion())},
#ifdef BENCH_CREDENTIALS
        {QStringLiteral("build"), QStringLiteral("feature")},
#else
        {QStringLiteral("build"), QStringLiteral("base")},
#endif
        {QStringLiteral("results"), results}};
    const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Indented);
    fwrite(bytes.data(), 1, size_t(bytes.size()), stdout);
    return 0;
}
