#include <QtTest>

#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlError>
#include <QQmlIncubationController>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/terminal/TerminalView.h>
#include "TerminalTestAccess.h"

#include <memory>

using bitshell::core::AppSettings;
using bitshell::core::LocalProfileStore;
using bitshell::core::SessionStore;
using bitshell::terminal::TerminalTestAccess;
using bitshell::terminal::TerminalView;

namespace {
QVariant nativeVariant(const QVariant& value) {
    return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value;
}
QVariantMap savedSession(const QString& id, const QString& host, const QString& user) {
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), id},
        {QStringLiteral("host"), host}, {QStringLiteral("user"), user},
        {QStringLiteral("port"), 22}, {QStringLiteral("identityKey"), QStringLiteral("none")}};
}
}

class CommandPaletteTest final : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void init();
    void cleanup();
    void shortcutFindsDuplicateIpTabsAndSwitchesByKeyboard();
    void exactDestinationsRankBeforeSubstringMatches();
    void openResultsTrackReorderedAndClosedTabs();
    void savedAndNewConnectionRowsUseFreshCatalogData();
    void unavailableSavedRouteDoesNotSelectDirectSshAutomatically();
    void queryPresetKeepsSearchFocus();
    void settingsTogglesAndThemesWorkAfterClosingThePalette();
    void localLaunchersIncludeWslWithoutStartingIt();
    void escapeRestoresFocusAndReviewDialogsBlockTheShortcut();
private:
    QVariantList fixtureCommand() const;
    QObject* named(const QString& name) const { return m_window ? m_window->findChild<QObject*>(name) : nullptr; }
    QObject* palette() const { return named(QStringLiteral("commandPalette")); }
    QVariantList rows() const { return palette() ? nativeVariant(palette()->property("results")).toList() : QVariantList{}; }
    QVariantMap row(const QString& id) const {
        for (const auto& value : rows()) if (value.toMap().value(QStringLiteral("id")) == id) return value.toMap();
        return {};
    }
    QVariantList rowsOfType(const QString& type) const {
        QVariantList result;
        for (const auto& value : rows()) if (value.toMap().value(QStringLiteral("type")) == type) result.append(value);
        return result;
    }
    TerminalView* activeTerminal() const {
        return qobject_cast<TerminalView*>(m_pane->property("activeTerminal").value<QObject*>());
    }
    bool showPalette(const QString& query);
    bool choose(const QString& id);
    bool addFixtureTab();
    bool renameTab(int index, const QString& name) {
        return QMetaObject::invokeMethod(m_pane, "renameTab", Q_ARG(QVariant, index), Q_ARG(QVariant, name));
    }
    void capture(const QString& fileName) const;

    QTemporaryDir m_profile;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
    QObject* m_pane = nullptr;
    AppSettings* m_settings = nullptr;
    SessionStore* m_sessions = nullptr;
    LocalProfileStore* m_localProfiles = nullptr;
    QStringList m_warnings;
};

QVariantList CommandPaletteTest::fixtureCommand() const {
#ifdef Q_OS_WIN
    return {QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe"), QStringLiteral("broadcast")};
#else
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
        QStringLiteral("printf 'PALETTE_READY\\n'; while IFS= read -r line; do :; done")};
#endif
}

