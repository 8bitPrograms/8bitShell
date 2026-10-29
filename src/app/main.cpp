#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlIncubationController>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QScreen>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJSValue>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <bitshell/core/Platform.h>
#include "Version.h"
#include <UpdateClient.h>
#ifdef Q_OS_WIN
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

void applyMergedTitleBar(QWindow* window) {
    if (window == nullptr)
        return;
    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    // One pixel of the system frame keeps the shadow and rounded corners
    // after the native title bar is removed.
    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
}
#endif
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

int main(int argc, char* argv[]) {
    QElapsedTimer startupElapsed;
    startupElapsed.start();
    QString platformError;
    if (!bitshell::core::Platform::initializeProcess(platformError)) {
        fprintf(stderr, "%s\n", qPrintable(platformError));
        return EXIT_FAILURE;
    }
#ifdef Q_OS_LINUX
    if (::geteuid() == 0 || ::geteuid() != ::getuid() || ::getegid() != ::getgid()) {
        fputs("8bitShell must run as an unprivileged desktop user, not root or set-id.\n", stderr);
        return EXIT_FAILURE;
    }
    const rlimit noCore{0, 0};
    if (::setrlimit(RLIMIT_CORE, &noCore) != 0 || ::prctl(PR_SET_DUMPABLE, 0) != 0) {
        fputs("Cannot disable process dumps; startup blocked.\n", stderr);
        return EXIT_FAILURE;
    }
#endif
    // Separate the command before Qt processes its own options. Everything after
    // -e/--execute/-- is opaque argv, including flags that resemble app/Qt flags.
    QStringList terminalCommand;
    int guiArgc = argc;
    for (int i = 1; i < argc; ++i) {
        const auto argument = QString::fromLocal8Bit(argv[i]);
        if (argument != QStringLiteral("-e") && argument != QStringLiteral("--execute") && argument != QStringLiteral("--")) continue;
        guiArgc = i;
        for (int commandIndex = i + 1; commandIndex < argc; ++commandIndex) terminalCommand << QString::fromLocal8Bit(argv[commandIndex]);
        if (terminalCommand.isEmpty() || terminalCommand.first().isEmpty()) {
            fputs("Supply a local executable after -e/--execute/--.\n", stderr); return EXIT_FAILURE;
        }
        break;
    }
    std::vector<char*> guiArgv(argv, argv + guiArgc); guiArgv.push_back(nullptr);
    QGuiApplication app(guiArgc, guiArgv.data());

    QCoreApplication::setApplicationName(QStringLiteral("8bitShell"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BIT8SHELL_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("8bitShell"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("8bitPrograms.github.io"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.8bitshell.App"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/qt/qml/BitShell/assets/bitshell.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Programmable remote access workspace"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption(QCommandLineOption({QStringLiteral("e"), QStringLiteral("execute")},
        QStringLiteral("Run a local program with literal arguments; consumes the remaining command line"), QStringLiteral("program [arguments...]")));
    const QCommandLineOption directoryOption(QStringLiteral("working-directory"), QStringLiteral("Initial local terminal directory"), QStringLiteral("directory"));
    const QCommandLineOption titleOption(QStringLiteral("title"), QStringLiteral("Window title"), QStringLiteral("title"));
    parser.addOption(directoryOption); parser.addOption(titleOption);
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a rendered UI screenshot and exit"),
        QStringLiteral("path"));
    const QCommandLineOption panelOption(
        QStringLiteral("panel"),
        QStringLiteral("Select the initial side panel: files, rules, or hidden"),
        QStringLiteral("name"));
    const QCommandLineOption overlayOption(
        QStringLiteral("overlay"),
        QStringLiteral("Open an initial overlay for visual testing"),
        QStringLiteral("name"));
    const QCommandLineOption sidebarFilterOption(
        QStringLiteral("sidebar-filter"),
        QStringLiteral("Set the initial saved-session filter"),
        QStringLiteral("text"));
    const QCommandLineOption windowSizeOption(
        QStringLiteral("window-size"),
        QStringLiteral("Set the initial window size, for example 1280x760"),
        QStringLiteral("widthxheight"));
    parser.addOption(screenshotOption);
    parser.addOption(panelOption);
    parser.addOption(overlayOption);
    parser.addOption(sidebarFilterOption);
    parser.addOption(windowSizeOption);
    const QCommandLineOption performanceReportOption(QStringLiteral("performance-report"),
        QStringLiteral("Write numeric startup diagnostics to a JSON file and exit (local sessions only)"), QStringLiteral("path"));
    const QCommandLineOption performanceTabsOption(QStringLiteral("performance-tabs"),
        QStringLiteral("Local tabs for the performance probe, 0 to 20"), QStringLiteral("count"), QStringLiteral("1"));
    const QCommandLineOption performanceDurationOption(QStringLiteral("performance-duration"),
        QStringLiteral("Probe settling duration, 500 to 30000 milliseconds"), QStringLiteral("milliseconds"), QStringLiteral("3000"));
    parser.addOption(performanceReportOption);
    parser.addOption(performanceTabsOption);
    parser.addOption(performanceDurationOption);
    parser.process(app);
    const bool performanceProbe = parser.isSet(performanceReportOption);
    bool tabsValid = false, durationValid = false;
    const int performanceTabs = parser.value(performanceTabsOption).toInt(&tabsValid);
    const int performanceDuration = parser.value(performanceDurationOption).toInt(&durationValid);
    if ((!performanceProbe && (parser.isSet(performanceTabsOption) || parser.isSet(performanceDurationOption)))
        || (performanceProbe && (!tabsValid || performanceTabs < 0 || performanceTabs > 20
            || !durationValid || performanceDuration < 500 || performanceDuration > 30000
            || parser.value(performanceReportOption).isEmpty() || parser.isSet(screenshotOption)))) {
        fputs("Performance probes require a report path, 0..20 tabs, 500..30000 ms, and no screenshot option.\n", stderr);
        return EXIT_FAILURE;
    }
    if (parser.isSet(QStringLiteral("execute"))) { fputs("Use -e PROGRAM [ARGS...] or --execute PROGRAM [ARGS...], without '='.\n", stderr); return EXIT_FAILURE; }
    if (!parser.positionalArguments().isEmpty()) { fputs("Unexpected arguments. Use -e for a local program.\n", stderr); return EXIT_FAILURE; }
    if (parser.isSet(directoryOption) && !QDir::setCurrent(parser.value(directoryOption))) {
        fputs("Cannot open the requested local working directory.\n", stderr); return EXIT_FAILURE;
    }

    // A single custom visual language across Plasma, other Linux desktops,
    // and the native Windows build.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    UpdateClient updater(QStringLiteral("8bitShell"), QStringLiteral("8bitPrograms/8bitShell"),
        QCoreApplication::applicationVersion(), QDir(bitshell::core::Platform::appConfigDirectory()).filePath(QStringLiteral("updates.ini")));
    QQmlApplicationEngine engine;
    QVariantMap initialProperties{{QStringLiteral("startupCommand"), terminalCommand}};
    initialProperties.insert(QStringLiteral("updater"), QVariant::fromValue(&updater));
    if (parser.isSet(directoryOption))
        initialProperties.insert(QStringLiteral("startupDirectory"), QDir::currentPath());
    if (performanceProbe) {
        initialProperties.insert(QStringLiteral("startupTabCount"), performanceTabs);
        initialProperties.insert(QStringLiteral("performanceProbe"), true);
    }
    if (parser.isSet(panelOption)) {
        const auto panel = parser.value(panelOption);
        const int panelMode = panel == QStringLiteral("files") ? 1 : 0;
        initialProperties.insert(QStringLiteral("inspectorMode"), panelMode);
        if (panel == QStringLiteral("rules"))
            initialProperties.insert(QStringLiteral("startupOverlay"), QStringLiteral("rule"));
    }
    // Select panels before Component.onCompleted so the first frame restores
    // their final position, instead of starting an entrance animation.
    // Leave room for native window borders/taskbars on smaller VM displays.
    if (const auto* screen = QGuiApplication::primaryScreen()) {
        const QSize available = screen->availableGeometry().size();
        initialProperties.insert(QStringLiteral("width"), qBound(1080, available.width() - 32, 1510));
        initialProperties.insert(QStringLiteral("height"), qBound(680, available.height() - 64, 900));
    }
    const QStringList requestedSize = parser.value(windowSizeOption).split(u'x');
    if (requestedSize.size() == 2) {
        bool widthValid = false, heightValid = false;
        const int width = requestedSize.at(0).toInt(&widthValid);
        const int height = requestedSize.at(1).toInt(&heightValid);
        if (widthValid && heightValid && width >= 800 && height >= 560) {
            initialProperties.insert(QStringLiteral("width"), width);
            initialProperties.insert(QStringLiteral("height"), height);
        }
    }
    engine.setInitialProperties(initialProperties);
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(EXIT_FAILURE); },
        Qt::QueuedConnection);
    engine.loadFromModule(QStringLiteral("BitShell"), QStringLiteral("Main"));
    if (!engine.rootObjects().isEmpty()) {
        auto *window = qobject_cast<QWindow *>(engine.rootObjects().first());
        if (window) QObject::connect(&updater, &UpdateClient::closeRequested, window, [&updater, window] {
            if (!window->close()) updater.cancelInstall();
        });
    }
    if (!parser.isSet(screenshotOption) && !performanceProbe) updater.start();
#ifdef Q_OS_WIN
    if (!engine.rootObjects().isEmpty())
        applyMergedTitleBar(qobject_cast<QWindow*>(engine.rootObjects().first()));
#endif
    const qint64 engineReadyMilliseconds = startupElapsed.elapsed();
    qint64 firstFrameMilliseconds = -1;
    if (performanceProbe) {
        auto* window = engine.rootObjects().isEmpty() ? nullptr
            : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return EXIT_FAILURE;
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
            if (firstFrameMilliseconds < 0) firstFrameMilliseconds = startupElapsed.elapsed();
        }, Qt::QueuedConnection);
        const QString reportPath = parser.value(performanceReportOption);
        QTimer::singleShot(performanceDuration, &app, [&, window, reportPath] {
            QVariant sessionDiagnostics;
            if (!QMetaObject::invokeMethod(window, "performanceSessionDiagnostics", Q_RETURN_ARG(QVariant, sessionDiagnostics))) {
                fputs("Cannot collect performance session diagnostics.\n", stderr);
                app.exit(EXIT_FAILURE);
                return;
            }
            if (sessionDiagnostics.metaType() == QMetaType::fromType<QJSValue>())
                sessionDiagnostics = sessionDiagnostics.value<QJSValue>().toVariant();
            const QJsonObject report{
                {QStringLiteral("schema"), 1},
                {QStringLiteral("version"), QStringLiteral(BIT8SHELL_VERSION)},
                {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
                {QStringLiteral("main_to_engine_ready_ms"), engineReadyMilliseconds},
                {QStringLiteral("main_to_first_frame_ms"), firstFrameMilliseconds},
                {QStringLiteral("settled_at_ms"), startupElapsed.elapsed()},
                {QStringLiteral("requested_tabs"), performanceTabs},
                {QStringLiteral("tabs"), window->property("performanceTabCount").toInt()},
                {QStringLiteral("running_local_processes"), window->property("performanceRunningTabs").toInt()},
                {QStringLiteral("sessions"), QJsonArray::fromVariantList(sessionDiagnostics.toList())},
                {QStringLiteral("width"), window->width()}, {QStringLiteral("height"), window->height()}
            };
            QSaveFile file(reportPath);
            const auto bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
                fputs("Cannot write the performance report.\n", stderr);
                app.exit(EXIT_FAILURE);
                return;
            }
            app.exit(EXIT_SUCCESS);
        });
    }

    if (!engine.rootObjects().isEmpty()) {
        QObject* root = engine.rootObjects().first();
        if (parser.isSet(titleOption)) root->setProperty("title", parser.value(titleOption));
        root->setProperty("startupFilter", parser.value(sidebarFilterOption));
        if (parser.isSet(overlayOption))
            root->setProperty("startupOverlay", parser.value(overlayOption));
    }

    QTimer captureTimer;
    QElapsedTimer captureElapsed;
    int renderedFrames = 0;
    if (parser.isSet(screenshotOption)) {
        const auto outputPath = parser.value(screenshotOption);
        auto* window = engine.rootObjects().isEmpty() ? nullptr
            : qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return EXIT_FAILURE;
        QObject::connect(window, &QQuickWindow::frameSwapped, &app,
                         [&renderedFrames] { renderedFrames = qMin(renderedFrames + 1, 3); }, Qt::QueuedConnection);
        captureElapsed.start();
        captureTimer.setInterval(50);
        QObject::connect(&captureTimer, &QTimer::timeout, &app, [&, window, outputPath] {
            if (captureElapsed.elapsed() >= 10000) {
                fputs("Screenshot timed out waiting for a rendered, ready window.\n", stderr);
                captureTimer.stop();
                app.exit(EXIT_FAILURE);
                return;
            }
            const auto* incubation = engine.incubationController();
            const bool loading = incubation && incubation->incubatingObjectCount() != 0;
            bool animating = false;
            for (const auto* name : {"connectionsPanel", "filesPanel"}) {
                const auto* item = window->findChild<QObject*>(QString::fromLatin1(name));
                animating |= item && item->property("animating").toBool();
            }
            if (captureElapsed.elapsed() < 750 || loading || animating || renderedFrames < 2) {
                window->update();
                return;
            }
            captureTimer.stop();
            if (!window->grabWindow().save(outputPath)) {
                app.exit(EXIT_FAILURE);
                return;
            }
            app.exit(EXIT_SUCCESS);
        });
        captureTimer.start();
    }

    return app.exec();
}