void CommandPaletteTest::initTestCase() {
    QVERIFY(m_profile.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("command-palette-test"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
}

void CommandPaletteTest::init() {
    const auto directory = m_profile.filePath(QString::fromLatin1(QTest::currentTestFunction()));
    QVERIFY(QDir().mkpath(directory));
    qputenv("BIT8SHELL_PROFILE", directory.toUtf8());
    qputenv("XDG_CONFIG_HOME", directory.toUtf8());
    qputenv("XDG_CACHE_HOME", QDir(directory).filePath(QStringLiteral("cache")).toUtf8());
    m_warnings.clear();
    m_engine = std::make_unique<QQmlApplicationEngine>();
    connect(m_engine.get(), &QQmlEngine::warnings, this, [this](const QList<QQmlError>& warnings) {
        for (const auto& warning : warnings) m_warnings.append(warning.toString());
    });
    // Start only a harmless local fixture. Tests never invoke an SSH server or WSL.
    m_engine->setInitialProperties({{QStringLiteral("startupCommand"), fixtureCommand()}});
    m_engine->loadFromModule(QStringLiteral("BitShell"), QStringLiteral("Main"));
    QVERIFY2(!m_engine->rootObjects().isEmpty(), qPrintable(m_warnings.join(u'\n')));
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    QVERIFY(m_window);
    m_window->setWidth(1080); m_window->setHeight(680);
    m_window->show(); m_window->requestActivate();
    m_pane = named(QStringLiteral("terminalPane"));
    QVERIFY(m_pane);
    m_settings = m_engine->singletonInstance<AppSettings*>("BitShell", "AppSettings");
    m_sessions = m_engine->singletonInstance<SessionStore*>("BitShell", "SessionStore");
    m_localProfiles = m_engine->singletonInstance<LocalProfileStore*>("BitShell", "LocalProfileStore");
    QVERIFY(m_settings && m_sessions && m_localProfiles);
    m_settings->setLogByDefault(false);
    QTRY_COMPARE(m_pane->property("tabCount").toInt(), 1);
    QTRY_VERIFY(activeTerminal() && activeTerminal()->isRunning());
    activeTerminal()->setLogEnabled(false);
    activeTerminal()->forceActiveFocus();
    QTRY_VERIFY(activeTerminal()->hasActiveFocus());
}

void CommandPaletteTest::cleanup() {
    QCoreApplication::processEvents();
    if (m_engine && m_engine->incubationController())
        QTRY_COMPARE(m_engine->incubationController()->incubatingObjectCount(), 0);
    if (m_window) m_window->close();
    m_engine.reset();
    m_window = nullptr; m_pane = nullptr; m_settings = nullptr; m_sessions = nullptr; m_localProfiles = nullptr;
    QVERIFY2(m_warnings.isEmpty(), qPrintable(m_warnings.join(u'\n')));
}

bool CommandPaletteTest::showPalette(const QString& query) {
    if (palette() && palette()->property("visible").toBool()) {
        palette()->setProperty("query", query);
        return QMetaObject::invokeMethod(palette(), "focusSearch");
    }
    if (!QMetaObject::invokeMethod(m_window, "openCommandPalette", Q_ARG(QVariant, query))) return false;
    if (!QTest::qWaitFor([this] { return palette() && palette()->property("opened").toBool(); }, 5000)) return false;
    // Settle the opener's deferred focus/select-all before simulating the next
    // human key press. Popup.opened itself can become true synchronously.
    QCoreApplication::processEvents();
    return true;
}

bool CommandPaletteTest::choose(const QString& id) {
    if (!palette() || !palette()->property("opened").toBool() || row(id).isEmpty()) return false;
    QMetaObject::invokeMethod(palette(), "focusSearch");
    const int count = rows().size();
    for (int attempt = 0; attempt <= count && palette()->property("selectedId").toString() != id; ++attempt)
        QTest::keyClick(m_window, Qt::Key_Down);
    if (palette()->property("selectedId").toString() != id) return false;
    QTest::keyClick(m_window, Qt::Key_Return);
    return QTest::qWaitFor([this] { return !palette()->property("visible").toBool(); }, 5000);
}

bool CommandPaletteTest::addFixtureTab() {
    const int before = m_pane->property("tabCount").toInt();
    const auto* previous = activeTerminal();
    if (!QMetaObject::invokeMethod(m_pane, "newCommandSession", Q_ARG(QVariant, fixtureCommand()))) return false;
    return QTest::qWaitFor([this, before, previous] {
        return m_pane->property("tabCount").toInt() == before + 1 && activeTerminal()
            && activeTerminal() != previous && activeTerminal()->isRunning();
    }, 5000);
}

void CommandPaletteTest::capture(const QString& fileName) const {
    const auto directory = qEnvironmentVariable("BIT8SHELL_UI_TEST_CAPTURE_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QTest::qWait(80);
    const auto frame = m_window->grabWindow();
    QVERIFY(!frame.isNull());
    QVERIFY(frame.save(QDir(directory).filePath(fileName)));
}

void CommandPaletteTest::shortcutFindsDuplicateIpTabsAndSwitchesByKeyboard() {
    QVERIFY(!palette()); // The palette does not add a startup popup tree.
    auto* first = activeTerminal();
    QVERIFY(TerminalTestAccess::startManagedProfileFixture(*first, savedSession(QStringLiteral("a"), QStringLiteral("192.0.2.25"), QStringLiteral("ops"))));
    QVERIFY(renameTab(0, QStringLiteral("Primary database")));
    const QString firstId = m_pane->property("activeTabId").toString();
    QVERIFY(addFixtureTab());
    auto* second = activeTerminal();
    QVERIFY(second != first);
    auto other = savedSession(QStringLiteral("b"), QStringLiteral("192.0.2.25"), QStringLiteral("deploy"));
    other[QStringLiteral("port")] = 2222;
    QVERIFY(TerminalTestAccess::startManagedProfileFixture(*second, other));
    QVERIFY(renameTab(1, QStringLiteral("Deployment session")));
    const QString secondId = m_pane->property("activeTabId").toString();
    QCOMPARE(m_settings->keyBindings().value(QStringLiteral("commandPalette")).toString(), QStringLiteral("Ctrl+Shift+P"));
    second->forceActiveFocus();
    QTest::keyClick(m_window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    QTRY_VERIFY(palette() && palette()->property("opened").toBool());
    auto* field = qobject_cast<QQuickItem*>(named(QStringLiteral("commandPaletteSearch")));
    QVERIFY(field);
    QTRY_VERIFY(field->hasActiveFocus());
    for (const char key : QByteArrayLiteral("192.0.2.25")) QTest::keyClick(m_window, key);
    QTRY_COMPARE(palette()->property("query").toString(), QStringLiteral("192.0.2.25"));
    QTRY_COMPARE(rowsOfType(QStringLiteral("tab")).size(), 2);
    QCOMPARE(rows().first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("tab:") + firstId);
    QVERIFY(!row(QStringLiteral("connect:new")).isEmpty());
    capture(QStringLiteral("command-palette-open-ip-tabs.png"));
    QVERIFY(choose(QStringLiteral("tab:") + firstId));
    QTRY_COMPARE(m_pane->property("activeTabId").toString(), firstId);
    QTRY_VERIFY(first->hasActiveFocus());
    QCOMPARE(m_pane->property("tabCount").toInt(), 2);
    QVERIFY(showPalette(QStringLiteral("192.0.2.25")));
    QTRY_VERIFY(!row(QStringLiteral("tab:") + secondId).isEmpty());
    QVERIFY(choose(QStringLiteral("tab:") + secondId));
    QTRY_COMPARE(m_pane->property("activeTabId").toString(), secondId);
    QCOMPARE(activeTerminal(), second);
}

void CommandPaletteTest::exactDestinationsRankBeforeSubstringMatches() {
    auto* actions = named(QStringLiteral("commandPaletteActions"));
    QVERIFY(actions);
    const auto tab = [](const QString& id, const QString& host) {
        return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("name"), id},
            {QStringLiteral("host"), host}, {QStringLiteral("user"), QStringLiteral("ops")},
            {QStringLiteral("port"), 22}, {QStringLiteral("running"), true},
            {QStringLiteral("active"), false}, {QStringLiteral("gateway"), QString{}},
            {QStringLiteral("localName"), QString{}}};
    };
    const QVariantList tabs{tab(QStringLiteral("partial"), QStringLiteral("10.0.0.10")),
        tab(QStringLiteral("exact"), QStringLiteral("10.0.0.1")),
        tab(QStringLiteral("v6"), QStringLiteral("2001:0db8:0:0:0:0:0:1"))};
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(actions, "search", Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, QStringLiteral("10.0.0.1")), Q_ARG(QVariant, tabs)));
    auto matches = nativeVariant(result).toList();
    QVERIFY(!matches.isEmpty());
    QCOMPARE(matches.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("tab:exact"));
    QVERIFY(QMetaObject::invokeMethod(actions, "search", Q_RETURN_ARG(QVariant, result),
        Q_ARG(QVariant, QStringLiteral("[2001:db8::1]")), Q_ARG(QVariant, tabs)));
    matches = nativeVariant(result).toList();
    QVERIFY(!matches.isEmpty());
    QCOMPARE(matches.first().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("tab:v6"));
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
}

void CommandPaletteTest::openResultsTrackReorderedAndClosedTabs() {
    QVERIFY(TerminalTestAccess::startManagedProfileFixture(*activeTerminal(), savedSession(QStringLiteral("a"), QStringLiteral("192.0.2.26"), QStringLiteral("ops"))));
    const QString first = m_pane->property("activeTabId").toString();
    QVERIFY(addFixtureTab());
    QVERIFY(TerminalTestAccess::startManagedProfileFixture(*activeTerminal(), savedSession(QStringLiteral("b"), QStringLiteral("192.0.2.26"), QStringLiteral("deploy"))));
    const QString second = m_pane->property("activeTabId").toString();
    QVERIFY(showPalette(QStringLiteral("192.0.2.26")));
    QTRY_COMPARE(rowsOfType(QStringLiteral("tab")).size(), 2);
    QVERIFY(QMetaObject::invokeMethod(m_pane, "moveTabById", Q_ARG(QVariant, second), Q_ARG(QVariant, 0)));
    QTRY_COMPARE(rowsOfType(QStringLiteral("tab")).first().toMap().value(QStringLiteral("target")).toString(), second);
    QVERIFY(choose(QStringLiteral("tab:") + first));
    QTRY_COMPARE(m_pane->property("activeTabId").toString(), first);
    QCOMPARE(m_pane->property("currentIndex").toInt(), 1);
    QVERIFY(showPalette(QStringLiteral("192.0.2.26")));
    QVERIFY(QMetaObject::invokeMethod(m_pane, "closeTab", Q_ARG(QVariant, 0)));
    QTRY_VERIFY(row(QStringLiteral("tab:") + second).isEmpty());
    QTRY_COMPARE(rowsOfType(QStringLiteral("tab")).size(), 1);
    QVERIFY(choose(QStringLiteral("tab:") + first));
    QCOMPARE(m_pane->property("activeTabId").toString(), first);
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(m_window, "executePaletteAction", Q_ARG(QVariant, QStringLiteral("tab:") + second),
                                    Q_ARG(QVariant, QStringLiteral("192.0.2.26"))));
    QCOMPARE(m_pane->property("activeTabId").toString(), first);
}

void CommandPaletteTest::savedAndNewConnectionRowsUseFreshCatalogData() {
    QCOMPARE(m_sessions->addSession(savedSession(QStringLiteral("database"), QStringLiteral("198.51.100.17"), QStringLiteral("oracle"))), 0);
    QVERIFY(showPalette(QStringLiteral("198.51.100.17")));
    QTRY_VERIFY(!row(QStringLiteral("saved:database")).isEmpty());
    QVERIFY(!row(QStringLiteral("connect:new")).isEmpty());
    QVERIFY(!row(QStringLiteral("save:new")).isEmpty());
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
    const auto originalId = m_pane->property("activeTabId").toString();
    QVERIFY(m_sessions->updateSession(0, {{QStringLiteral("name"), QStringLiteral("Renamed database")}}));
    QTRY_COMPARE(row(QStringLiteral("saved:database")).value(QStringLiteral("title")).toString(), QStringLiteral("Renamed database"));
    QVERIFY(m_sessions->removeSession(0));
    QTRY_VERIFY(row(QStringLiteral("saved:database")).isEmpty());
    QVERIFY(QMetaObject::invokeMethod(palette(), "close"));
    QTRY_VERIFY(!palette()->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(m_window, "executePaletteAction", Q_ARG(QVariant, QStringLiteral("saved:database")),
                                    Q_ARG(QVariant, QStringLiteral("198.51.100.17"))));
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
    QCOMPARE(m_pane->property("activeTabId").toString(), originalId);

    // Creating a saved entry opens the prefilled editor; it never connects.
    QVERIFY(showPalette(QStringLiteral("oracle@198.51.100.17")));
    QVERIFY(choose(QStringLiteral("save:new")));
    auto* editor = named(QStringLiteral("sessionEditorDialog"));
    QVERIFY(editor);
    QTRY_VERIFY(editor->property("visible").toBool());
    QVERIFY(named(QStringLiteral("sessionHostField")) && named(QStringLiteral("sessionUserField")));
    QCOMPARE(named(QStringLiteral("sessionHostField"))->property("text").toString(), QStringLiteral("198.51.100.17"));
    QCOMPARE(named(QStringLiteral("sessionUserField"))->property("text").toString(), QStringLiteral("oracle"));
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
    QCOMPARE(m_sessions->count(), 0);
    QVERIFY(QMetaObject::invokeMethod(editor, "reject"));
}

void CommandPaletteTest::unavailableSavedRouteDoesNotSelectDirectSshAutomatically() {
    const QString host = QStringLiteral("198.51.100.18");
    const QVariantMap route{{QStringLiteral("type"), QStringLiteral("cyberark-psmp")},
        {QStringLiteral("name"), QStringLiteral("Required gateway")},
        {QStringLiteral("host"), QStringLiteral("psmp.example.test")}, {QStringLiteral("port"), 22},
        {QStringLiteral("account"), QVariantMap{{QStringLiteral("source"), QStringLiteral("literal")},
                                               {QStringLiteral("name"), QStringLiteral("oracle")}}}};
    QVERIFY(m_sessions->saveRoute(QStringLiteral("required"), route));
    auto saved = savedSession(QStringLiteral("routed"), host, QStringLiteral("oracle"));
    saved[QStringLiteral("connection")] = QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")},
                                                      {QStringLiteral("route"), QStringLiteral("required")}};
    QCOMPARE(m_sessions->addSession(saved), 0);
    QVERIFY(m_sessions->removeRoute(QStringLiteral("required")));
    QVERIFY(showPalette(host));
    QTRY_VERIFY(!row(QStringLiteral("saved:routed")).isEmpty());
    QVERIFY(!row(QStringLiteral("saved:routed")).value(QStringLiteral("enabled")).toBool());
    QVERIFY(!row(QStringLiteral("connect:new")).isEmpty());
    QVERIFY(!row(QStringLiteral("connect:new")).value(QStringLiteral("autoSelect"), true).toBool());
    QCOMPARE(palette()->property("currentIndex").toInt(), -1);
    QCOMPARE(palette()->property("selectedId").toString(), QString{});

    // The assertions above stop before Enter if a connection is selected.
    // Observe the key with currentIndex=-1; no network row is ever activated.
    QSignalSpy chosen(palette(), SIGNAL(chosen(QString)));
    QVERIFY(chosen.isValid());
    QTest::keyClick(m_window, Qt::Key_Return);
    QCoreApplication::processEvents();
    QCOMPARE(chosen.count(), 0);
    QVERIFY(palette()->property("opened").toBool());
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
    QVERIFY(activeTerminal()->currentSshConnection().isEmpty());

    QTest::keyClick(m_window, Qt::Key_Down);
    QTRY_COMPARE(palette()->property("selectedId").toString(), QStringLiteral("connect:new"));
    QCOMPARE(chosen.count(), 0); // Navigation alone does not connect.
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!palette()->property("visible").toBool());
}

void CommandPaletteTest::queryPresetKeepsSearchFocus() {
    QVERIFY(showPalette(QStringLiteral(">change theme")));
    QTRY_VERIFY(!row(QStringLiteral("theme:choose")).isEmpty());
    QCOMPARE(palette()->property("selectedId").toString(), QStringLiteral("theme:choose"));
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_VERIFY(palette()->property("opened").toBool());
    QTRY_COMPARE(palette()->property("query").toString(), QStringLiteral("theme "));
    // Allow both close restoration and the reopened search's queued focus work
    // to finish, then verify typing still reaches the palette.
    QCoreApplication::processEvents();
    QTRY_VERIFY(m_window->activeFocusItem());
    QTRY_COMPARE(m_window->activeFocusItem()->objectName(), QStringLiteral("commandPaletteSearch"));
    QTest::keyClick(m_window, Qt::Key_A, Qt::ControlModifier);
    for (const char key : QByteArrayLiteral("theme dracula")) QTest::keyClick(m_window, key);
    QTRY_COMPARE(palette()->property("query").toString(), QStringLiteral("theme dracula"));
    QTRY_VERIFY(!row(QStringLiteral("ui-theme:dracula")).isEmpty());
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
}

void CommandPaletteTest::settingsTogglesAndThemesWorkAfterClosingThePalette() {
    QVERIFY(showPalette(QStringLiteral("settings appearance")));
    QTRY_VERIFY(!row(QStringLiteral("settings:Appearance")).isEmpty());
    QVERIFY(choose(QStringLiteral("settings:Appearance")));
    auto* drawer = named(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    QTRY_VERIFY(drawer->property("opened").toBool());
    QCOMPARE(drawer->property("currentSection").toInt(), 1);
    QVERIFY(!palette()->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    const bool sidebar = m_settings->sidebarVisible();
    QVERIFY(showPalette(QStringLiteral("sidebar")));
    QVERIFY(choose(QStringLiteral("sidebar:toggle")));
    QCOMPARE(m_settings->sidebarVisible(), !sidebar);
    QVERIFY(m_settings->reload());
    QCOMPARE(m_settings->sidebarVisible(), !sidebar);

    QVERIFY(!m_settings->bonusThemesUnlocked());
    QVERIFY(showPalette(QStringLiteral("silverhand")));
    QVERIFY(row(QStringLiteral("ui-theme:cyberpunk-silverhand")).isEmpty());
    for (int n = 0; n < 7; ++n) m_settings->registerVersionTap();
    QTRY_VERIFY(!row(QStringLiteral("ui-theme:cyberpunk-silverhand")).isEmpty());
    m_settings->setAppearanceMode(QStringLiteral("dark"));
    m_settings->setSeparateTerminalTheme(false);
    QVERIFY(choose(QStringLiteral("ui-theme:cyberpunk-silverhand")));
    QCOMPARE(m_settings->effectiveUiThemeId(), QStringLiteral("cyberpunk-silverhand"));
    QCOMPARE(m_settings->effectiveTerminalThemeId(), QStringLiteral("cyberpunk-silverhand"));
    QTRY_COMPARE(activeTerminal()->colorScheme().value(QStringLiteral("background")).toString(), QStringLiteral("#0b0d16"));
    QCOMPARE(activeTerminal()->colorScheme().value(QStringLiteral("cursor")).toString(), QStringLiteral("#fcee0a"));

    m_settings->setSeparateTerminalTheme(true);
    m_settings->setTerminalThemeId(QStringLiteral("xterm"));
    QVERIFY(showPalette(QStringLiteral("theme dracula")));
    QTRY_VERIFY(!row(QStringLiteral("ui-theme:dracula")).isEmpty());
    QVERIFY(choose(QStringLiteral("ui-theme:dracula")));
    QCOMPARE(m_settings->effectiveUiThemeId(), QStringLiteral("dracula"));
    QCOMPARE(m_settings->effectiveTerminalThemeId(), QStringLiteral("xterm"));
    QTRY_COMPARE(activeTerminal()->colorScheme().value(QStringLiteral("background")).toString(), QStringLiteral("#000000"));
    QVERIFY(showPalette(QStringLiteral("theme")));
    QVERIFY(!rowsOfType(QStringLiteral("terminalTheme")).isEmpty());
    capture(QStringLiteral("command-palette-themes.png"));
    QVERIFY(choose(QStringLiteral("terminal-theme:dracula")));
    QCOMPARE(m_settings->effectiveTerminalThemeId(), QStringLiteral("dracula"));
    QTRY_COMPARE(activeTerminal()->colorScheme().value(QStringLiteral("background")).toString(), QStringLiteral("#282a36"));
}

void CommandPaletteTest::localLaunchersIncludeWslWithoutStartingIt() {
    const auto arguments = fixtureCommand();
    auto profile = QVariantMap{{QStringLiteral("id"), QStringLiteral("custom:palette-fixture")},
        {QStringLiteral("name"), QStringLiteral("Palette fixture shell")},
        {QStringLiteral("kind"), QStringLiteral("local")}, {QStringLiteral("distribution"), QString{}},
        {QStringLiteral("program"), arguments.first()}, {QStringLiteral("arguments"), arguments.mid(1)},
        {QStringLiteral("workingDirectory"), QString{}}};
    const auto id = m_localProfiles->saveProfile(profile);
    QVERIFY2(!id.isEmpty(), qPrintable(m_localProfiles->lastError()));
    QVERIFY(showPalette(QStringLiteral("Palette fixture shell")));
    QTRY_VERIFY(!row(QStringLiteral("profile:") + id).isEmpty());
    const auto* previous = activeTerminal();
    QVERIFY(choose(QStringLiteral("profile:") + id));
    QTRY_COMPARE(m_pane->property("tabCount").toInt(), 2);
    QTRY_VERIFY(activeTerminal() && activeTerminal() != previous && activeTerminal()->isRunning());
    QVERIFY(activeTerminal()->currentSshConnection().isEmpty());
#ifdef Q_OS_WIN
    QVERIFY(showPalette(QStringLiteral("powershell")));
    QTRY_VERIFY(!rowsOfType(QStringLiteral("local")).isEmpty());
    profile[QStringLiteral("id")] = QStringLiteral("custom:palette-wsl");
    profile[QStringLiteral("name")] = QStringLiteral("WSL palette fixture");
    profile[QStringLiteral("kind")] = QStringLiteral("wsl");
    profile[QStringLiteral("distribution")] = QStringLiteral("Palette-Test-Distribution");
    profile[QStringLiteral("arguments")] = QVariantList{};
    const auto wslId = m_localProfiles->saveProfile(profile);
    QVERIFY2(!wslId.isEmpty(), qPrintable(m_localProfiles->lastError()));
    QVERIFY(showPalette(QStringLiteral("WSL palette fixture")));
    QTRY_VERIFY(!row(QStringLiteral("profile:") + wslId).isEmpty());
    QVERIFY(row(QStringLiteral("profile:") + wslId).value(QStringLiteral("subtitle")).toString().contains(QStringLiteral("Palette-Test-Distribution")));
    QCOMPARE(m_pane->property("tabCount").toInt(), 2); // Merely listing a WSL launcher must not boot it.
#endif
}

void CommandPaletteTest::escapeRestoresFocusAndReviewDialogsBlockTheShortcut() {
    auto* terminal = activeTerminal();
    terminal->forceActiveFocus();
    QVERIFY(showPalette(QStringLiteral("settings")));
    QTest::keyClick(m_window, Qt::Key_Tab);
    QTRY_VERIFY2(m_window->activeFocusItem() && m_window->activeFocusItem()->objectName().startsWith(QStringLiteral("commandPaletteResult:")),
        qPrintable(m_window->activeFocusItem() ? QStringLiteral("Focused: ") + m_window->activeFocusItem()->objectName()
                   + QStringLiteral(" (") + QString::fromLatin1(m_window->activeFocusItem()->metaObject()->className()) + u')'
                   : QStringLiteral("No active focus item")));
    QTest::keyClick(m_window, Qt::Key_Backtab, Qt::ShiftModifier);
    QTRY_VERIFY(m_window->activeFocusItem());
    QTRY_COMPARE(m_window->activeFocusItem()->objectName(), QStringLiteral("commandPaletteSearch"));
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!palette()->property("visible").toBool());
    QTRY_VERIFY(terminal->hasActiveFocus());

    terminal->pasteText(QStringLiteral("first line\nsecond line"));
    QTRY_VERIFY(m_pane->property("securityPromptActive").toBool());
    QTRY_VERIFY(m_window->activeFocusItem() && m_window->activeFocusItem()->property("text").toString() == QStringLiteral("Cancel"));
    QTest::keyClick(m_window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier);
    QTest::qWait(100);
    QVERIFY(!palette()->property("visible").toBool());
    QVERIFY(m_pane->property("securityPromptActive").toBool());
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!m_pane->property("securityPromptActive").toBool());
    QCOMPARE(m_pane->property("tabCount").toInt(), 1);
}

int main(int argc, char* argv[]) {
    QTemporaryDir initial(QDir::tempPath() + QStringLiteral("/bitshell-palette-test-XXXXXX"));
    if (!initial.isValid()) return EXIT_FAILURE;
    qputenv("BIT8SHELL_PROFILE", initial.path().toUtf8());
    qputenv("XDG_CONFIG_HOME", initial.path().toUtf8());
#ifdef Q_OS_WIN
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("SystemRoot") + QStringLiteral("/Fonts")).toUtf8());
#endif
    QGuiApplication app(argc, argv);
    if (QFontDatabase::families().isEmpty()) return EXIT_FAILURE;
    CommandPaletteTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "CommandPaletteTest.moc"
