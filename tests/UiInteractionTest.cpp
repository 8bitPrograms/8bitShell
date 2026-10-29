#include <QtTest>

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QQmlApplicationEngine>
#include <QQmlIncubationController>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QScopeGuard>
#include <bitshell/core/YamlFile.h>
#include <QTemporaryDir>
#include <QWheelEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDrag>
#include <QDropEvent>
#include <QStyleHints>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/MacroStore.h>
#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/SftpController.h>
#include <bitshell/core/LocalFileModel.h>
#include <bitshell/core/SessionStore.h>
#include <bitshell/core/PersonalProfile.h>
#include <bitshell/terminal/TerminalView.h>
#include <bitshell/terminal/BroadcastController.h>
#include "TerminalTestAccess.h"

#include <memory>
#include <algorithm>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <thread>
#include <chrono>
#endif

class UiInteractionTest final : public QObject {
    Q_OBJECT

public:
    explicit UiInteractionTest(QString configRoot)
        : m_configRoot(std::move(configRoot)) {}

private slots:
    void initTestCase();
    void optionalUiLoadsOnFirstUse();
    void versionTapsRevealBonusThemes();
    void linksUiAndTerminalThemesUnlessSeparated();
    void scrollbackPaddingUsesRealBlankCells();
    void panelControlsStayReachable();
    void panelsRetractUpwardsWithoutResizingTheirContents();
    void fileBrowserStateFollowsTabsAndVisibilityPersists();
    void threeDotMenusOpenAndEditActionWorks();
    void sessionInspectorCopiesFullSessionDetails();
    void catalogBackgroundAndFolderRightClickCreateInExpectedLocation();
    void dragNearEdgeScrollsLargeCatalog();
    void dragsSessionIntoFolder();
    void dragsWholeFolderTreeAndBackToRoot();
    void switchesUiAndTerminalThemesIndependently();
    void switchesDayNightMatrixAndAutomaticModes();
    void themeSwitchWorkload();
    void themeMenusRenderingAndSshIcons();
    void settingsDropdownScrollingKeepsRowsVisible_data();
    void settingsDropdownScrollingKeepsRowsVisible();
    void reordersAndScrollsOverflowingTabs();
    void macroButtonsMonitoringAndPanelVisibility();
    void terminalMenuWheelAndShortcutEditor();
    void fileBrowserFoldersNavigateWithMouseAndKeyboard();
    void fileBrowserReceivesDesktopDropsAndExportsRemoteFiles();
    void yamlImportExportAndLiveReloadControls();
    void securityPromptsAreVisibleAndCancelByDefault();
    void cyberArkAndPersonalProfileControls();
    void sshKeySettingsCyberArkToggleAndAboutUi();
    void sessionSearchDropdownRefreshesAndUsesStableIds();
    void splitPanesAndReviewedMultiSsh();
    void deletesSingleSelectedFoldersAndWholeCatalogSafely();
    void localProfilesDefaultMenuAndSettings();
    void cleanupTestCase();

private:
    [[nodiscard]] QString bitshellConfigPath(const QString& fileName) const;
    void writeCatalogFixture();
    [[nodiscard]] QQuickItem* treeRow(const QString& name, bool folder) const;
    [[nodiscard]] QQuickItem* visualItem(const QString& objectName) const;
    [[nodiscard]] QObject* namedObject(const QString& name) const;
    [[nodiscard]] QPoint itemPoint(QQuickItem* item, qreal xRatio = 0.5) const;
    void clickItem(QQuickItem* item);
    [[nodiscard]] bool beginDrag(QQuickItem* source);
    void finishDragAt(QQuickItem* destination);
    void cancelDragAt(const QPoint& destination);
    void captureFrame(const QString& name) const;
    [[nodiscard]] QString persistedSessionGroup(const QString& id) const;
    [[nodiscard]] QStringList persistedFolders() const;

    QString m_configRoot;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QQuickWindow* m_window = nullptr;
};

QString UiInteractionTest::bitshellConfigPath(const QString& fileName) const {
    return QDir(QDir(m_configRoot).filePath(QStringLiteral("8bitshell"))).filePath(fileName);
}

void UiInteractionTest::writeCatalogFixture() {
    QVERIFY(QDir().mkpath(bitshellConfigPath({})));

    QJsonArray folders{
        QStringLiteral("00 Source"),
        QStringLiteral("00 Source/Nested"),
        QStringLiteral("01 Target"),
    };
    for (int index = 0; index < 36; ++index) {
        folders.append(QStringLiteral("Fleet %1").arg(index + 1, 2, 10, QLatin1Char('0')));
    }

    QJsonArray sessions{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("move-me")},
            {QStringLiteral("name"), QStringLiteral("move-me")},
            {QStringLiteral("host"), QStringLiteral("move.example.test")},
            {QStringLiteral("user"), QStringLiteral("operator")},
            {QStringLiteral("port"), 22},
            {QStringLiteral("group"), QStringLiteral("00 Source/Nested")},
            {QStringLiteral("backspaceMode"), QStringLiteral("bs")},
        },
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("stay-nested")},
            {QStringLiteral("name"), QStringLiteral("stay-nested")},
            {QStringLiteral("host"), QStringLiteral("stay.example.test")},
            {QStringLiteral("user"), QStringLiteral("operator")},
            {QStringLiteral("port"), 22},
            {QStringLiteral("group"), QStringLiteral("00 Source/Nested")},
        },
    };

    QFile catalog(bitshellConfigPath(QStringLiteral("sessions.json")));
    QVERIFY(catalog.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QJsonDocument document(QJsonObject{
        {QStringLiteral("version"), 2},
        {QStringLiteral("folders"), folders},
        {QStringLiteral("sessions"), sessions},
    });
    QCOMPARE(catalog.write(document.toJson(QJsonDocument::Indented)),
             document.toJson(QJsonDocument::Indented).size());
    catalog.close();

    QSettings settings(bitshellConfigPath(QStringLiteral("settings.ini")),
                       QSettings::IniFormat);
    settings.setValue(QStringLiteral("navigation/expandedFolders"),
                      QStringList{QStringLiteral("00 Source"),
                                  QStringLiteral("00 Source/Nested"),
                                  QStringLiteral("01 Target")});
    settings.setValue(QStringLiteral("logging/enabledByDefault"), false);
    settings.setValue(QStringLiteral("logging/directory"),
                      QDir(m_configRoot).filePath(QStringLiteral("logs")));
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);
}

void UiInteractionTest::initTestCase() {
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0.0"));
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("bitshell-ui-test"));
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    writeCatalogFixture();

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->loadFromModule(QStringLiteral("BitShell"), QStringLiteral("Main"));
    QVERIFY2(!m_engine->rootObjects().isEmpty(), "The main QML window did not load");
    m_window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst());
    QVERIFY(m_window != nullptr);
    m_window->setWidth(1080);
    m_window->setHeight(680);
    m_window->show();
    QTest::qWait(250);

    QTRY_VERIFY(namedObject(QStringLiteral("catalogSidebar")) != nullptr);
    QTRY_VERIFY(treeRow(QStringLiteral("move-me"), false) != nullptr);
    QTRY_VERIFY(treeRow(QStringLiteral("01 Target"), true) != nullptr);
}

QObject* UiInteractionTest::namedObject(const QString& name) const {
    if (!m_window) return nullptr;
    auto* result = m_window->findChild<QObject*>(name, Qt::FindChildrenRecursively);
    if (result) return result;
    // Tests that address an editor directly explicitly request its lazy load.
    // The startup regression below uses findChild directly to test absence.
    if (QStringList{QStringLiteral("settingsDrawer"), QStringLiteral("sessionEditorDialog"),
            QStringLiteral("macroManager"), QStringLiteral("routeManager"),
            QStringLiteral("folderConnectionDialog"), QStringLiteral("sshKeyManager"),
            QStringLiteral("aboutDialog")}.contains(name))
        QMetaObject::invokeMethod(m_window, "ensureOverlay", Q_ARG(QVariant, name));
    if (QStringList{QStringLiteral("sftpController"), QStringLiteral("localFileModel"),
            QStringLiteral("fileBrowser")}.contains(name)) {
        auto* loader = m_window->findChild<QObject*>(QStringLiteral("filePanelLoader"));
        if (loader) loader->setProperty("active", true);
    }
    return m_window->findChild<QObject*>(name, Qt::FindChildrenRecursively);
}

void UiInteractionTest::versionTapsRevealBonusThemes() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(settings && !settings->bonusThemesUnlocked());
    const bool wasSeparate = settings->separateTerminalTheme();
    settings->setSeparateTerminalTheme(true);
    const auto restoreSeparation = qScopeGuard([&] { settings->setSeparateTerminalTheme(wasSeparate); });
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Appearance"))));
    QTRY_VERIFY(drawer->property("opened").toBool());
    auto* version = visualItem(QStringLiteral("applicationVersionButton"));
    auto* choices = namedObject(QStringLiteral("uiThemeCombo"));
    QVERIFY(version && choices);
    QVERIFY(version->property("text").toString().contains(QStringLiteral("1.0.0")));
    const int before = choices->property("count").toInt();
    const QString selected = settings->uiThemeId();
    const QString oldTerminal = settings->terminalThemeId();
    const QString oldDayUi = settings->uiLightThemeId();
    const QString oldDayTerminal = settings->terminalLightThemeId();
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QObject* terminal = nullptr;
    QTRY_VERIFY((terminal = pane->property("activeTerminal").value<QObject*>()) != nullptr);
    // Previously saved bonus selections retain their appearance but cannot leak
    // into any of the four pickers before the version gesture unlocks them.
    for (const auto* id : {"blueberry", "blueberry-light", "cyberpunk-silverhand", "cyberpunk-v", "cyberpunk-k"}) {
        const QString themeId = QString::fromLatin1(id);
        settings->setUiThemeId(themeId);
        settings->setUiLightThemeId(themeId);
        settings->setTerminalThemeId(themeId);
        settings->setTerminalLightThemeId(themeId);
        for (const auto* name : {"uiThemeCombo", "uiLightThemeCombo", "terminalThemeCombo", "terminalLightThemeCombo"}) {
            auto* picker = namedObject(QString::fromLatin1(name));
            QVERIFY(picker);
            QTRY_COMPARE(picker->property("currentIndex").toInt(), -1);
            QVERIFY(picker->property("currentText").toString().isEmpty());
            QCOMPARE(picker->property("count").toInt(), QByteArray(name).startsWith("ui") ? 19 : 27);
        }
        QCOMPARE(settings->uiThemeId(), themeId);
        QVERIFY(!settings->bonusThemesUnlocked());
    }
    settings->setUiThemeId(QStringLiteral("missing-theme"));
    settings->setTerminalThemeId(QStringLiteral("missing-theme"));
    QTRY_COMPARE(m_window->color(), QColor(QStringLiteral("#11111b")));
    QTRY_COMPARE(terminal->property("colorScheme").toMap().value(QStringLiteral("background")).toString(), QStringLiteral("#000000"));
    settings->setUiThemeId(selected);
    settings->setUiLightThemeId(oldDayUi);
    settings->setTerminalThemeId(oldTerminal);
    settings->setTerminalLightThemeId(oldDayTerminal);
    for (int tap = 0; tap < 6; ++tap) clickItem(version);
    QVERIFY(!settings->bonusThemesUnlocked());
    QCOMPARE(choices->property("count").toInt(), before);
    clickItem(version);
    QTRY_VERIFY(settings->bonusThemesUnlocked());
    QTRY_COMPARE(choices->property("count").toInt(), before + 5);
    QCOMPARE(settings->uiThemeId(), selected);
    QVERIFY(namedObject(QStringLiteral("bonusThemesHint"))->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QVariant activeId;
    QVERIFY(QMetaObject::invokeMethod(pane, "tabIdAt", Q_RETURN_ARG(QVariant, activeId),
                                     Q_ARG(QVariant, pane->property("currentIndex"))));
    auto* activeTab = visualItem(QStringLiteral("openTab:") + activeId.toString());
    auto* tabLabel = visualItem(QStringLiteral("openTabLabel:") + activeId.toString());
    auto* headerBand = visualItem(QStringLiteral("headerAccentBand"));
    auto* header = visualItem(QStringLiteral("topBar"));
    auto* headerTitle = namedObject(QStringLiteral("headerTitle"));
    auto* connectionSurface = namedObject(QStringLiteral("quickConnectSurface"));
    auto* connectionField = namedObject(QStringLiteral("quickConnectField"));
    auto* status = namedObject(QStringLiteral("terminalStatusBar"));
    QVERIFY(activeTab);
    QVERIFY(tabLabel);
    QVERIFY(headerBand);
    QVERIFY(header && headerTitle && connectionSurface && connectionField);
    QVERIFY(status);
    const auto luminance = [](QColor color) {
        const auto linear = [](double channel) {
            return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    };
    const auto contrast = [&](QColor first, QColor second) {
        const auto a = luminance(first), b = luminance(second);
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    };
    struct CyberpunkPalette { QString id; QString name; QString background; QString foreground; QString accent; QString secondary; };
    for (const auto& palette : {
             CyberpunkPalette{QStringLiteral("cyberpunk-silverhand"), QStringLiteral("Silverhand"), QStringLiteral("#0b0d16"), QStringLiteral("#e8f0f2"), QStringLiteral("#fcee0a"), QStringLiteral("#00f0ff")},
             CyberpunkPalette{QStringLiteral("cyberpunk-v"), QStringLiteral("V"), QStringLiteral("#140d13"), QStringLiteral("#f4e4e8"), QStringLiteral("#ff4f5e"), QStringLiteral("#00f0ff")},
             CyberpunkPalette{QStringLiteral("cyberpunk-k"), QStringLiteral("K"), QStringLiteral("#0b141b"), QStringLiteral("#e2f2f5"), QStringLiteral("#00f0ff"), QStringLiteral("#91bcff")}}) {
        settings->setUiThemeId(palette.id);
        settings->setTerminalThemeId(palette.id);
        QTRY_COMPARE(choices->property("currentText").toString(), palette.name);
        QTRY_COMPARE(m_window->property("color").value<QColor>(), QColor(palette.background));
        QTRY_COMPARE(terminal->property("colorScheme").toMap().value(QStringLiteral("background")).toString(), palette.background);
        const auto scheme = terminal->property("colorScheme").toMap();
        QCOMPARE(scheme.value(QStringLiteral("foreground")).toString(), palette.foreground);
        QCOMPARE(scheme.value(QStringLiteral("ansi")).toList().size(), 16);
        QTRY_COMPARE(activeTab->property("color").value<QColor>(), QColor(palette.accent));
        QCOMPARE(tabLabel->property("color").value<QColor>(), QColor(palette.background));
        QVERIFY(headerBand->isVisible());
        QCOMPARE(headerBand->property("color").value<QColor>(), QColor(palette.accent));
        QCOMPARE(header->property("color").value<QColor>(), QColor(palette.accent));
        QVERIFY(contrast(headerTitle->property("color").value<QColor>(), QColor(palette.accent)) >= 4.5);
        QVERIFY(contrast(connectionField->property("color").value<QColor>(),
                         connectionSurface->property("color").value<QColor>()) >= 4.5);
        QCOMPARE(QQmlProperty(status, QStringLiteral("border.color")).read().value<QColor>(), QColor(palette.secondary));
        // This text is a renderer fixture, not an executed command or user data.
        // A deterministic ANSI sample also makes the three palette captures comparable.
        auto* terminalView = qobject_cast<bitshell::terminal::TerminalView*>(terminal);
        QVERIFY(terminalView);
        bitshell::terminal::TerminalTestAccess::feed(*terminalView, QByteArray(
            "\x1b[0m\x1b[2J\x1b[H"
            "\x1b[90m# Theme preview fixture\x1b[0m\r\n\r\n"
            "\x1b[36mPS\x1b[0m C:\\Projects\\8bitPrograms> \x1b[33mGet-ChildItem\x1b[0m\r\n\r\n"
            "    Directory: C:\\Projects\\8bitPrograms\r\n\r\n"
            "\x1b[90mMode       Name\x1b[0m\r\n"
            "d-----     \x1b[34m8bitPad\x1b[0m\r\n"
            "d-----     \x1b[34m8bitShell\x1b[0m\r\n"
            "-a----     \x1b[32mnotes.txt\x1b[0m\r\n\r\n"
            "\x1b[32mSuccess\x1b[0m   \x1b[33mWarning\x1b[0m   \x1b[31mError\x1b[0m   \x1b[36mInformation\x1b[0m\r\n\r\n"
            "\x1b[36mPS\x1b[0m C:\\Projects\\8bitPrograms> "));
        // Check a rendered band pixel too: data-only assertions can miss a layer
        // covering the accent or a component that is not actually visible.
        QTest::qWait(150);
        const auto frame = m_window->grabWindow();
        QVERIFY(!frame.isNull());
        const auto point = headerBand->mapToScene(QPointF(headerBand->width() / 2.0, 1.0));
        const qreal scale = qreal(frame.width()) / m_window->width();
        QCOMPARE(frame.pixelColor(qRound(point.x() * scale), qRound(point.y() * scale)), QColor(palette.accent));
        // Check the middle of the real toolbar too, not only its old 3px edge.
        const auto headerPoint = header->mapToScene(QPointF(2.0, header->height() / 2.0));
        QCOMPARE(frame.pixelColor(qRound(headerPoint.x() * scale), qRound(headerPoint.y() * scale)), QColor(palette.accent));
        captureFrame(palette.id + QStringLiteral("-workspace.png"));
    }
    settings->setUiThemeId(selected);
    settings->setTerminalThemeId(oldTerminal);
    QVERIFY(!headerBand->isVisible());
}

void UiInteractionTest::linksUiAndTerminalThemesUnlessSeparated() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(settings && !settings->separateTerminalTheme());
    const auto oldUi = settings->uiThemeId();
    const auto oldDayUi = settings->uiLightThemeId();
    const auto oldTerminal = settings->terminalThemeId();
    const auto oldMode = settings->appearanceMode();
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    const auto restore = qScopeGuard([&] {
        settings->setSeparateTerminalTheme(false);
        settings->setUiThemeId(oldUi);
        settings->setUiLightThemeId(oldDayUi);
        settings->setTerminalThemeId(oldTerminal);
        settings->setAppearanceMode(oldMode);
        QMetaObject::invokeMethod(drawer, "close");
    });
    settings->setAppearanceMode(QStringLiteral("dark"));
    settings->setTerminalThemeId(QStringLiteral("dracula"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Appearance"))));
    QTRY_VERIFY(drawer->property("opened").toBool());
    auto* separate = visualItem(QStringLiteral("separateTerminalThemeSwitch"));
    auto* ui = namedObject(QStringLiteral("uiThemeCombo"));
    auto* dayUi = namedObject(QStringLiteral("uiLightThemeCombo"));
    auto* terminalPicker = visualItem(QStringLiteral("terminalThemeCombo"));
    auto* terminalDayPicker = visualItem(QStringLiteral("terminalLightThemeCombo"));
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(separate && ui && dayUi && terminalPicker && terminalDayPicker && pane);
    QObject* terminal = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(terminal);
    const int tabCount = pane->property("tabCount").toInt();
    const auto background = [&] {
        return terminal->property("colorScheme").toMap().value(QStringLiteral("background")).toString();
    };
    const auto choose = [](QObject* picker, const char* id) {
        return QMetaObject::invokeMethod(picker, "chosen", Q_ARG(QString, QString::fromLatin1(id)));
    };
    QVERIFY(!separate->property("checked").toBool());
    QVERIFY(!terminalPicker->isVisible() && !terminalDayPicker->isVisible());
    QVERIFY(!terminalPicker->isEnabled() && !terminalDayPicker->isEnabled());
    QVERIFY(choose(ui, "cyberpunk-silverhand"));
    QTRY_COMPARE(m_window->color(), QColor(QStringLiteral("#0b0d16")));
    QTRY_COMPARE(background(), QStringLiteral("#0b0d16"));
    QCOMPARE(terminal->property("colorScheme").toMap().value(QStringLiteral("ansi")).toList().at(3).toString(), QStringLiteral("#fcee0a"));
    captureFrame(QStringLiteral("appearance-linked.png"));

    clickItem(separate);
    QTRY_VERIFY(settings->separateTerminalTheme());
    QTRY_VERIFY(terminalPicker->isVisible() && terminalDayPicker->isVisible());
    QTRY_COMPARE(background(), QStringLiteral("#282a36")); // Retained terminal choice.
    QVERIFY(choose(terminalPicker, "nord"));
    QTRY_COMPARE(background(), QStringLiteral("#2e3440"));
    QCOMPARE(m_window->color(), QColor(QStringLiteral("#0b0d16")));
    captureFrame(QStringLiteral("appearance-separated.png"));
    QVERIFY(choose(ui, "cyberpunk-k"));
    QTRY_COMPARE(m_window->color(), QColor(QStringLiteral("#0b141b")));
    QCOMPARE(background(), QStringLiteral("#2e3440"));
    bitshell::core::AppSettings reloaded;
    QVERIFY(reloaded.separateTerminalTheme());
    QCOMPARE(reloaded.effectiveTerminalThemeId(), QStringLiteral("nord"));

    clickItem(separate);
    QTRY_VERIFY(!settings->separateTerminalTheme());
    QTRY_COMPARE(background(), QStringLiteral("#0b141b"));
    QVERIFY(!terminalPicker->isVisible() && !terminalDayPicker->isVisible());
    QCOMPARE(settings->terminalThemeId(), QStringLiteral("nord"));
    QVERIFY(choose(dayUi, "github-light"));
    settings->setAppearanceMode(QStringLiteral("light"));
    QTRY_COMPARE(m_window->color(), QColor(QStringLiteral("#f6f8fa")));
    QTRY_COMPARE(background(), QStringLiteral("#ffffff"));
    settings->setAppearanceMode(QStringLiteral("dark"));
    QTRY_COMPARE(background(), QStringLiteral("#0b141b"));
    settings->setUiThemeId(QStringLiteral("missing-theme"));
    QTRY_COMPARE(m_window->color(), QColor(QStringLiteral("#11111b")));
    QTRY_COMPARE(background(), QStringLiteral("#1e1e2e")); // Linked public fallback.
    QCOMPARE(pane->property("activeTerminal").value<QObject*>(), terminal);
    QCOMPARE(pane->property("tabCount").toInt(), tabCount);
    // An external configuration edit rejects saving. The visible toggle must
    // still describe the active mode, rather than staying optimistically on.
    QFile external(settings->storagePath());
    QVERIFY(external.open(QIODevice::Append));
    QVERIFY(external.write("\n# External edit during appearance selection\n") > 0);
    external.close();
    clickItem(separate);
    QTRY_VERIFY(!settings->lastError().isEmpty());
    QVERIFY(!settings->separateTerminalTheme());
    QTRY_VERIFY(!separate->property("checked").toBool());
    QVERIFY2(settings->reload(), qPrintable(settings->lastError()));
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
}

void UiInteractionTest::optionalUiLoadsOnFirstUse() {
    for (const auto* name : {"settingsDrawer", "sessionEditorDialog", "macroManager",
            "routeManager", "folderConnectionDialog", "sshKeyManager", "aboutDialog",
            "sftpController", "localFileModel"})
        QVERIFY2(!m_window->findChild<QObject*>(QString::fromLatin1(name)), name);
    auto* loader = m_window->findChild<QObject*>(QStringLiteral("filePanelLoader"));
    QVERIFY(loader && !loader->property("active").toBool());
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    QTRY_VERIFY(drawer->property("width").toReal() > 280);
    QTRY_VERIFY(drawer->property("width").toReal() < 460);
    QTRY_VERIFY(drawer->property("height").toReal() > 500);
    QVERIFY(!m_window->findChild<QObject*>(QStringLiteral("sshKeyManager")));
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Terminal"))));
    QTRY_VERIFY(drawer->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("opened").toBool());
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QCOMPARE(namedObject(QStringLiteral("settingsDrawer")), drawer);
    m_window->setProperty("inspectorMode", 1);
    QTRY_VERIFY(loader->property("active").toBool());
    auto* files = m_window->findChild<QObject*>(QStringLiteral("localFileModel"));
    QVERIFY(files);
    m_window->setProperty("inspectorMode", 0);
    QCOMPARE(m_window->findChild<QObject*>(QStringLiteral("localFileModel")), files);
}

QQuickItem* UiInteractionTest::treeRow(const QString& name, bool folder) const {
    if (m_window == nullptr) {
        return nullptr;
    }
    QList<QQuickItem*> pending{m_window->contentItem()};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeLast();
        pending.append(item->childItems());
        if (!item->objectName().startsWith(QStringLiteral("sessionTreeRow:"))) {
            continue;
        }
        if (item->property("nodeName").toString() == name
            && item->property("folderNode").toBool() == folder) {
            return item;
        }
    }
    return nullptr;
}

QQuickItem* UiInteractionTest::visualItem(const QString& objectName) const {
    if (m_window == nullptr) {
        return nullptr;
    }
    QList<QQuickItem*> pending{m_window->contentItem()};
    while (!pending.isEmpty()) {
        QQuickItem* item = pending.takeLast();
        pending.append(item->childItems());
        if (item->objectName() == objectName) {
            return item;
        }
    }
    return nullptr;
}

QPoint UiInteractionTest::itemPoint(QQuickItem* item, qreal xRatio) const {
    const QPointF local(item->width() * xRatio, item->height() * 0.5);
    return item->mapToScene(local).toPoint();
}

void UiInteractionTest::clickItem(QQuickItem* item) {
    QVERIFY(item != nullptr);
    // Synthetic pointer events bypass the window manager. After a native file
    // dialog, activate the main window as a real desktop click would do.
    auto* targetWindow = item->window();
    QVERIFY(targetWindow != nullptr);
    if (QGuiApplication::focusWindow() != targetWindow) {
        targetWindow->requestActivate();
        QTRY_COMPARE(QGuiApplication::focusWindow(), targetWindow);
    }
    // Real pointer movement reveals hover-only controls before the press.
    QTest::mouseMove(targetWindow, itemPoint(item), 20);
    QCoreApplication::processEvents();
    QTest::mouseClick(targetWindow, Qt::LeftButton, Qt::NoModifier, itemPoint(item), 20);

}

void UiInteractionTest::captureFrame(const QString& name) const {
    const QString directory = qEnvironmentVariable("BIT8SHELL_UI_TEST_CAPTURE_DIR");
    if (directory.isEmpty() || m_window == nullptr) {
        return;
    }
    if (!QDir().mkpath(directory)) {
        return;
    }
    (void)m_window->grabWindow().save(QDir(directory).filePath(name));
}

void UiInteractionTest::scrollbackPaddingUsesRealBlankCells() {
    bitshell::terminal::TerminalView terminal;
    terminal.setLogEnabled(false);
    terminal.setWidth(160);
    terminal.setHeight(80);
    bitshell::terminal::TerminalTestAccess::feed(terminal, QByteArray("short\r\n").repeated(20));
    QVERIFY(terminal.scrollbackLines() > 0);
    const auto cells = bitshell::terminal::TerminalTestAccess::popScrollbackRow(terminal, 1000);
    QCOMPARE(cells.size(), size_t(1000));
    for (const auto& cell : cells) QCOMPARE(cell.width, char(1));
    // Wider columns, then additional rows, restore old narrow history lines.
    terminal.setWidth(900);
    terminal.setHeight(600);
}

void UiInteractionTest::panelControlsStayReachable() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    auto* connections = visualItem(QStringLiteral("connectionsToggle"));
    auto* quickConnect = visualItem(QStringLiteral("quickConnectField"));
    auto* layout = visualItem(QStringLiteral("splitLayoutSelector"));
    auto* appearance = visualItem(QStringLiteral("appearanceToggle"));
    auto* profile = visualItem(QStringLiteral("profileSettingsButton"));
    auto* profileAvatar = visualItem(QStringLiteral("topBarProfileAvatar"));
    auto* macros = visualItem(QStringLiteral("macroBarToggle"));
    auto* monitoring = visualItem(QStringLiteral("monitoringToggle"));
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(settings && connections && quickConnect && layout && appearance
            && profile && profileAvatar && macros && monitoring && pane);
    QVERIFY(!namedObject(QStringLiteral("viewMenuButton")));
    const auto inWindow = [this](QQuickItem* item) {
        const auto rect = item->mapRectToScene(item->boundingRect());
        return item->isVisible() && rect.left() >= 0 && rect.top() >= 0
            && rect.right() <= m_window->width() + 1 && rect.bottom() <= m_window->height() + 1;
    };
    QTRY_VERIFY(pane->property("activeTerminal").value<QObject*>() != nullptr);
    const auto* originalTerminal = pane->property("activeTerminal").value<QObject*>();
    const int originalCount = pane->property("tabCount").toInt();
    for (const int width : {1080, 1510}) {
        m_window->setWidth(width);
        for (const int inspector : {0, 1}) {
            m_window->setProperty("inspectorMode", inspector);
            QTest::qWait(100);
            for (int toggle = 0; toggle < 2; ++toggle) {
                QVERIFY(inWindow(connections));
                QVERIFY(inWindow(layout));
                QVERIFY(inWindow(appearance));
                QVERIFY(inWindow(profile));
                QVERIFY(inWindow(profileAvatar));
                QVERIFY(inWindow(macros));
                QVERIFY(inWindow(monitoring));
                QVERIFY(connections->mapRectToScene(connections->boundingRect()).right()
                        < quickConnect->mapRectToScene(quickConnect->boundingRect()).left());
                QVERIFY(layout->mapRectToScene(layout->boundingRect()).right()
                        < appearance->mapRectToScene(appearance->boundingRect()).left());
                QVERIFY(layout->mapRectToScene(layout->boundingRect()).bottom() <= 58.0);
                QCOMPARE(appearance->property("text").toString(), QString{});
                const QRectF profileRect = profile->mapRectToScene(profile->boundingRect());
                const QRectF avatarRect = profileAvatar->mapRectToScene(profileAvatar->boundingRect());
                QVERIFY(avatarRect.left() >= profileRect.left() + 6.0);
                QVERIFY(avatarRect.right() <= profileRect.right() - 6.0);
                const bool wasVisible = settings->sidebarVisible();
                clickItem(connections);
                QTRY_COMPARE(settings->sidebarVisible(), !wasVisible);
                QTRY_COMPARE(connections->property("panelVisible").toBool(), !wasVisible);
                QTRY_COMPARE(namedObject(QStringLiteral("catalogSidebar"))->property("visible").toBool(), !wasVisible);
                bitshell::core::AppSettings persisted;
                QCOMPARE(persisted.sidebarVisible(), !wasVisible);
                QTest::qWait(60);
                QVERIFY(inWindow(connections));
                QVERIFY(inWindow(macros));
                QVERIFY(inWindow(monitoring));
                if (width == 1080 && inspector == 0)
                    captureFrame(wasVisible ? QStringLiteral("connections-hidden.png") : QStringLiteral("connections-shown.png"));
            }
        }
    }
    m_window->setWidth(1080);
    m_window->setProperty("inspectorMode", 0);
    clickItem(macros);
    QTRY_VERIFY(!settings->macroBarVisible());
    QTRY_VERIFY(!namedObject(QStringLiteral("macroBar"))->property("visible").toBool());
    QTRY_VERIFY(!macros->property("panelVisible").toBool());
    clickItem(macros);
    QTRY_VERIFY(settings->macroBarVisible());
    QTRY_VERIFY(macros->property("panelVisible").toBool());
    QTRY_VERIFY(monitoring->isEnabled());
    clickItem(monitoring);
    QTRY_VERIFY(pane->property("monitoringActive").toBool());
    QTRY_VERIFY(monitoring->property("panelVisible").toBool());
    clickItem(visualItem(QStringLiteral("hideMonitoringButton")));
    QTRY_VERIFY(!pane->property("monitoringActive").toBool());
    QTRY_VERIFY(!monitoring->property("panelVisible").toBool());
    // Keyboard activation also reopens the sidebar; no new terminal is launched.
    clickItem(connections);
    connections->forceActiveFocus();
    QTest::keyClick(m_window, Qt::Key_Space);
    QTRY_VERIFY(settings->sidebarVisible());
    QTRY_VERIFY(!namedObject(QStringLiteral("connectionsPanel"))->property("animating").toBool());
    QTRY_VERIFY(!namedObject(QStringLiteral("filesPanel"))->property("visible").toBool());
    QCOMPARE(pane->property("activeTerminal").value<QObject*>(), originalTerminal);
    QCOMPARE(pane->property("tabCount").toInt(), originalCount);
}

void UiInteractionTest::panelsRetractUpwardsWithoutResizingTheirContents() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    auto* connectionsPanel = visualItem(QStringLiteral("connectionsPanel"));
    auto* filesPanel = visualItem(QStringLiteral("filesPanel"));
    auto* pane = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("terminalPane")));
    auto* sftp = namedObject(QStringLiteral("sftpController"));
    auto* localFiles = namedObject(QStringLiteral("localFileModel"));
    QVERIFY(settings && connectionsPanel && filesPanel && pane && sftp && localFiles);
    const auto* originalTerminal = pane->property("activeTerminal").value<QObject*>();
    const int originalCount = pane->property("tabCount").toInt();
    m_window->setWidth(1510);
    settings->setSidebarVisible(true);
    m_window->setProperty("inspectorMode", 1);
    QTRY_COMPARE(connectionsPanel->property("reveal").toReal(), 1.0);
    QTRY_COMPARE(filesPanel->property("reveal").toReal(), 1.0);
    QTest::qWait(60);

    struct PanelCase { QQuickItem* panel; QString contents; QString toggle; QString capture; };
    const QList<PanelCase> cases{
        {connectionsPanel, QStringLiteral("catalogSidebar"), QStringLiteral("connectionsToggle"), QStringLiteral("connections-retracting.png")},
        {filesPanel, QStringLiteral("fileBrowser"), QStringLiteral("filesToggle"), QStringLiteral("files-retracting.png")},
    };
    for (const auto& test : cases) {
        auto* contents = visualItem(test.contents);
        auto* toggle = visualItem(test.toggle);
        QVERIFY(contents && toggle && test.panel->clip());
        const QSizeF contentSize(contents->width(), contents->height());
        const QRectF panelRect = test.panel->mapRectToScene(test.panel->boundingRect());
        const qreal terminalWidth = pane->width();
        // Slow just this test's transition so the intermediate geometry can be
        // sampled reliably on busy CI machines and captured for visual review.
        const int normalDuration = test.panel->property("animationDuration").toInt();
        test.panel->setProperty("animationDuration", 600);
        clickItem(toggle);
        QTRY_VERIFY(test.panel->property("animating").toBool());
        QTest::qWait(120);
        const auto offset = contents->mapToItem(test.panel, QPointF());
        QVERIFY(offset.y() < -1 && offset.y() > -contentSize.height());
        QCOMPARE(offset.x(), 0.0);
        QCOMPARE(QSizeF(contents->width(), contents->height()), contentSize);
        QCOMPARE(test.panel->mapRectToScene(test.panel->boundingRect()), panelRect);
        QCOMPARE(pane->width(), terminalWidth);
        QVERIFY(!contents->isEnabled());
        captureFrame(test.capture);

        // Reverse before closure: no restart, jump, or horizontal resize.
        clickItem(toggle);
        QVERIFY(test.panel->isVisible());
        QCOMPARE(pane->width(), terminalWidth);
        QTRY_COMPARE(test.panel->property("reveal").toReal(), 1.0);
        QCOMPARE(contents->mapToItem(test.panel, QPointF()).y(), 0.0);
        QVERIFY(contents->isEnabled());
        QCOMPARE(QSizeF(contents->width(), contents->height()), contentSize);

        // Fully closing releases the slot once; reopening retains its width.
        test.panel->setProperty("animationDuration", normalDuration);
        clickItem(toggle);
        QTRY_VERIFY(!test.panel->isVisible());
        QTRY_VERIFY(pane->width() > terminalWidth);
        QVERIFY(!contents->isVisible());
        clickItem(toggle);
        QTRY_COMPARE(test.panel->property("reveal").toReal(), 1.0);
        QTRY_COMPARE(pane->width(), terminalWidth);
        QCOMPARE(test.panel->width(), panelRect.width());
        QCOMPARE(QSizeF(contents->width(), contents->height()), contentSize);
        QCOMPARE(namedObject(QStringLiteral("sftpController")), sftp);
        QCOMPARE(namedObject(QStringLiteral("localFileModel")), localFiles);
        QCOMPARE(pane->property("activeTerminal").value<QObject*>(), originalTerminal);
        QCOMPARE(pane->property("tabCount").toInt(), originalCount);
    }

    // Highlight rules now live in the sectioned Settings surface instead of
    // competing with the file browser for another inspector slot.
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    QVERIFY(QMetaObject::invokeMethod(drawer, "openRules", Q_ARG(QVariant, false)));
    QTRY_VERIFY(drawer->property("opened").toBool());
    QTRY_VERIFY(visualItem(QStringLiteral("settingsHighlightPanel"))->isVisible());
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    m_window->setProperty("inspectorMode", 0);
    QTRY_VERIFY(!filesPanel->isVisible());
    m_window->setWidth(1080);
    QTest::qWait(60);
}

void UiInteractionTest::fileBrowserStateFollowsTabsAndVisibilityPersists() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    auto* local = qobject_cast<bitshell::core::LocalFileModel*>(namedObject(QStringLiteral("localFileModel")));
    QVERIFY(settings && pane && local);
    QTemporaryDir paths;
    QVERIFY(paths.isValid());
    const QString firstPath = paths.filePath(QStringLiteral("first"));
    const QString secondPath = paths.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkdir(firstPath));
    QVERIFY(QDir().mkdir(secondPath));

    m_window->setProperty("inspectorMode", 1);
    QTRY_VERIFY(local->active());
    auto* remoteConnectView = visualItem(QStringLiteral("remoteFilesConnectView"));
    auto* localPane = visualItem(QStringLiteral("localFilePane"));
    QVERIFY(remoteConnectView && localPane);
    QTRY_VERIFY(!remoteConnectView->isVisible());
    QTRY_VERIFY(localPane->height() > 500);
    local->changeDirectory(firstPath);
    QTRY_COMPARE(local->currentPath(), firstPath);
    const int originalCount = pane->property("tabCount").toInt();
    const int firstIndex = pane->property("currentIndex").toInt();
    QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    QTRY_COMPARE(pane->property("tabCount").toInt(), originalCount + 1);
    auto* browser = namedObject(QStringLiteral("fileBrowser"));
    QVERIFY(browser);
    QTRY_COMPARE(browser->property("appliedSessionKey").toString(),
                 pane->property("activeTabId").toString());
    QTRY_VERIFY(!browser->property("pendingSessionChange").toBool());
    local->changeDirectory(secondPath);
    QTRY_COMPARE(local->currentPath(), secondPath);
    QVERIFY(QMetaObject::invokeMethod(pane, "activate", Q_ARG(QVariant, firstIndex)));
    QTRY_COMPARE(local->currentPath(), firstPath);
    QVERIFY(QMetaObject::invokeMethod(pane, "activate", Q_ARG(QVariant, originalCount)));
    QTRY_COMPARE(local->currentPath(), secondPath);

    m_window->setProperty("inspectorMode", 0);
    QTRY_VERIFY(!local->active());
    QVERIFY(!settings->fileBrowserVisible());
    bitshell::core::AppSettings persisted;
    QVERIFY(!persisted.fileBrowserVisible());
    m_window->setProperty("inspectorMode", 1);
    QTRY_COMPARE(local->currentPath(), secondPath);

    QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, originalCount)));
    QTRY_COMPARE(pane->property("tabCount").toInt(), originalCount);
    m_window->setProperty("inspectorMode", 0);
}

void UiInteractionTest::threeDotMenusOpenAndEditActionWorks() {
    auto* catalogButton = qobject_cast<QQuickItem*>(
        namedObject(QStringLiteral("catalogMenuButton")));
    QVERIFY(catalogButton != nullptr);
    clickItem(catalogButton);
    auto* catalogMenu = namedObject(QStringLiteral("catalogContextMenu"));
    QTRY_VERIFY(catalogMenu != nullptr && catalogMenu->property("visible").toBool());
    QVERIFY(catalogMenu->property("height").toReal() < 480.0);
    QVERIFY(QMetaObject::invokeMethod(catalogMenu, "close"));
    QTest::qWait(100);

    auto* folderRow = treeRow(QStringLiteral("00 Source"), true);
    QVERIFY(folderRow != nullptr);
    auto* folderButton = folderRow->findChild<QQuickItem*>(
        QStringLiteral("treeMenuButton"), Qt::FindChildrenRecursively);
    clickItem(folderButton);
    auto* folderMenu = folderRow->findChild<QObject*>(
        QStringLiteral("folderContextMenu"), Qt::FindChildrenRecursively);
    QTRY_VERIFY(folderMenu != nullptr && folderMenu->property("visible").toBool());
    QVERIFY(folderMenu->property("height").toReal() < 310.0);
    auto* renameFolderItem = folderMenu->findChild<QQuickItem*>(
        QStringLiteral("renameFolderMenuItem"), Qt::FindChildrenRecursively);
    QVERIFY(renameFolderItem != nullptr);
    clickItem(renameFolderItem);
    auto* folderEditor = namedObject(QStringLiteral("folderEditorDialog"));
    QTRY_VERIFY(folderEditor != nullptr && folderEditor->property("visible").toBool());
    QCOMPARE(namedObject(QStringLiteral("catalogSidebar"))
                 ->property("folderDialogPath")
                 .toString(),
             QStringLiteral("00 Source"));
    QVERIFY(QMetaObject::invokeMethod(folderEditor, "close"));
    QTest::qWait(120);

    auto* sessionRow = treeRow(QStringLiteral("move-me"), false);
    QVERIFY(sessionRow != nullptr);
    auto* sessionButton = sessionRow->findChild<QQuickItem*>(
        QStringLiteral("treeMenuButton"), Qt::FindChildrenRecursively);
    clickItem(sessionButton);
    auto* sessionMenu = sessionRow->findChild<QObject*>(
        QStringLiteral("sessionContextMenu"), Qt::FindChildrenRecursively);
    QTRY_VERIFY(sessionMenu != nullptr && sessionMenu->property("visible").toBool());
    QVERIFY(sessionMenu->property("height").toReal() < 260.0);

    auto* editItem = sessionMenu->findChild<QQuickItem*>(
        QStringLiteral("editSessionMenuItem"), Qt::FindChildrenRecursively);
    QVERIFY(editItem != nullptr);
    clickItem(editItem);
    auto* editor = namedObject(QStringLiteral("sessionEditorDialog"));
    QTRY_VERIFY(editor != nullptr && editor->property("visible").toBool());
    QCOMPARE(editor->property("editIndex").toInt(), 0);
    auto* sessionBackspace = editor->findChild<QObject*>(
        QStringLiteral("sessionBackspaceCombo"), Qt::FindChildrenRecursively);
    QVERIFY(sessionBackspace != nullptr);
    QCOMPARE(sessionBackspace->property("currentValue").toString(),
             QStringLiteral("bs"));
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTest::qWait(120);
}

void UiInteractionTest::sessionInspectorCopiesFullSessionDetails() {
    auto* sidebar = namedObject(QStringLiteral("catalogSidebar"));
    QVERIFY(sidebar);
    const QVariantMap node{
        {QStringLiteral("kind"), QStringLiteral("session")},
        {QStringLiteral("key"), QStringLiteral("session:move-me")},
        {QStringLiteral("name"), QStringLiteral("move-me")},
        {QStringLiteral("host"), QStringLiteral("move.example.test")},
        {QStringLiteral("user"), QStringLiteral("operator")},
        {QStringLiteral("port"), 22},
        {QStringLiteral("group"), QStringLiteral("00 Source/Nested")},
    };
    QVERIFY(QMetaObject::invokeMethod(sidebar, "inspectNode", Q_ARG(QVariant, QVariant::fromValue(node))));

    auto* inspector = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("sessionInspector")));
    QVERIFY(inspector);
    const auto values = [](QQuickItem* root) {
        QList<QQuickItem*> fields;
        const auto walk = [&fields](auto&& self, QQuickItem* item) -> void {
            if (item == nullptr)
                return;
            if (item->objectName() == QLatin1String("sessionInspectorValue"))
                fields.append(item);
            for (auto* child : item->childItems())
                self(self, child);
        };
        walk(walk, root);
        return fields;
    };
    QList<QQuickItem*> fields;
    QQuickItem* host = nullptr;
    for (int attempt = 0; attempt < 50 && host == nullptr; ++attempt) {
        if (attempt > 0)
            QTest::qWait(20);
        fields = values(inspector);
        host = nullptr;
        for (auto* field : fields) {
            if (field->property("text").toString() == QStringLiteral("move.example.test"))
                host = field;
        }
    }
    QCOMPARE(fields.size(), 6);
    for (auto* field : fields) {
        QVERIFY(field->property("readOnly").toBool());
        QVERIFY(field->property("selectByMouse").toBool());
        QVERIFY(field->height() > 8.0);
    }
    QVERIFY(host);
    QVERIFY(QMetaObject::invokeMethod(host, "select", Q_ARG(int, 0), Q_ARG(int, 4)));
    QCOMPARE(host->property("selectedText").toString(), QStringLiteral("move"));

    auto* menu = namedObject(QStringLiteral("sessionInspectorCopyMenu"));
    QVERIFY(menu);
    menu->setProperty("valueField", QVariant::fromValue(static_cast<QObject*>(host)));
    auto* copyItem = menu->findChild<QObject*>(QStringLiteral("sessionInspectorCopyItem"));
    QVERIFY(copyItem);
    QTRY_VERIFY(copyItem->property("enabled").toBool());
    QGuiApplication::clipboard()->setText(QStringLiteral("stale-inspector-clipboard"));
    QVERIFY(QMetaObject::invokeMethod(copyItem, "triggered"));
    QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("move.example.test"));
    QCOMPARE(host->property("selectedText").toString(), QStringLiteral("move.example.test"));
}

void UiInteractionTest::catalogBackgroundAndFolderRightClickCreateInExpectedLocation() {
    auto* sidebar = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("catalogSidebar")));
    auto* contextTarget = visualItem(QStringLiteral("catalogAreaContextTarget"));
    auto* editor = namedObject(QStringLiteral("sessionEditorDialog"));
    QVERIFY(sidebar && contextTarget && editor);
    QSignalSpy addSessionSpy(sidebar, SIGNAL(addSessionRequested(QString)));
    QVERIFY(addSessionSpy.isValid());

    // A no-result view gives us genuine catalog background even when the test
    // fixture is larger than the viewport.
    sidebar->setProperty("filterText", QStringLiteral("no-right-click-match.example.test"));
    QTRY_COMPARE(sidebar->property("searchResultCount").toInt(), 0);
    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier,
                      itemPoint(contextTarget, 0.73), 20);
    auto* catalogMenu = namedObject(QStringLiteral("catalogAreaContextMenu"));
    QTRY_VERIFY(catalogMenu && catalogMenu->property("opened").toBool());
    captureFrame(QStringLiteral("catalog-background-context-menu.png"));
    clickItem(qobject_cast<QQuickItem*>(
        namedObject(QStringLiteral("catalogAreaNewSessionItem"))));
    QTRY_COMPARE(addSessionSpy.count(), 1);
    QCOMPARE(addSessionSpy.takeFirst().constFirst().toString(), QString{});
    QTRY_VERIFY(editor->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTest::qWait(100);

    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier,
                      itemPoint(contextTarget, 0.32), 20);
    QTRY_VERIFY(catalogMenu->property("opened").toBool());
    clickItem(qobject_cast<QQuickItem*>(
        namedObject(QStringLiteral("catalogAreaNewFolderItem"))));
    auto* folderEditor = namedObject(QStringLiteral("folderEditorDialog"));
    QTRY_VERIFY(folderEditor && folderEditor->property("opened").toBool());
    QCOMPARE(sidebar->property("folderDialogMode").toString(), QStringLiteral("create"));
    QCOMPARE(sidebar->property("folderDialogPath").toString(), QString{});
    QVERIFY(QMetaObject::invokeMethod(folderEditor, "close"));
    QTest::qWait(100);

    // The same pointer path delegates row hits to the folder menu and carries
    // that precise folder into the new-session editor.
    sidebar->setProperty("filterText", QString{});
    auto* folderRow = treeRow(QStringLiteral("00 Source"), true);
    QTRY_VERIFY(folderRow != nullptr);
    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier,
                      itemPoint(folderRow, 0.55), 20);
    auto* folderMenu = folderRow->findChild<QObject*>(
        QStringLiteral("folderContextMenu"), Qt::FindChildrenRecursively);
    QTRY_VERIFY(folderMenu && folderMenu->property("opened").toBool());
    auto* newSession = folderMenu->findChild<QQuickItem*>(
        QStringLiteral("newSessionInFolderMenuItem"), Qt::FindChildrenRecursively);
    QVERIFY(newSession);
    captureFrame(QStringLiteral("catalog-folder-context-menu.png"));
    clickItem(newSession);
    QTRY_COMPARE(addSessionSpy.count(), 1);
    QCOMPARE(addSessionSpy.takeFirst().constFirst().toString(), QStringLiteral("00 Source"));
    QTRY_VERIFY(editor->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTest::qWait(100);

    // The catalog-level pointer layer must not steal the existing session menu.
    auto* sessionRow = treeRow(QStringLiteral("move-me"), false);
    QVERIFY(sessionRow);
    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier,
                      itemPoint(sessionRow, 0.55), 20);
    auto* sessionMenu = sessionRow->findChild<QObject*>(
        QStringLiteral("sessionContextMenu"), Qt::FindChildrenRecursively);
    QTRY_VERIFY(sessionMenu && sessionMenu->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(sessionMenu, "close"));
    QTest::qWait(100);
}

bool UiInteractionTest::beginDrag(QQuickItem* source) {
    const QPoint start = itemPoint(source, 0.36);
    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, start, 20);
    QTest::mouseMove(m_window, start + QPoint(28, 2), 70);

    auto* sidebar = namedObject(QStringLiteral("catalogSidebar"));
    for (int attempt = 0; attempt < 30; ++attempt) {
        if (sidebar != nullptr && sidebar->property("catalogDragActive").toBool()) {
            return true;
        }
        QTest::qWait(20);
    }
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier,
                        start + QPoint(28, 2), 20);
    return false;
}

void UiInteractionTest::finishDragAt(QQuickItem* destination) {
    const QPoint destinationPoint = itemPoint(destination, 0.45);
    QTest::mouseMove(m_window, destinationPoint, 100);
    QTest::qWait(80);
    auto* sidebar = namedObject(QStringLiteral("catalogSidebar"));
    QVERIFY(sidebar != nullptr && sidebar->property("catalogDragActive").toBool());
    auto* dropArea = destination->findChild<QQuickItem*>(
        QStringLiteral("folderDropArea"), Qt::FindChildrenRecursively);
    if (dropArea == nullptr) {
        dropArea = destination->findChild<QQuickItem*>(
            QStringLiteral("catalogRootDropArea"), Qt::FindChildrenRecursively);
    }
    QVERIFY(dropArea != nullptr);
    QTRY_VERIFY2(dropArea->property("containsDrag").toBool(),
                 "The destination DropArea never received the active catalog drag");
    captureFrame(QStringLiteral("drag-target.png"));
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, destinationPoint, 30);
    QTRY_VERIFY(sidebar != nullptr
                && !sidebar->property("catalogDragActive").toBool());
}

void UiInteractionTest::cancelDragAt(const QPoint& destination) {
    QTest::mouseMove(m_window, destination, 60);
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, destination, 20);
    auto* sidebar = namedObject(QStringLiteral("catalogSidebar"));
    QTRY_VERIFY(sidebar != nullptr
                && !sidebar->property("catalogDragActive").toBool());
}

void UiInteractionTest::dragNearEdgeScrollsLargeCatalog() {
    auto* source = treeRow(QStringLiteral("00 Source"), true);
    auto* list = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("sessionTreeList")));
    QVERIFY(source != nullptr);
    QVERIFY(list != nullptr);
    list->setProperty("contentY", list->property("originY"));
    QTest::qWait(30);
    const qreal initialY = list->property("contentY").toReal();

    QVERIFY(beginDrag(source));
    const QPoint bottomEdge = list->mapToScene(
        QPointF(list->width() * 0.45, list->height() - 9)).toPoint();
    QTest::mouseMove(m_window, bottomEdge, 80);
    QTest::qWait(320);
    QVERIFY2(list->property("contentY").toReal() > initialY + 25.0,
             "Holding a dragged row at the lower edge did not scroll the catalog");
    captureFrame(QStringLiteral("drag-edge-scroll.png"));

    cancelDragAt(QPoint(520, 330));
    list->setProperty("contentY", list->property("originY"));
    QTest::qWait(80);
}

QString UiInteractionTest::persistedSessionGroup(const QString& id) const {
    QFile file(bitshellConfigPath(QStringLiteral("sessions.yaml")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QVariantMap document;
    QString error;
    if (!bitshell::core::YamlFile::decode(file.readAll(), QStringLiteral("8bitshell/sessions"), document, error, 2)) return {};
    for (const auto& value : document.value(QStringLiteral("sessions")).toList()) {
        const auto session = value.toMap();
        if (session.value(QStringLiteral("id")).toString() == id)
            return session.value(QStringLiteral("folder")).toString();
    }
    return {};
}
QStringList UiInteractionTest::persistedFolders() const {
    QFile file(bitshellConfigPath(QStringLiteral("sessions.yaml")));
    if (!file.open(QIODevice::ReadOnly)) return {};
    QVariantMap document;
    QString error;
    if (!bitshell::core::YamlFile::decode(file.readAll(), QStringLiteral("8bitshell/sessions"), document, error, 2)) return {};
    return document.value(QStringLiteral("folders")).toStringList();
}

void UiInteractionTest::dragsSessionIntoFolder() {
    auto* session = treeRow(QStringLiteral("move-me"), false);
    auto* target = treeRow(QStringLiteral("01 Target"), true);
    QVERIFY(session != nullptr);
    QVERIFY(target != nullptr);
    QVERIFY(beginDrag(session));
    finishDragAt(target);
    QTRY_COMPARE(persistedSessionGroup(QStringLiteral("move-me")),
                 QStringLiteral("01 Target"));

    QQuickItem* movedSession = nullptr;
    QTRY_VERIFY((movedSession = treeRow(QStringLiteral("move-me"), false)) != nullptr);
    QTRY_COMPARE(movedSession->property("nodePath").toString(),
                 QStringLiteral("01 Target"));
}

void UiInteractionTest::dragsWholeFolderTreeAndBackToRoot() {
    auto* source = treeRow(QStringLiteral("00 Source"), true);
    auto* target = treeRow(QStringLiteral("01 Target"), true);
    QVERIFY(source != nullptr);
    QVERIFY(target != nullptr);
    QVERIFY(beginDrag(source));
    finishDragAt(target);

    QTRY_COMPARE(persistedSessionGroup(QStringLiteral("stay-nested")),
                 QStringLiteral("01 Target/00 Source/Nested"));
    QTRY_VERIFY(persistedFolders().contains(QStringLiteral("01 Target/00 Source/Nested")));

    QQuickItem* movedFolder = nullptr;
    QTRY_VERIFY((movedFolder = treeRow(QStringLiteral("00 Source"), true)) != nullptr);
    QCOMPARE(movedFolder->property("nodePath").toString(),
             QStringLiteral("01 Target/00 Source"));
    auto* rootTarget = qobject_cast<QQuickItem*>(
        namedObject(QStringLiteral("catalogRootDropTarget")));
    QVERIFY(rootTarget != nullptr);
    QVERIFY(beginDrag(movedFolder));
    finishDragAt(rootTarget);

    QTRY_COMPARE(persistedSessionGroup(QStringLiteral("stay-nested")),
                 QStringLiteral("00 Source/Nested"));
    QTRY_VERIFY(persistedFolders().contains(QStringLiteral("00 Source/Nested")));
    QVERIFY(!persistedFolders().contains(QStringLiteral("01 Target/00 Source")));
}

void UiInteractionTest::themeSwitchWorkload() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(settings);
    const bool wasSeparate = settings->separateTerminalTheme();
    settings->setSeparateTerminalTheme(true);
    const auto restoreSeparation = qScopeGuard([&] { settings->setSeparateTerminalTheme(wasSeparate); });
    settings->setAppearanceMode(QStringLiteral("dark"));
    settings->setUiThemeId(QStringLiteral("blueberry"));
    settings->setTerminalThemeId(QStringLiteral("blueberry"));
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Shortcuts"))));
    QTest::qWait(200);
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    auto* terminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
    QVERIFY(terminal);
    QByteArray output;
    for (int line = 0; line < 2000; ++line) output += "\x1b[31mERROR\x1b[0m deterministic theme change fixture\r\n";
    bitshell::terminal::TerminalTestAccess::feed(*terminal, output);
    terminal->setSearchText(QStringLiteral("ERROR"));
    terminal->scrollToBottom();
    for (const bool ui : {true, false}) {
        QList<double> setters, frames;
        for (int sample = 0; sample < 8; ++sample) {
            const QString id = sample % 2 ? QStringLiteral("blueberry") : QStringLiteral("dracula");
            QElapsedTimer timer;
            QSignalSpy paletteChanges(m_window, m_window->metaObject()->property(m_window->metaObject()->indexOfProperty("palette")).notifySignal());
            QVERIFY(paletteChanges.isValid());
            timer.start();
            if (ui) settings->setUiThemeId(id);
            else settings->setTerminalThemeId(id);
            setters << double(timer.nsecsElapsed()) / 1000000.0;
            QCOMPARE(paletteChanges.size(), ui ? 1 : 0);
            QCoreApplication::processEvents();
            QVERIFY(!m_window->grabWindow().isNull());
            frames << double(timer.nsecsElapsed()) / 1000000.0;
        }
        std::sort(setters.begin(), setters.end());
        std::sort(frames.begin(), frames.end());
        qInfo().nospace() << (ui ? "UI" : "Terminal") << " theme setter ms median=" << setters[4]
                         << " max=" << setters.last() << "; including offscreen frame ms median="
                         << frames[4] << " max=" << frames.last();
    }
    terminal->clearSearch();
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
}

void UiInteractionTest::themeMenusRenderingAndSshIcons() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(settings);
    const auto originalFont = settings->terminalFontFamily();
    settings->setAppearanceMode(QStringLiteral("dark"));
    settings->setUiThemeId(QStringLiteral("blueberry"));
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    for (const auto& name : {QStringLiteral("uiThemeCombo"), QStringLiteral("terminalAntialiasingCombo"), QStringLiteral("terminalHintingCombo"), QStringLiteral("terminalFontCombo")}) {
        const bool appearancePage = name == QStringLiteral("uiThemeCombo");
        QVERIFY(QMetaObject::invokeMethod(drawer, "openSection",
                                          Q_ARG(QVariant, appearancePage ? QStringLiteral("Appearance")
                                                                        : QStringLiteral("Terminal"))));
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto* scroll = namedObject(appearancePage ? QStringLiteral("appearanceSettingsScroll")
                                                  : QStringLiteral("terminalSettingsScroll"));
        QVERIFY(scroll);
        auto* flick = scroll->property("contentItem").value<QObject*>();
        auto* content = qobject_cast<QQuickItem*>(flick->property("contentItem").value<QObject*>());
        QVERIFY(flick && content);
        auto* combo = qobject_cast<QQuickItem*>(namedObject(name));
        QVERIFY(combo);
        auto* popup = combo->property("popup").value<QObject*>();
        QVERIFY(popup);
        auto* list = popup->property("contentItem").value<QObject*>();
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 0);
        flick->setProperty("contentY", std::max(0.0, combo->mapToItem(content, QPointF{}).y() - 100.0));
        QTest::qWait(50);
        clickItem(combo);
        QTRY_VERIFY(popup->property("visible").toBool());
        QTRY_COMPARE(list->property("count").toInt(), combo->property("count").toInt());
        QVERIFY(popup->property("height").toReal() <= 300.0);
        QTest::keyClick(m_window, Qt::Key_Down);
        QTest::keyClick(m_window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QTRY_COMPARE(list->property("count").toInt(), 0);
        if (name == QStringLiteral("terminalAntialiasingCombo"))
            QCOMPARE(settings->terminalTextAntialiasing(), QStringLiteral("grayscale"));
        if (name == QStringLiteral("terminalHintingCombo"))
            QCOMPARE(settings->terminalFontHinting(), QStringLiteral("none"));
    }
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    auto* terminal = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(terminal);
    QCOMPARE(terminal->property("textAntialiasing").toString(), QStringLiteral("grayscale"));
    QCOMPARE(terminal->property("fontHinting").toString(), QStringLiteral("none"));
    auto* smoothing = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("terminalAntialiasingCombo")));
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Terminal"))));
    auto* terminalScroll = namedObject(QStringLiteral("terminalSettingsScroll"));
    auto* terminalFlick = terminalScroll->property("contentItem").value<QObject*>();
    auto* terminalContent = qobject_cast<QQuickItem*>(terminalFlick->property("contentItem").value<QObject*>());
    QVERIFY(terminalFlick && terminalContent && smoothing);
    terminalFlick->setProperty("contentY", std::max(0.0, smoothing->mapToItem(terminalContent, QPointF{}).y() - 100.0));
    QTest::qWait(50);
    captureFrame(QStringLiteral("terminal-rendering-settings.png"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTest::qWait(200);
    QList<QQuickItem*> icons;
    QList<QQuickItem*> pending{m_window->contentItem()};
    while (!pending.isEmpty()) {
        auto* item = pending.takeLast();
        pending.append(item->childItems());
        if (item->objectName() == QStringLiteral("sessionSshLock")) icons << item;
    }
    QVERIFY(icons.isEmpty()); // Protocol indicators now belong to open SSH tabs, not catalog rows.
    settings->setTerminalTextAntialiasing(QStringLiteral("system"));
    settings->setTerminalFontHinting(QStringLiteral("default"));
    settings->setTerminalFontFamily(originalFont);
    settings->setUiThemeId(QStringLiteral("blueberry"));
}

void UiInteractionTest::settingsDropdownScrollingKeepsRowsVisible_data() {
    QTest::addColumn<QString>("paletteId");
    QTest::newRow("dark") << QStringLiteral("blueberry");
    QTest::newRow("light") << QStringLiteral("blueberry-light");
}

void UiInteractionTest::settingsDropdownScrollingKeepsRowsVisible() {
    QFETCH(QString, paletteId);
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(settings);
    const bool wasSeparate = settings->separateTerminalTheme();
    settings->setSeparateTerminalTheme(true);
    const auto restoreSeparation = qScopeGuard([&] { settings->setSeparateTerminalTheme(wasSeparate); });
    const auto originalFont = settings->terminalFontFamily();
    const auto originalUi = settings->uiThemeId();
    const auto originalMode = settings->appearanceMode();
    const auto originalDayUi = settings->uiLightThemeId();
    const auto originalTerminalTheme = settings->terminalThemeId();
    settings->setAppearanceMode(QStringLiteral("dark"));
    settings->setUiThemeId(paletteId);
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(pane);
    QVariant activeId;
    QVERIFY(QMetaObject::invokeMethod(pane, "tabIdAt", Q_RETURN_ARG(QVariant, activeId),
                                     Q_ARG(QVariant, pane->property("currentIndex"))));
    auto* activeTab = visualItem(QStringLiteral("openTab:") + activeId.toString());
    QVERIFY(activeTab);
    // UI chrome must not borrow the independently selected terminal background.
    QTRY_COMPARE(activeTab->property("color").value<QColor>(), m_window->color());
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    const auto restore = qScopeGuard([&] {
        for (const auto& name : {QStringLiteral("uiLightThemeCombo"), QStringLiteral("terminalThemeCombo"),
                                 QStringLiteral("terminalFontCombo")}) {
            if (auto* picker = namedObject(name)) {
                if (auto* popup = picker->property("popup").value<QObject*>())
                    QMetaObject::invokeMethod(popup, "close");
            }
        }
        QMetaObject::invokeMethod(drawer, "close");
        settings->setTerminalFontFamily(originalFont);
        settings->setUiLightThemeId(originalDayUi);
        settings->setTerminalThemeId(originalTerminalTheme);
        settings->setUiThemeId(originalUi);
        settings->setAppearanceMode(originalMode);
        QCoreApplication::processEvents();
    });
    // Checking only count/currentIndex missed this regression: two ListViews
    // could take turns parenting the shared delegates, leaving blank rows.
    const auto verifyRows = [this](QQuickItem* combo, QQuickItem* list) {
        QTest::qWait(80);
        const auto frame = m_window->grabWindow();
        QVERIFY(!frame.isNull());
        auto* items = qobject_cast<QQuickItem*>(list->property("contentItem").value<QObject*>());
        QVERIFY(items);
        int visibleRows = 0;
        for (auto* item : items->childItems()) {
            if (!item->property("index").isValid()) continue;
            const auto bounds = item->mapRectToItem(list, item->boundingRect());
            if (bounds.top() < -0.5 || bounds.bottom() > list->height() + 0.5) continue;
            ++visibleRows;
            QVERIFY(item->isVisible());
            auto* label = qobject_cast<QQuickItem*>(item->property("contentItem").value<QObject*>());
            QVERIFY(label);
            QString expected;
            QVERIFY(QMetaObject::invokeMethod(combo, "textAt", Q_RETURN_ARG(QString, expected),
                                             Q_ARG(int, item->property("index").toInt())));
            QVERIFY(!expected.isEmpty());
            QCOMPARE(label->property("text").toString(), expected);
            QCOMPARE(label->property("textFormat").toInt(), 0); // Text.PlainText
            const auto logical = label->mapRectToScene(label->boundingRect());
            const qreal scale = qreal(frame.width()) / m_window->width();
            const auto rect = QRectF(logical.topLeft() * scale, logical.size() * scale)
                                  .toAlignedRect().intersected(frame.rect());
            const auto ink = label->property("color").value<QColor>();
            int textPixels = 0;
            for (int y = rect.top(); y <= rect.bottom(); ++y) {
                for (int x = rect.left(); x <= rect.right(); ++x) {
                    const auto pixel = frame.pixelColor(x, y);
                    // Allow antialiased edge pixels in small UI fonts.
                    if (qAbs(pixel.red() - ink.red()) + qAbs(pixel.green() - ink.green())
                            + qAbs(pixel.blue() - ink.blue()) < 180) ++textPixels;
                }
            }
            QVERIFY2(textPixels > 5, qPrintable(QStringLiteral("Dropdown row has no rendered text: %1").arg(expected)));
        }
        const int minimumRows = std::min(combo->property("count").toInt(), int(list->height() / 32) - 1);
        QVERIFY2(visibleRows >= minimumRows, "The popup lost visible delegates while scrolling");
    };
    for (const auto& name : {QStringLiteral("uiLightThemeCombo"), QStringLiteral("terminalThemeCombo"),
                             QStringLiteral("terminalFontCombo")}) {
        const bool terminalPage = name == QStringLiteral("terminalFontCombo");
        QVERIFY(QMetaObject::invokeMethod(drawer, "openSection",
                                          Q_ARG(QVariant, terminalPage ? QStringLiteral("Terminal")
                                                                      : QStringLiteral("Appearance"))));
        QTRY_VERIFY(drawer->property("opened").toBool());
        auto* scroll = namedObject(terminalPage ? QStringLiteral("terminalSettingsScroll")
                                                : QStringLiteral("appearanceSettingsScroll"));
        QVERIFY(scroll);
        auto* flick = scroll->property("contentItem").value<QObject*>();
        QVERIFY(flick);
        auto* content = qobject_cast<QQuickItem*>(flick->property("contentItem").value<QObject*>());
        QVERIFY(content);
        auto* combo = qobject_cast<QQuickItem*>(namedObject(name));
        QVERIFY(combo);
        const int originalIndex = combo->property("currentIndex").toInt();
        const qreal maximumOuterY = std::max(
            0.0,
            flick->property("contentHeight").toReal() - flick->property("height").toReal());
        flick->setProperty(
            "contentY",
            qBound(0.0, combo->mapToItem(content, QPointF{}).y() - 100.0, maximumOuterY));
        QTest::qWait(60);
        const auto outerScrollY = flick->property("contentY").toReal();
        const bool outerScrollable = flick->property("contentHeight").toReal()
                                     > flick->property("height").toReal() + 0.5;
        auto* popup = combo->property("popup").value<QObject*>();
        QVERIFY(popup);
        auto* list = qobject_cast<QQuickItem*>(popup->property("contentItem").value<QObject*>());
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(), 0);
        clickItem(combo);
        QTRY_VERIFY(popup->property("opened").toBool());
        QTest::keyClick(list->window(), Qt::Key_Home);
        QTRY_COMPARE(combo->property("highlightedIndex").toInt(), 0);
        verifyRows(combo, list);
        // Walk past the initially instantiated rows; no direct currentIndex setter.
        for (int i = 0; i < std::min(20, combo->property("count").toInt() - 1); ++i) {
            QTest::keyClick(list->window(), Qt::Key_Down);
            QTest::qWait(10);
        }
        verifyRows(combo, list);
        QTest::keyClick(list->window(), Qt::Key_End);
        QTRY_COMPARE(combo->property("highlightedIndex").toInt(), combo->property("count").toInt() - 1);
        verifyRows(combo, list);
        captureFrame(name + u'-' + QString::fromLatin1(QTest::currentDataTag()) + QStringLiteral("-keyboard-bottom.png"));
        QTest::keyClick(list->window(), Qt::Key_Home);
        verifyRows(combo, list);
        const QPoint point = itemPoint(list);
        const bool scrollable = list->property("contentHeight").toReal() > list->height() + 0.5;
        for (int direction : {-1, 1}) {
            const auto beforeY = list->property("contentY").toReal();
            for (int step = 0; step < 6; ++step) {
                // Keep window-local logical coordinates at fractional DPR, and
                // advance time like QTest's mouse events. Repeated timestamp-zero
                // wheel events cannot reverse a Flickable's scrolling velocity.
                QWheelEvent wheel(point, list->window()->mapToGlobal(point), QPoint(), QPoint(0, 120 * direction),
                                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                QTest::lastMouseTimestamp += 30;
                wheel.setTimestamp(static_cast<ulong>(QTest::lastMouseTimestamp));
                QGuiApplication::sendEvent(list->window(), &wheel);
                QTest::qWait(30);
            }
            if (scrollable) {
                QTRY_VERIFY2(direction < 0 ? list->property("contentY").toReal() > beforeY
                                          : list->property("contentY").toReal() < beforeY,
                             qPrintable(QStringLiteral("Wheel did not scroll %1; direction=%2, before=%3, after=%4")
                                 .arg(name).arg(direction).arg(beforeY).arg(list->property("contentY").toReal())));
            } else {
                QCOMPARE(list->property("contentY").toReal(), beforeY);
            }
            // Let wheel inertia end before measuring rows or starting a separate
            // keyboard gesture; otherwise a late frame can move the End row away.
            QTRY_VERIFY(!list->property("moving").toBool());
            verifyRows(combo, list);
            if (outerScrollable)
                QCOMPARE(flick->property("contentY").toReal(), outerScrollY);
            else
                QCOMPARE(flick->property("contentY").toReal(), 0.0);
        }
        // Mouse selection and reopening at a late item must also remain usable.
        QTest::keyClick(list->window(), Qt::Key_End);
        verifyRows(combo, list);
        const int lastIndex = combo->property("count").toInt() - 1;
        QQuickItem* lastRow = nullptr;
        QTRY_VERIFY2((lastRow = visualItem(QStringLiteral("comboOption:%1:%2").arg(name).arg(lastIndex))) != nullptr,
                     qPrintable(QStringLiteral("Missing final row in %1; contentY=%2, highlighted=%3, count=%4")
                                    .arg(name).arg(list->property("contentY").toReal())
                                    .arg(combo->property("highlightedIndex").toInt()).arg(list->property("count").toInt())));
        const auto lastText = lastRow->property("text").toString();
        clickItem(lastRow);
        QTRY_VERIFY(!popup->property("visible").toBool());
        // Selecting an installed font can remove the missing configured font
        // prepended by SettingsDrawer, shifting the model's indexes by one.
        QCOMPARE(combo->property("currentText").toString(), lastText);
        QTRY_COMPARE(list->property("count").toInt(), 0);
        clickItem(combo);
        QTRY_VERIFY(popup->property("opened").toBool());
        verifyRows(combo, list);
        const int selectedIndex = combo->property("currentIndex").toInt();
        QTest::keyClick(list->window(), Qt::Key_Up);
        QTest::keyClick(list->window(), Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), selectedIndex - 1);
        QTRY_COMPARE(list->property("count").toInt(), 0);
        if (name == QStringLiteral("terminalFontCombo")) settings->setTerminalFontFamily(originalFont);
        else {
            combo->setProperty("currentIndex", originalIndex);
            QVERIFY(QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, originalIndex)));
        }
    }
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    settings->setUiThemeId(originalUi);
    settings->setAppearanceMode(originalMode);
}

void UiInteractionTest::switchesUiAndTerminalThemesIndependently() {
    auto* preferences = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(preferences);
    const bool wasSeparate = preferences->separateTerminalTheme();
    preferences->setSeparateTerminalTheme(true);
    const auto restoreSeparation = qScopeGuard([&] { preferences->setSeparateTerminalTheme(wasSeparate); });
    auto* uiTheme = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("uiThemeCombo")));
    auto* terminalTheme = qobject_cast<QQuickItem*>(
        namedObject(QStringLiteral("terminalThemeCombo")));
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(uiTheme != nullptr);
    QVERIFY(terminalTheme != nullptr);
    QVERIFY(pane != nullptr);
    QCOMPARE(uiTheme->property("count").toInt(), 24);
    QCOMPARE(terminalTheme->property("count").toInt(), 32);

    terminalTheme->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(terminalTheme, "activated", Q_ARG(int, 0)));
    uiTheme->setProperty("currentIndex", 3); // Nord
    QVERIFY(QMetaObject::invokeMethod(uiTheme, "activated", Q_ARG(int, 3)));
    QTRY_COMPARE(m_window->property("color").value<QColor>(),
                 QColor(QStringLiteral("#242933")));

    QObject* activeTerminal = pane->property("activeTerminal").value<QObject*>();
    QTRY_VERIFY((activeTerminal = pane->property("activeTerminal").value<QObject*>())
                != nullptr);
    QCOMPARE(activeTerminal->property("colorScheme").toMap()
                 .value(QStringLiteral("background")).toString(),
             QStringLiteral("#070b11"));

    terminalTheme->setProperty("currentIndex", 1); // Dracula
    QVERIFY(QMetaObject::invokeMethod(terminalTheme, "activated", Q_ARG(int, 1)));
    QTRY_COMPARE(activeTerminal->property("colorScheme").toMap()
                     .value(QStringLiteral("background")).toString(),
                 QStringLiteral("#282a36"));
    QCOMPARE(m_window->property("color").value<QColor>(),
             QColor(QStringLiteral("#242933")));
    auto* renderedTerminal = qobject_cast<bitshell::terminal::TerminalView*>(activeTerminal);
    QVERIFY(renderedTerminal);
    bitshell::terminal::TerminalTestAccess::feed(*renderedTerminal,
        "\033[31mRED \033[32mGREEN \033[34mBLUE \033[35mMAGENTA \033[36mCYAN\033[0m\r\n");
    QTest::qWait(120);
    captureFrame(QStringLiteral("themes-independent.png"));

    bitshell::core::AppSettings settings;
    QCOMPARE(settings.uiThemeId(),
             QStringLiteral("nord"));
    QCOMPARE(settings.terminalThemeId(),
             QStringLiteral("dracula"));

    uiTheme->setProperty("currentIndex", 0);
    terminalTheme->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(uiTheme, "activated", Q_ARG(int, 0)));
    QVERIFY(QMetaObject::invokeMethod(terminalTheme, "activated", Q_ARG(int, 0)));
}

void UiInteractionTest::switchesDayNightMatrixAndAutomaticModes() {
    auto* preferences = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(preferences);
    const bool wasSeparate = preferences->separateTerminalTheme();
    preferences->setSeparateTerminalTheme(true);
    const auto restoreSeparation = qScopeGuard([&] { preferences->setSeparateTerminalTheme(wasSeparate); });
    auto* mode = namedObject(QStringLiteral("appearanceModeCombo"));
    auto* uiDay = namedObject(QStringLiteral("uiLightThemeCombo"));
    auto* terminalDay = namedObject(QStringLiteral("terminalLightThemeCombo"));
    auto* toggle = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("appearanceToggle")));
    auto* sun = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("appearanceSunIcon")));
    auto* moon = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("appearanceMoonIcon")));
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(mode && uiDay && terminalDay && toggle && sun && moon && pane);
    QObject* terminal = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(terminal);
    const auto choose = [](QObject* combo, int index) {
        combo->setProperty("currentIndex", index);
        return QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, index));
    };
    QVERIFY(choose(uiDay, 18)); // GitHub Light
    QVERIFY(choose(terminalDay, 15)); // Blueberry Light
    QCOMPARE(m_window->property("color").value<QColor>(), QColor(QStringLiteral("#090d14")));
    QVERIFY(moon->opacity() > sun->opacity());
    clickItem(toggle);
    QTRY_COMPARE(mode->property("currentValue").toString(), QStringLiteral("light"));
    QTRY_COMPARE(m_window->property("color").value<QColor>(), QColor(QStringLiteral("#f6f8fa")));
    QTRY_VERIFY(sun->opacity() > 0.99 && moon->opacity() < 0.01);
    QTRY_COMPARE(terminal->property("colorScheme").toMap().value(QStringLiteral("background")).toString(),
                 QStringLiteral("#fff8fb"));
    QTest::qWait(120);
    captureFrame(QStringLiteral("day-workspace.png"));
    QVERIFY(choose(terminalDay, 3)); // Mixed: light UI with Nord terminal
    QTRY_COMPARE(terminal->property("colorScheme").toMap().value(QStringLiteral("background")).toString(),
                 QStringLiteral("#2e3440"));
    QCOMPARE(m_window->property("color").value<QColor>(), QColor(QStringLiteral("#f6f8fa")));
    clickItem(toggle);
    QTRY_COMPARE(m_window->property("color").value<QColor>(), QColor(QStringLiteral("#090d14")));
    QTRY_COMPARE(terminal->property("colorScheme").toMap().value(QStringLiteral("background")).toString(),
                 QStringLiteral("#070b11"));

    QVERIFY(choose(mode, 3)); // Scheduled auto mode
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer);
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Appearance"))));
    auto* dayStart = namedObject(QStringLiteral("dayStartField"));
    auto* nightStart = namedObject(QStringLiteral("nightStartField"));
    QVERIFY(dayStart && nightStart);
    QTRY_VERIFY(dayStart->property("visible").toBool());
    dayStart->setProperty("text", QStringLiteral("08:30"));
    QVERIFY(QMetaObject::invokeMethod(dayStart, "editingFinished"));
    nightStart->setProperty("text", QStringLiteral("20:45"));
    QVERIFY(QMetaObject::invokeMethod(nightStart, "editingFinished"));
    bitshell::core::AppSettings persisted;
    QCOMPARE(persisted.dayStart(), QStringLiteral("08:30"));
    QCOMPARE(persisted.nightStart(), QStringLiteral("20:45"));
    QTest::qWait(250);
    captureFrame(QStringLiteral("appearance-matrix.png"));
    auto* settingsScroll = namedObject(QStringLiteral("appearanceSettingsScroll"));
    QVERIFY(settingsScroll);
    auto* settingsFlick = settingsScroll->property("contentItem").value<QObject*>();
    QVERIFY(settingsFlick);
    auto* settingsContent = qobject_cast<QQuickItem*>(
        settingsFlick->property("contentItem").value<QObject*>());
    QVERIFY(settingsContent);
    settingsFlick->setProperty(
        "contentY", std::max(0.0, qobject_cast<QQuickItem*>(uiDay)
                                      ->mapToItem(settingsContent, QPointF{}).y() - 100.0));
    QTest::qWait(80);
    clickItem(qobject_cast<QQuickItem*>(uiDay));
    auto* popup = uiDay->property("popup").value<QObject*>();
    QVERIFY(popup);
    QTRY_VERIFY(popup->property("visible").toBool());
    QVERIFY(popup->property("height").toReal() <= 300.0);
    QTest::qWait(150);
    captureFrame(QStringLiteral("appearance-palette-menu.png"));
    QTest::keyClick(m_window, Qt::Key_End);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE(uiDay->property("currentValue").toString(), QStringLiteral("cyberpunk-k"));
    QVERIFY(choose(uiDay, 18));
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QTest::qWait(80);
    clickItem(toggle);
    QTRY_VERIFY(mode->property("currentValue").toString() != QStringLiteral("schedule"));
    const QString manual = mode->property("currentValue").toString();
    QTest::qWait(1100); // Automatic timer must not undo the explicit toggle.
    QCOMPARE(mode->property("currentValue").toString(), manual);
    QVERIFY(choose(mode, 2)); // Follow OS can be re-enabled without losing the matrix.
    QCOMPARE(uiDay->property("currentValue").toString(), QStringLiteral("github-light"));
    QCOMPARE(terminalDay->property("currentValue").toString(), QStringLiteral("nord"));
    QVERIFY(choose(mode, 0));
}

void UiInteractionTest::reordersAndScrollsOverflowingTabs() {
    m_window->setProperty("inspectorMode", 1);
    QTest::qWait(100);
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    auto* tabs = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("openTabsView")));
    QVERIFY(pane != nullptr);
    QVERIFY(tabs != nullptr);

    for (int index = 0; index < 9; ++index) {
        QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    }
    QTRY_COMPARE(pane->property("tabCount").toInt(), 10);
    QVariant firstId, secondId;
    QVERIFY(QMetaObject::invokeMethod(pane, "tabIdAt", Q_RETURN_ARG(QVariant, firstId), Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(pane, "tabIdAt", Q_RETURN_ARG(QVariant, secondId), Q_ARG(QVariant, 1)));
    QTRY_VERIFY(tabs->property("contentWidth").toReal()
                > tabs->property("width").toReal() + 100.0);

    auto* next = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("nextTabsButton")));
    auto* previous = qobject_cast<QQuickItem*>(
        namedObject(QStringLiteral("previousTabsButton")));
    QVERIFY(next != nullptr);
    QVERIFY(previous != nullptr);
    QTRY_VERIFY(next->isVisible());
    QTRY_VERIFY(previous->isVisible());

    tabs->setProperty("contentX", 0.0);
    QTRY_VERIFY(next->property("enabled").toBool());
    clickItem(next);
    QTRY_VERIFY(tabs->property("contentX").toReal() > 40.0);
    QTRY_VERIFY(previous->property("enabled").toBool());
    captureFrame(QStringLiteral("tabs-overflow.png"));

    QVERIFY(QMetaObject::invokeMethod(tabs, "positionViewAtBeginning"));
    QTest::qWait(80);
    auto* first = visualItem(QStringLiteral("openTab:") + firstId.toString());
    auto* second = visualItem(QStringLiteral("openTab:") + secondId.toString());
    QVERIFY(first != nullptr);
    QVERIFY(second != nullptr);
    QCOMPARE(first->property("index").toInt(), 0);
    QCOMPARE(second->property("index").toInt(), 1);

    const QPoint start = itemPoint(first, 0.35);
    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, start, 20);
    QTest::mouseMove(m_window, start + QPoint(34, 1), 80);
    QTRY_COMPARE(pane->property("draggedTabId").toString(), firstId.toString());
    const QPoint destination = itemPoint(second, 0.72);
    QTest::mouseMove(m_window, destination, 120);
    captureFrame(QStringLiteral("tabs-reorder.png"));
    QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, destination, 30);
    QTRY_VERIFY(pane->property("draggedTabId").toString().isEmpty());
    QTRY_COMPARE(first->property("index").toInt(), 1);

    QObject* activeBeforeMove = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(activeBeforeMove != nullptr);
    const int activeIndex = pane->property("currentIndex").toInt();
    QVERIFY(QMetaObject::invokeMethod(pane, "moveTab",
                                      Q_ARG(QVariant, activeIndex),
                                      Q_ARG(QVariant, 0)));
    QTRY_COMPARE(pane->property("currentIndex").toInt(), 0);
    QTRY_COMPARE(pane->property("activeTerminal").value<QObject*>(),
                 activeBeforeMove);

    QVariant activeId;
    QVERIFY(QMetaObject::invokeMethod(pane, "tabIdAt", Q_RETURN_ARG(QVariant, activeId),
                                      Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(tabs, "positionViewAtBeginning"));
    QTest::qWait(80);
    auto* activeTab = visualItem(QStringLiteral("openTab:") + activeId.toString());
    QVERIFY(activeTab);
    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier, itemPoint(activeTab, 0.5), 20);
    auto* contextMenu = namedObject(QStringLiteral("openTabContextMenu"));
    QTRY_VERIFY(contextMenu && contextMenu->property("visible").toBool());
    captureFrame(QStringLiteral("tab-context-menu.png"));

    auto* categoryEntry = visualItem(QStringLiteral("appSubmenuItem:Category"));
    QVERIFY(categoryEntry && categoryEntry->isVisible());
    const QColor untintedTab = activeTab->property("color").value<QColor>();
    QTest::mouseMove(m_window, itemPoint(categoryEntry), 30);
    auto* categoryMenu = namedObject(QStringLiteral("tabCategoryMenu"));
    QTRY_VERIFY(categoryMenu && categoryMenu->property("visible").toBool());
    auto* redCategory = categoryMenu->findChild<QQuickItem*>(QStringLiteral("tabCategoryRed"));
    QVERIFY(redCategory && redCategory->isVisible());
    QTest::qWait(120);
    captureFrame(QStringLiteral("tab-category-submenu.png"));
    clickItem(redCategory);
    QTRY_VERIFY(!categoryMenu->property("visible").toBool());
    QTRY_VERIFY(!contextMenu->property("visible").toBool());

    QVariant tabInfo;
    QVERIFY(QMetaObject::invokeMethod(pane, "tabInfoAt", Q_RETURN_ARG(QVariant, tabInfo),
                                      Q_ARG(QVariant, 0)));
    QCOMPARE(tabInfo.toMap().value(QStringLiteral("tabColor")).toString(), QStringLiteral("red"));
    QTRY_VERIFY(activeTab->property("color").value<QColor>() != untintedTab);
    auto* categoryStripe = visualItem(QStringLiteral("tabCategoryStripe:") + activeId.toString());
    QVERIFY(categoryStripe);
    QTRY_COMPARE(categoryStripe->width(), 4.0);
    captureFrame(QStringLiteral("tab-category-red-tint.png"));

    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier, itemPoint(activeTab, 0.5), 20);
    QTRY_VERIFY(contextMenu->property("visible").toBool());
    clickItem(contextMenu->findChild<QQuickItem*>(QStringLiteral("pinTabMenuItem")));
    QVERIFY(QMetaObject::invokeMethod(pane, "tabInfoAt", Q_RETURN_ARG(QVariant, tabInfo),
                                      Q_ARG(QVariant, 0)));
    QVERIFY(tabInfo.toMap().value(QStringLiteral("pinned")).toBool());
    QVERIFY(QMetaObject::invokeMethod(pane, "renameTab", Q_ARG(QVariant, 0),
                                      Q_ARG(QVariant, QStringLiteral("PROD work"))));
    QVERIFY(QMetaObject::invokeMethod(pane, "tabInfoAt", Q_RETURN_ARG(QVariant, tabInfo),
                                      Q_ARG(QVariant, 0)));
    QCOMPARE(tabInfo.toMap().value(QStringLiteral("label")).toString(), QStringLiteral("PROD work"));
    QCOMPARE(tabInfo.toMap().value(QStringLiteral("tabColor")).toString(), QStringLiteral("red"));
    QVERIFY(QMetaObject::invokeMethod(pane, "duplicateTab", Q_ARG(QVariant, 0)));
    QTRY_COMPARE(pane->property("tabCount").toInt(), 11);
    QVERIFY(QMetaObject::invokeMethod(pane, "closeMatching", Q_ARG(QVariant, 0),
                                      Q_ARG(QVariant, QStringLiteral("others"))));
    QTRY_COMPARE(pane->property("tabCount").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(pane, "tabInfoAt", Q_RETURN_ARG(QVariant, tabInfo),
                                      Q_ARG(QVariant, 0)));
    QVERIFY(tabInfo.toMap().value(QStringLiteral("pinned")).toBool());

    while (pane->property("tabCount").toInt() > 1) {
        const int last = pane->property("tabCount").toInt() - 1;
        QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, last)));
    }
    QTRY_COMPARE(pane->property("tabCount").toInt(), 1);
    m_window->setProperty("inspectorMode", 0);
}

void UiInteractionTest::yamlImportExportAndLiveReloadControls() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>(QStringLiteral("BitShell"), QStringLiteral("AppSettings"));
    auto* sessions = m_engine->singletonInstance<bitshell::core::SessionStore*>(QStringLiteral("BitShell"), QStringLiteral("SessionStore"));
    auto* macros = m_engine->singletonInstance<bitshell::core::MacroStore*>(QStringLiteral("BitShell"), QStringLiteral("MacroStore"));
    QVERIFY(settings && sessions && macros);
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Data & About"))));
    auto* scroll = namedObject(QStringLiteral("dataSettingsScroll"));
    QVERIFY(scroll);
    auto* content = scroll->property("contentItem").value<QObject*>();
    QVERIFY(content); content->setProperty("contentY", 0);
    QTest::qWait(220);
    auto* actions = namedObject(QStringLiteral("settingsYamlActions"));
    QVERIFY(actions);
    const QString exportPath = QDir(m_configRoot).filePath(QStringLiteral("ui-settings.yaml"));
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(actions, "exportTo", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, QUrl::fromLocalFile(exportPath).toString())));
    QVERIFY2(result.toBool(), qPrintable(actions->property("status").toString()));
    QVERIFY(QFileInfo::exists(exportPath));
    const qreal originalSize = settings->terminalFontSize();
    settings->setTerminalFontSize(originalSize + 2);
    actions->setProperty("pendingSource", QUrl::fromLocalFile(exportPath).toString());
    auto* confirmation = namedObject(QStringLiteral("settingsYamlActionsConfirmImport"));
    QVERIFY(confirmation);
    QVERIFY(QMetaObject::invokeMethod(confirmation, "open"));
    QTRY_VERIFY(confirmation->property("visible").toBool());
    QTest::qWait(90);
    captureFrame(QStringLiteral("yaml-import-confirmation.png"));
    QVERIFY(QMetaObject::invokeMethod(confirmation, "accept"));
    QTRY_COMPARE(settings->terminalFontSize(), originalSize);
    QTest::qWait(100);
    captureFrame(QStringLiteral("yaml-settings.png"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QTRY_VERIFY(!confirmation->property("visible").toBool());
    QTest::qWait(100);
    captureFrame(QStringLiteral("yaml-settings-closed.png"));

    // The catalog menu exposes the same tested controls, without a sprawling menu.
    clickItem(visualItem(QStringLiteral("catalogMenuButton")));
    captureFrame(QStringLiteral("yaml-menu-clicked.png"));
    auto* menu = namedObject(QStringLiteral("catalogContextMenu"));
    QTRY_VERIFY(menu && menu->property("visible").toBool());
    QTest::qWait(110);
    auto* yamlItem = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("sessionYamlMenuItem")));
    QVERIFY(yamlItem); clickItem(yamlItem);
    auto* catalogDialog = namedObject(QStringLiteral("sessionsYamlDialog"));
    QTRY_VERIFY(catalogDialog && catalogDialog->property("visible").toBool());
    auto* catalogActions = namedObject(QStringLiteral("catalogYamlActions"));
    const QString sessionFile = QDir(m_configRoot).filePath(QStringLiteral("ui-catalog.yaml"));
    QVERIFY(QMetaObject::invokeMethod(catalogActions, "exportTo", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, sessionFile)));
    QVERIFY(result.toBool());
    const int count = sessions->count();
    QVERIFY(QMetaObject::invokeMethod(catalogActions, "importFrom", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, sessionFile)));
    QVERIFY(result.toBool()); QCOMPARE(sessions->count(), count);
    QTest::qWait(120);
    captureFrame(QStringLiteral("yaml-catalog.png"));
    QVERIFY(QMetaObject::invokeMethod(catalogDialog, "close"));
    QTest::qWait(150);

    auto* manager = namedObject(QStringLiteral("macroManager"));
    QVERIFY(QMetaObject::invokeMethod(manager, "openForFolder", Q_ARG(QVariant, QString{})));
    const QString groupId = manager->property("groupId").toString();
    QVERIFY(!groupId.isEmpty());
    namedObject(QStringLiteral("macroGroupName"))->setProperty("text", QStringLiteral("YAML tools"));
    QVERIFY(QMetaObject::invokeMethod(manager, "save", Q_RETURN_ARG(QVariant, result)));
    QVERIFY(result.toBool());
    auto* macroActions = namedObject(QStringLiteral("macrosYamlActions"));
    QVERIFY(macroActions);
    macroActions->setProperty("exportGroupId", groupId);
    const QString macroFile = QDir(m_configRoot).filePath(QStringLiteral("ui-group.yaml"));
    QVERIFY(QMetaObject::invokeMethod(macroActions, "exportTo", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, macroFile)));
    QVERIFY(result.toBool());
    QFile exported(macroFile); QVERIFY(exported.open(QIODevice::ReadOnly));
    auto bytes = exported.readAll(); exported.close(); bytes.replace("YAML tools", "Imported tools");
    QVERIFY(exported.open(QIODevice::WriteOnly)); QCOMPARE(exported.write(bytes), bytes.size()); exported.close();
    const auto previousGroups = macros->groups().size();
    QVERIFY(QMetaObject::invokeMethod(macroActions, "importFrom", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, macroFile)));
    QVERIFY(result.toBool()); QCOMPARE(macros->groups().size(), previousGroups + 1);
    QCOMPARE(macros->assignedGroup({}), groupId);
    QTest::qWait(140);
    captureFrame(QStringLiteral("yaml-macros.png"));
    QVERIFY(QMetaObject::invokeMethod(manager, "close"));
    QTest::qWait(150);
    auto* strip = namedObject(QStringLiteral("macroBar"));
    const QString importedId = macros->groups().last().toMap().value(QStringLiteral("id")).toString();
    strip->setProperty("overrideGroup", importedId);
    QTRY_COMPARE(strip->property("resolved").toMap().value(QStringLiteral("id")).toString(), importedId);
    QVERIFY(macros->assignFolder({}, importedId));
    strip->setProperty("overrideGroup", QString{});
    QTRY_COMPARE(strip->property("resolved").toMap().value(QStringLiteral("id")).toString(), importedId);
    QVERIFY(macros->assignFolder({}, groupId));
    QVERIFY(macros->removeGroup(importedId));
}

void UiInteractionTest::macroButtonsMonitoringAndPanelVisibility() {
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>(QStringLiteral("BitShell"), QStringLiteral("AppSettings"));
    auto* macros = m_engine->singletonInstance<bitshell::core::MacroStore*>(QStringLiteral("BitShell"), QStringLiteral("MacroStore"));
    QVERIFY(settings && macros);
    settings->setAppearanceMode(QStringLiteral("dark"));
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(pane);
    auto* previousTerminal = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    QTRY_VERIFY(pane->property("activeTerminal").value<QObject*>() != nullptr
                && pane->property("activeTerminal").value<QObject*>() != previousTerminal);
    auto* terminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
    QVERIFY(terminal);
    QTRY_VERIFY(terminal->isRunning());
#ifdef Q_OS_WIN
    // User PowerShell modules may display publisher prompts even with -NoProfile.
    // Use the native console fixture so this interaction test is independent of
    // the user's shell and profile while still exercising the Windows ConPTY path.
    const QString testShell = QCoreApplication::applicationDirPath()
                              + QStringLiteral("/windows_terminal_fixture.exe");
    QVERIFY(QFile::exists(testShell));
    QVERIFY(terminal->startLocalCommand({testShell, QStringLiteral("broadcast")}));
    QTRY_VERIFY(terminal->isRunning());
#endif
    QVERIFY(!pane->property("monitoringActive").toBool());
    QVERIFY(!namedObject(QStringLiteral("hostMonitor"))->property("enabled").toBool());

    auto* manager = namedObject(QStringLiteral("macroManager"));
    QVERIFY(QMetaObject::invokeMethod(manager, "openForFolder", Q_ARG(QVariant, QString{})));
    QTRY_VERIFY(manager->property("visible").toBool());
    namedObject(QStringLiteral("macroGroupName"))->setProperty("text", QStringLiteral("Daily tools"));
    namedObject(QStringLiteral("macroButtonLabel"))->setProperty("text", QStringLiteral("Hello"));
#ifdef Q_OS_WIN
    const QString macroText = QStringLiteral("UI_MACRO_OK");
    const QByteArray macroResult("ACK:UI_MACRO_OK");
#else
    const QString macroText = QStringLiteral("printf 'UI_MACRO_%s\\n' OK");
    const QByteArray macroResult("UI_MACRO_OK");
#endif
    namedObject(QStringLiteral("macroButtonText"))->setProperty("text", macroText);
    clickItem(visualItem(QStringLiteral("saveAssignMacroGroup")));
    QVERIFY2(!macros->assignedGroup({}).isEmpty(), qPrintable(macros->lastError()));
    captureFrame(QStringLiteral("macro-manager.png"));
    QVERIFY(QMetaObject::invokeMethod(manager, "close"));
    QTest::qWait(150);
    QQuickItem* macroButton = nullptr;
    QTRY_VERIFY((macroButton = visualItem(QStringLiteral("macroAction:Hello"))) != nullptr);
    auto* strip = namedObject(QStringLiteral("macroBar"));
    QVERIFY(strip);
    QTRY_COMPARE(strip->property("terminal").value<QObject*>(),
                 pane->property("activeTerminal").value<QObject*>());
    QTRY_COMPARE(pane->property("activeTerminal").value<QObject*>(), terminal);
    QTRY_VERIFY(macroButton->isVisible());
    QTRY_VERIFY(macroButton->isEnabled());
    const auto buttons = strip->property("resolved").toMap().value(QStringLiteral("buttons")).toList();
    QCOMPARE(buttons.size(), 1);
    QCOMPARE(buttons.constFirst().toMap().value(QStringLiteral("text")).toString(),
             macroText);
    QSignalSpy output(terminal, &bitshell::terminal::TerminalView::rawOutputReceived);
    clickItem(macroButton);
    const auto received = [&output] {
        QByteArray bytes;
        for (const auto& sample : output) bytes += sample.at(0).toByteArray();
        return bytes;
    };
#ifndef Q_OS_WIN
    QTRY_VERIFY(received().contains("UI_MACRO_"));
#endif
    // Paste-only buttons leave the command for inspection. Submit through the terminal's
    // input channel so a closing modal cannot steal the synthetic window key event. The
    // Windows fixture deliberately disables terminal echo; its ACK below proves both writes.
    terminal->sendText(QStringLiteral("\r"));
    QByteArray macroOutput;
    QTRY_VERIFY2_WITH_TIMEOUT(
        ([&] { macroOutput = received(); return macroOutput.contains(macroResult); }()),
        macroOutput.toHex(' ').constData(), 15000);
    auto* sessions = m_engine->singletonInstance<bitshell::core::SessionStore*>(QStringLiteral("BitShell"), QStringLiteral("SessionStore"));
    QVERIFY(sessions->addFolder({}, QStringLiteral("Macro inherit")));
    QVERIFY(sessions->addFolder(QStringLiteral("Macro inherit"), QStringLiteral("Child")));
    const int inheritedSession = sessions->addSession({
        {QStringLiteral("name"), QStringLiteral("inherit-test")},
        {QStringLiteral("host"), QStringLiteral("example.invalid")},
        {QStringLiteral("group"), QStringLiteral("Macro inherit/Child")}});
    QVERIFY(inheritedSession >= 0);
    const auto globalGroup = macros->effectiveGroup({});
    const QString oracleGroup = macros->saveGroup({}, QStringLiteral("Oracle"), globalGroup.value(QStringLiteral("buttons")).toList());
    pane->setProperty("activeCatalogId", sessions->get(inheritedSession).value(QStringLiteral("id")));
    QVERIFY(macros->assignFolder(QStringLiteral("Macro inherit"), oracleGroup));
    QTRY_COMPARE(strip->property("resolved").toMap().value(QStringLiteral("id")).toString(), oracleGroup);
    QVERIFY(sessions->renameFolder(QStringLiteral("Macro inherit"), QStringLiteral("Renamed macros")));
    QTRY_COMPARE(macros->assignedGroup(QStringLiteral("Renamed macros")), oracleGroup);
    QTRY_COMPARE(pane->property("activeFolder").toString(), QStringLiteral("Renamed macros/Child"));
    QVERIFY(sessions->moveSession(inheritedSession, {}));
    QTRY_COMPARE(strip->property("resolved").toMap().value(QStringLiteral("id")).toString(), globalGroup.value(QStringLiteral("id")).toString());
    pane->setProperty("activeCatalogId", QString{});
    QVERIFY(sessions->removeSession(inheritedSession));
    QVERIFY(sessions->removeFolder(QStringLiteral("Renamed macros/Child")));
    QVERIFY(sessions->removeFolder(QStringLiteral("Renamed macros")));
    QVERIFY(macros->removeGroup(oracleGroup));
    QVERIFY(QMetaObject::invokeMethod(pane, "toggleMonitoring"));
    auto* monitor = namedObject(QStringLiteral("hostMonitor"));
#ifdef Q_OS_WIN
    QTRY_VERIFY_WITH_TIMEOUT(!monitor->property("metrics").toMap().isEmpty(), 5000);
    QVERIFY(monitor->property("status").toString().contains(QStringLiteral("local Windows")));
#else
    QTRY_VERIFY(!monitor->property("metrics").toMap().isEmpty());
#endif
    m_window->setProperty("inspectorMode", 1);
    QTRY_COMPARE(namedObject(QStringLiteral("filesPanel"))->property("reveal").toReal(), 1.0);
    auto* localModel = namedObject(QStringLiteral("localFileModel"));
    QVERIFY(localModel && localModel->property("count").toInt() > 0);
    captureFrame(QStringLiteral("blueberi-workspace.png"));
    clickItem(visualItem(QStringLiteral("filesToggle")));
    QTRY_COMPARE(m_window->property("inspectorMode").toInt(), 0);
    QVERIFY(namedObject(QStringLiteral("localFileModel")) == localModel);
    clickItem(visualItem(QStringLiteral("connectionsToggle")));
    QTRY_VERIFY(!namedObject(QStringLiteral("catalogSidebar"))->property("visible").toBool());
    clickItem(visualItem(QStringLiteral("connectionsToggle")));
    QTRY_VERIFY(settings->sidebarVisible());
    QVERIFY(QMetaObject::invokeMethod(pane, "toggleMonitoring"));
    QTRY_VERIFY(!monitor->property("enabled").toBool());
    settings->setAppearanceMode(QStringLiteral("light"));
    settings->setUiLightThemeId(QStringLiteral("blueberry-light"));
    settings->setTerminalLightThemeId(QStringLiteral("blueberry-light"));
    m_window->setProperty("inspectorMode", 1);
    QTRY_COMPARE(namedObject(QStringLiteral("filesPanel"))->property("reveal").toReal(), 1.0);
    captureFrame(QStringLiteral("raspberi-workspace.png"));
    settings->setAppearanceMode(QStringLiteral("dark"));
    m_window->setProperty("inspectorMode", 0);
}

void UiInteractionTest::terminalMenuWheelAndShortcutEditor() {
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    auto* terminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
    QVERIFY(terminal);
    terminal->forceActiveFocus();
    const auto before = terminal->fontPixelSize();
    const QPoint point = itemPoint(terminal);
    QWheelEvent wheel(point, m_window->mapToGlobal(point), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
    QGuiApplication::sendEvent(m_window, &wheel);
    QTRY_VERIFY(terminal->fontPixelSize() > before);
    QTest::mouseClick(m_window, Qt::RightButton, Qt::NoModifier, point);
    QObject* menu = terminal->parentItem()->findChild<QObject*>(QStringLiteral("terminalContextMenu"));
    QVERIFY(menu);
    QTRY_VERIFY(menu->property("visible").toBool());
    captureFrame(QStringLiteral("terminal-context-menu.png"));
    QVERIFY(QMetaObject::invokeMethod(menu, "close"));
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Shortcuts"))));
    QTest::qWait(180);
    auto* capture = visualItem(QStringLiteral("shortcut:copy"));
    QVERIFY(capture);
    capture->forceActiveFocus();
    QTest::keyClick(m_window, Qt::Key_C, Qt::AltModifier);
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>(QStringLiteral("BitShell"), QStringLiteral("AppSettings"));
    QTRY_COMPARE(settings->keyBindings().value(QStringLiteral("copy")).toString(), QStringLiteral("Alt+C"));
    const int tabCountBeforeCapture = pane->property("tabCount").toInt();
    QTest::keyClick(m_window, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(pane->property("tabCount").toInt(), tabCountBeforeCapture);
    QVERIFY(!settings->shortcutError().isEmpty());
    settings->applyKeyPreset(QStringLiteral("linux"));
    QVERIFY(!settings->copyOnSelect());
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
}

void UiInteractionTest::fileBrowserFoldersNavigateWithMouseAndKeyboard() {
    QTemporaryDir filesRoot;
    QVERIFY(filesRoot.isValid());
    const auto write = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    const QString localPath = filesRoot.filePath(QStringLiteral("local"));
    const QString remotePath = filesRoot.filePath(QStringLiteral("remote"));
    for (const auto& path : {localPath, remotePath}) {
        QVERIFY(QDir().mkpath(path + QStringLiteral("/.hidden folder/child")));
        QVERIFY(QDir().mkpath(path + QStringLiteral("/Projects/child")));
        QVERIFY(QDir().mkpath(path + QStringLiteral("/z-empty")));
        QVERIFY(write(path + QStringLiteral("/file.txt"), "navigation fixture"));
    }
    auto* local = qobject_cast<bitshell::core::LocalFileModel*>(namedObject(QStringLiteral("localFileModel")));
    auto* sftp = qobject_cast<bitshell::core::SftpController*>(namedObject(QStringLiteral("sftpController")));
    QVERIFY(local && sftp);
    const auto originalPath = local->currentPath();
    local->changeDirectory(localPath);
    m_window->setProperty("inspectorMode", 1);
    QTRY_COMPARE(namedObject(QStringLiteral("filesPanel"))->property("reveal").toReal(), 1.0);
    QTRY_VERIFY(visualItem(QStringLiteral("localFileList"))->isVisible());
    const auto exercise = [this](const QString& prefix, QObject* controller, const QString& path) {
        auto* pane = namedObject(prefix + QStringLiteral("FilePane"));
        auto* list = visualItem(prefix + QStringLiteral("FileList"));
        QVERIFY(pane && list);
        QQuickItem* folder = nullptr;
        QTRY_VERIFY((folder = visualItem(prefix + QStringLiteral("FileRow:.hidden folder"))) != nullptr);
        clickItem(folder);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/.hidden folder"));
        QTRY_COMPARE(pane->property("selectedRow").toInt(), -1);
        QTRY_COMPARE(namedObject(prefix + QStringLiteral("Path"))->property("text").toString(),
                     path + QStringLiteral("/.hidden folder"));
        captureFrame(prefix + QStringLiteral("-folder-open.png"));
        clickItem(visualItem(prefix + QStringLiteral("Back")));
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 20);
        QTRY_VERIFY((folder = visualItem(prefix + QStringLiteral("FileRow:Projects"))) != nullptr);
        const auto point = itemPoint(folder);
        QTest::mouseDClick(folder->window(), Qt::LeftButton, Qt::NoModifier, point, 20);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/Projects"));
        // Double-click must not also enter the child which replaced this row.
        QTest::qWait(80);
        QCOMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/Projects"));
        QTRY_VERIFY((folder = visualItem(prefix + QStringLiteral("FileRow:child"))) != nullptr);
        QTest::mouseClick(folder->window(), Qt::LeftButton, Qt::ControlModifier, itemPoint(folder));
        QVERIFY(pane->property("selectedDirectory").toBool());
        QCOMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/Projects"));
        QTest::keyClick(list->window(), Qt::Key_Return);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/Projects/child"));
        QTest::keyClick(list->window(), Qt::Key_Left, Qt::AltModifier);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/Projects"));
        QTest::keyClick(list->window(), Qt::Key_Backspace);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path);
        QTRY_VERIFY((folder = visualItem(prefix + QStringLiteral("FileRow:.hidden folder"))) != nullptr);
        QTest::mouseClick(folder->window(), Qt::LeftButton, Qt::ControlModifier, itemPoint(folder));
        clickItem(visualItem(prefix + QStringLiteral("OpenFolder")));
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/.hidden folder"));
        clickItem(visualItem(prefix + QStringLiteral("Back")));
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path);
        clickItem(visualItem(prefix + QStringLiteral("Forward")));
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/.hidden folder"));
        clickItem(visualItem(prefix + QStringLiteral("Up")));
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path);
        QTRY_VERIFY(controller->property("statusText").toString() != QStringLiteral("Loading…"));
        QTRY_COMPARE(controller->property("count").toInt(), 4);
        QTRY_VERIFY((folder = visualItem(prefix + QStringLiteral("FileRow:z-empty"))) != nullptr);
        QTest::mouseClick(folder->window(), Qt::RightButton, Qt::NoModifier, itemPoint(folder));
        auto* menu = namedObject(prefix + QStringLiteral("FileMenu"));
        QTRY_VERIFY(menu && menu->property("visible").toBool());
        QCOMPARE(controller->property("currentPath").toString(), path);
        QVERIFY(QMetaObject::invokeMethod(menu, "close"));
        QTRY_VERIFY(!menu->property("visible").toBool());
        list->forceActiveFocus();
        QTRY_VERIFY(list->hasActiveFocus());
        QTRY_VERIFY(pane->property("selectedDirectory").toBool());
        QTest::keyClick(list->window(), Qt::Key_Right);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path + QStringLiteral("/z-empty"));
        QTRY_COMPARE(controller->property("count").toInt(), 0);
        QTest::keyClick(list->window(), Qt::Key_Up, Qt::AltModifier);
        QTRY_VERIFY(!controller->property("busy").toBool());
        QTRY_COMPARE(controller->property("currentPath").toString(), path);
        QQuickItem* file = nullptr;
        QTRY_VERIFY((file = visualItem(prefix + QStringLiteral("FileRow:file.txt"))) != nullptr);
        clickItem(file);
        QVERIFY(!pane->property("selectedDirectory").toBool());
        QCOMPARE(controller->property("currentPath").toString(), path);
        QTest::keyClick(list->window(), Qt::Key_Return);
        QCOMPARE(controller->property("currentPath").toString(), path);
        // Up/down selection is separate from opening or copying anything.
        QTest::keyClick(list->window(), Qt::Key_Up);
        QVERIFY(pane->property("selectedDirectory").toBool());
        QCOMPARE(controller->property("currentPath").toString(), path);
    };
    exercise(QStringLiteral("local"), local, localPath);
    if (QTest::currentTestFailed()) return;

    // A folder drag is not a click: cancel the native drag in this isolated fixture.
    auto* folder = visualItem(QStringLiteral("localFileRow:z-empty"));
    QVERIFY(folder);
    QPointer<QQuickItem> pointer = folder->findChild<QQuickItem*>(QStringLiteral("fileRowPointer"));
    QVERIFY(pointer);
    const QPoint start = itemPoint(folder);
    const QPoint end = start + QPoint(QGuiApplication::styleHints()->startDragDistance() + 16, 0);
    bool dragStarted = false;
#ifdef Q_OS_WIN
    std::jthread nativeDragWakeup;
    if (QGuiApplication::platformName() == QStringLiteral("windows")) {
        // Qt 6.11's Windows drag startup waits for a native WM_MOUSEMOVE on
        // this window. QTest mouse events and QTimer cancellation alone cannot
        // wake that GetMessage loop. A no-buttons move cancels this synthetic
        // gesture without touching the real desktop cursor or other processes.
        const HWND handle = reinterpret_cast<HWND>(m_window->winId());
        const DWORD processId = GetCurrentProcessId();
        nativeDragWakeup = std::jthread([handle, processId] {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            DWORD owner = 0;
            if (GetWindowThreadProcessId(handle, &owner) && owner == processId)
                PostMessageW(handle, WM_MOUSEMOVE, 0, 0);
        });
    }
#endif
    QTimer::singleShot(150, this, [&] {
        dragStarted = pointer && pointer->property("startedDrag").toBool();
        QTest::mouseRelease(m_window, Qt::LeftButton, Qt::NoModifier, end);
        if (QGuiApplication::platformName() != QStringLiteral("offscreen")) QDrag::cancel();
    });
    QTest::mousePress(m_window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(m_window, end, 30);
    QTest::qWait(350);
    QVERIFY(dragStarted);
    QCOMPARE(local->currentPath(), localPath);

    if (QFileInfo(QStringLiteral("/usr/libexec/openssh/sftp-server")).isExecutable()) {
        const QString wrapper = filesRoot.filePath(QStringLiteral("direct-sftp"));
        QVERIFY(write(wrapper, "#!/bin/sh\ncd '" + remotePath.toUtf8()
                             + "' || exit 1\nexec /usr/bin/sftp -D /usr/libexec/openssh/sftp-server -b -\n"));
        QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        sftp->setTransportExecutableForTesting(wrapper);
        sftp->connectToProfile({{QStringLiteral("host"), QStringLiteral("local-ui-test.invalid")}});
        QTRY_VERIFY(sftp->connected() && !sftp->busy());
        QTRY_VERIFY(visualItem(QStringLiteral("remoteFileList"))->isVisible());
        QTest::qWait(80); // SplitView lays out the newly visible remote pane on polish.
        exercise(QStringLiteral("remote"), sftp, remotePath);
        sftp->disconnectFromHost();
        sftp->setTransportExecutableForTesting({});
    } else {
        qWarning("Local navigation tested; remote fixture unavailable without OpenSSH sftp-server");
    }
    clickItem(visualItem(QStringLiteral("filesToggle")));
    QTRY_VERIFY(!local->active());
    local->changeDirectory(originalPath);
}

void UiInteractionTest::fileBrowserReceivesDesktopDropsAndExportsRemoteFiles() {
    if (!QFileInfo(QStringLiteral("/usr/libexec/openssh/sftp-server")).isExecutable())
        QSKIP("OpenSSH sftp-server is not installed");
    QTemporaryDir filesRoot;
    const auto write = [](const QString& path, const QByteArray& data) {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    };
    const QString remotePath = filesRoot.filePath(QStringLiteral("remote"));
    const QString localPath = filesRoot.filePath(QStringLiteral("local"));
    QVERIFY(QDir().mkdir(remotePath)); QVERIFY(QDir().mkdir(localPath));
    QVERIFY(write(QDir(remotePath).filePath(QStringLiteral("remote.txt")), "from remote"));
    const QString source = filesRoot.filePath(QStringLiteral("desktop.txt"));
    QVERIFY(write(source, "from desktop"));
    const QString wrapper = filesRoot.filePath(QStringLiteral("direct-sftp"));
    QVERIFY(write(wrapper, "#!/bin/sh\ncd '" + remotePath.toUtf8() + "' || exit 1\nexec /usr/bin/sftp -D /usr/libexec/openssh/sftp-server -b -\n"));
    QVERIFY(QFile::setPermissions(wrapper, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    auto* sftp = qobject_cast<bitshell::core::SftpController*>(namedObject(QStringLiteral("sftpController")));
    auto* local = qobject_cast<bitshell::core::LocalFileModel*>(namedObject(QStringLiteral("localFileModel")));
    QVERIFY(sftp && local);
    sftp->setTransportExecutableForTesting(wrapper);
    local->changeDirectory(localPath);
    m_window->setProperty("inspectorMode", 1);
    sftp->connectToProfile({{QStringLiteral("host"), QStringLiteral("local-ui-test.invalid")}});
    QTRY_VERIFY(sftp->connected() && !sftp->busy());
    QTRY_VERIFY(visualItem(QStringLiteral("remoteFileDropArea"))
                && visualItem(QStringLiteral("remoteFileDropArea"))->isVisible()
                && visualItem(QStringLiteral("remoteFileDropArea"))->width() > 0);
    QTest::qWait(150);
    const auto dropUrls = [this](QQuickItem* target, const QList<QUrl>& urls) {
        QMimeData mime; mime.setUrls(urls);
        const QPoint position = itemPoint(target);
        QDragEnterEvent enter(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QGuiApplication::sendEvent(m_window, &enter);
        QDropEvent drop(position, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QGuiApplication::sendEvent(m_window, &drop);
        return drop.isAccepted();
    };
    QVERIFY(dropUrls(visualItem(QStringLiteral("remoteFileDropArea")), {QUrl::fromLocalFile(source)}));
    QTRY_VERIFY(!sftp->busy());
    QVERIFY2(QFileInfo::exists(QDir(remotePath).filePath(QStringLiteral("desktop.txt"))), qPrintable(sftp->statusText()));
    int remoteRow = -1;
    for (int i = 0; i < sftp->count(); ++i)
        if (sftp->data(sftp->index(i), bitshell::core::SftpController::NameRole).toString() == QStringLiteral("remote.txt")) remoteRow = i;
    QVERIFY(remoteRow >= 0);
    sftp->startDrag(remoteRow);
    QTRY_VERIFY(!sftp->busy());
    QVERIFY2(QFileInfo::exists(sftp->preparedDragUrl().toLocalFile()), qPrintable(sftp->statusText() + sftp->lastTransferText()));
    QVERIFY(dropUrls(visualItem(QStringLiteral("localFileDropArea")), {sftp->preparedDragUrl()}));
    QTRY_VERIFY(!local->busy());
    QFile copied(QDir(localPath).filePath(QStringLiteral("remote.txt")));
    QVERIFY(copied.open(QIODevice::ReadOnly));
    QCOMPARE(copied.readAll(), QByteArray("from remote"));
    captureFrame(QStringLiteral("sftp-local-remote.png"));
    clickItem(visualItem(QStringLiteral("filesToggle")));
    QVERIFY(sftp->connected());
    sftp->disconnectFromHost();
    sftp->setTransportExecutableForTesting({});
}

void UiInteractionTest::securityPromptsAreVisibleAndCancelByDefault() {
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(pane);
    auto* previousTerminal = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    QTRY_VERIFY(pane->property("activeTerminal").value<QObject*>() != previousTerminal);
    auto* terminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
    QVERIFY(terminal); QTRY_VERIFY(terminal->isRunning());
    auto* dialog = terminal->parentItem()->findChild<QObject*>(QStringLiteral("pasteConfirmationDialog"));
    QVERIFY(dialog);
    QSignalSpy output(terminal, &bitshell::terminal::TerminalView::rawOutputReceived);
    terminal->sendMacro(QStringLiteral("printf SECURITY_TEST_WOULD_RUN"), true);
    QTRY_VERIFY(dialog->property("visible").toBool());
    auto* preview = dialog->findChild<QObject*>(QStringLiteral("pastePreview"));
    QVERIFY(preview);
    QCOMPARE(preview->property("textFormat").toInt(), 0); // QTextEdit::PlainText
    QTRY_VERIFY(m_window->activeFocusItem() && m_window->activeFocusItem()->property("text").toString() == QStringLiteral("Cancel"));
    QTest::qWait(120);
    captureFrame(QStringLiteral("security-paste-review.png"));
    QVERIFY(m_window->activeFocusItem() && m_window->activeFocusItem()->property("text").toString() == QStringLiteral("Cancel"));
    QVERIFY(pane->property("securityPromptActive").toBool());
    const int countBefore = pane->property("tabCount").toInt();
    QTest::keyClick(m_window, Qt::Key_T, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(pane->property("tabCount").toInt(), countBefore);
    QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    QCOMPARE(pane->property("tabCount").toInt(), countBefore);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QByteArray received;
    for (const auto& sample : output) received += sample.at(0).toByteArray();
    QVERIFY(!received.contains("SECURITY_TEST_WOULD_RUN"));
    terminal->pasteText(QStringLiteral("<img src='https://invalid.example/image'>\n"));
    QTRY_VERIFY(dialog->property("visible").toBool());
    QCOMPARE(preview->property("textFormat").toInt(), 0);
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QTRY_COMPARE(preview->property("text").toString(), QString{});
    auto* x11 = terminal->parentItem()->findChild<QObject*>(QStringLiteral("x11ConfirmationDialog"));
    QVERIFY(x11);
    terminal->startSshProfile({{QStringLiteral("host"), QStringLiteral("example.invalid")}, {QStringLiteral("forwardX11"), true}});
    QTRY_VERIFY(x11->property("visible").toBool());
    QTest::qWait(120); captureFrame(QStringLiteral("security-x11-review.png"));
    QTest::keyClick(m_window, Qt::Key_Escape);
    QTRY_VERIFY(!x11->property("visible").toBool());
    auto* localProfiles = m_engine->singletonInstance<bitshell::core::LocalProfileStore*>("BitShell", "LocalProfileStore");
    QCOMPARE(terminal->activeTarget(), localProfiles->defaultProfile().value(QStringLiteral("name")).toString());
}

void UiInteractionTest::cyberArkAndPersonalProfileControls() {
    auto* personal = m_engine->singletonInstance<bitshell::core::PersonalProfile*>("BitShell", "PersonalProfile");
    auto* sessions = m_engine->singletonInstance<bitshell::core::SessionStore*>("BitShell", "SessionStore");
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(personal && sessions && settings);
    settings->setSidebarVisible(true);
    QTRY_COMPARE(namedObject(QStringLiteral("connectionsPanel"))->property("reveal").toReal(), 1.0);
    clickItem(visualItem(QStringLiteral("profileSettingsButton")));
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QTRY_VERIFY(drawer && drawer->property("opened").toBool());
    QTRY_COMPARE(drawer->property("currentSection").toInt(), 0);
    auto* displayName = drawer->findChild<QQuickItem*>(QStringLiteral("profileDisplayNameField"));
    QVERIFY(displayName);
    displayName->setProperty("text", QStringLiteral("Berry"));
    clickItem(drawer->findChild<QQuickItem*>(QStringLiteral("saveDisplayNameButton")));
    QTRY_COMPARE(personal->displayName(), QStringLiteral("Berry"));

    QImage fixture(60, 60, QImage::Format_ARGB32); fixture.fill(QColor(QStringLiteral("#4db6ac")));
    const auto avatarPath = QDir(m_configRoot).filePath(QStringLiteral("avatar-fixture.png"));
    QVERIFY(fixture.save(avatarPath));
    QVERIFY(personal->chooseAvatar(QUrl::fromLocalFile(avatarPath)));
    QVERIFY(!personal->avatarEnabled());
    auto* check = drawer->findChild<QQuickItem*>(QStringLiteral("avatarEnabledCheck"));
    QVERIFY(check); clickItem(check); QTRY_VERIFY(personal->avatarEnabled());
    auto* image = drawer->findChild<QQuickItem*>(QStringLiteral("profileAvatarImage"));
    QVERIFY(image); QTRY_COMPARE(image->property("status").toInt(), 1); // Image.Ready
    captureFrame(QStringLiteral("personal-profile.png"));
    clickItem(drawer->findChild<QQuickItem*>(QStringLiteral("chooseAvatarButton")));
    auto* picker = drawer->findChild<QObject*>(QStringLiteral("avatarFilePicker"));
    QTRY_VERIFY(picker && picker->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(picker, "close"));

    QVERIFY(QMetaObject::invokeMethod(drawer, "openSection", Q_ARG(QVariant, QStringLiteral("Authorization"))));
    auto* cyberToggle = drawer->findChild<QQuickItem*>(QStringLiteral("cyberArkSettingsToggle"));
    QVERIFY(cyberToggle);
    if (!personal->cyberArkEnabled()) clickItem(cyberToggle);
    QTRY_VERIFY(personal->cyberArkEnabled());
    auto* username = drawer->findChild<QQuickItem*>(QStringLiteral("personalUsernameField"));
    QVERIFY(username && username->isVisible());
    username->setProperty("text", QStringLiteral("EMP000123"));
    captureFrame(QStringLiteral("personal-profile-before-save.png"));
    clickItem(drawer->findChild<QQuickItem*>(QStringLiteral("saveEmployeeIdButton")));
    QTRY_COMPARE(personal->employeeId(), QStringLiteral("EMP000123"));
    QCOMPARE(personal->routingIdentity(), QStringLiteral("EMP000123"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QTest::qWait(100);

    auto* manager = namedObject(QStringLiteral("routeManager"));
    QVERIFY(manager); QVERIFY(QMetaObject::invokeMethod(manager, "openManager"));
    QTRY_VERIFY(manager->property("visible").toBool());
    QTRY_VERIFY(manager->property("opened").toBool());
    auto* id = manager->findChild<QQuickItem*>(QStringLiteral("routeIdField"));
    auto* host = manager->findChild<QQuickItem*>(QStringLiteral("routeHostField"));
    auto* domain = manager->findChild<QQuickItem*>(QStringLiteral("accountDomainField"));
    QVERIFY(id && host && domain);
    id->setProperty("text", QStringLiteral("ui-psmp"));
    host->setProperty("text", QStringLiteral("psmp.example.test"));
    domain->setProperty("text", QStringLiteral("corp.example.test"));
    QTest::qWait(100); // DialogButtonBox orders and lays out its roles on polish.
    captureFrame(QStringLiteral("cyberark-route-before-save.png"));
    auto* saveRoute = manager->findChild<QQuickItem*>(QStringLiteral("saveRouteButton"));
    QSignalSpy routeClicks(saveRoute, SIGNAL(clicked()));
    clickItem(saveRoute);
    QCOMPARE(routeClicks.count(), 1);
    QTest::qWait(60);
    captureFrame(QStringLiteral("cyberark-route-after-save.png"));
    QTRY_VERIFY2(sessions->routes().contains(QStringLiteral("ui-psmp")), qPrintable(sessions->lastError() + QStringLiteral(" / ") + manager->property("error").toString()));
    captureFrame(QStringLiteral("cyberark-route.png"));
    QVERIFY(QMetaObject::invokeMethod(manager, "close")); QTest::qWait(150);

    // Persist a folder assignment and an OPC session through their actual QML forms.
    QVERIFY(sessions->addFolder({}, QStringLiteral("UI Routing")));
    auto* folderDialog = namedObject(QStringLiteral("folderConnectionDialog")); QVERIFY(folderDialog);
    QVERIFY(QMetaObject::invokeMethod(folderDialog, "openForFolder", Q_ARG(QVariant, QStringLiteral("UI Routing"))));
    QTRY_VERIFY(folderDialog->property("opened").toBool()); QTest::qWait(100);
    auto* folderChoice = folderDialog->findChild<QObject*>(QStringLiteral("folderConnectionChoice")); QVERIFY(folderChoice);
    folderChoice->setProperty("mode", QStringLiteral("route"));
    folderChoice->setProperty("routeId", QStringLiteral("ui-psmp"));
    QTest::qWait(80);
    captureFrame(QStringLiteral("cyberark-folder-rule.png"));
    clickItem(folderDialog->findChild<QQuickItem*>(QStringLiteral("saveFolderConnectionButton")));
    QTRY_VERIFY2(!folderDialog->property("visible").toBool(), qPrintable(sessions->lastError()));
    QCOMPARE(sessions->folderConnection(QStringLiteral("UI Routing")).value(QStringLiteral("route")).toString(), QStringLiteral("ui-psmp"));

    auto* editor = namedObject(QStringLiteral("sessionEditorDialog")); QVERIFY(editor);
    QVERIFY(QMetaObject::invokeMethod(editor, "openCreate", Q_ARG(QVariant, QStringLiteral("UI Routing"))));
    QTRY_VERIFY(editor->property("opened").toBool()); QTest::qWait(100);
    auto* sessionHost = editor->findChild<QQuickItem*>(QStringLiteral("sessionHostField")); QVERIFY(sessionHost);
    sessionHost->setProperty("text", QStringLiteral("opc.example.test"));
    auto* sessionChoice = editor->findChild<QObject*>(QStringLiteral("sessionConnectionChoice")); QVERIFY(sessionChoice);
    auto* overrideAccount = sessionChoice->findChild<QObject*>(QStringLiteral("overrideAccountCheck")); QVERIFY(overrideAccount);
    overrideAccount->setProperty("checked", true);
    auto* source = sessionChoice->findChild<QObject*>(QStringLiteral("accountSourceCombo")); QVERIFY(source);
    source->setProperty("currentIndex", 1);
    auto* accountText = sessionChoice->findChild<QObject*>(QStringLiteral("accountValueField")); QVERIFY(accountText);
    accountText->setProperty("text", QStringLiteral("opc"));
    QTest::qWait(100); captureFrame(QStringLiteral("cyberark-session-account.png"));
    QVERIFY(QMetaObject::invokeMethod(editor, "saveSession"));
    QTRY_VERIFY2(!editor->property("visible").toBool(), qPrintable(editor->property("formError").toString()));
    const int opcRow = sessions->find(QStringLiteral("opc.example.test")); QVERIFY(opcRow >= 0);
    QCOMPARE(sessions->prepareProfile(sessions->get(opcRow), personal->username()).value(QStringLiteral("login")).toString(),
        QStringLiteral("EMP000123@opc@opc.example.test"));
    QVERIFY(sessions->reload());
    const int reloadedOpc = sessions->find(QStringLiteral("opc.example.test")); QVERIFY(reloadedOpc >= 0);
    QVERIFY(QMetaObject::invokeMethod(editor, "openEdit", Q_ARG(QVariant, reloadedOpc)));
    QTRY_VERIFY(editor->property("opened").toBool());
    QCOMPARE(accountText->property("text").toString(), QStringLiteral("opc"));
    sessionChoice->setProperty("mode", QStringLiteral("direct"));
    QVERIFY(QMetaObject::invokeMethod(editor, "saveSession"));
    QTRY_VERIFY(!editor->property("visible").toBool());
    const auto directProfile = sessions->prepareProfile(sessions->get(reloadedOpc), {});
    QVERIFY(!directProfile.contains(QStringLiteral("error")));
    QVERIFY(!directProfile.value(QStringLiteral("profile")).toMap().contains(QStringLiteral("psmp")));
    QVERIFY(sessions->removeSession(reloadedOpc));
    QVERIFY(sessions->removeFolder(QStringLiteral("UI Routing")));

    QVariantMap profile{{QStringLiteral("host"), QStringLiteral("db.example.test")}, {QStringLiteral("name"), QStringLiteral("Offline route preview")},
        {QStringLiteral("connection"), QVariantMap{{QStringLiteral("mode"), QStringLiteral("route")}, {QStringLiteral("route"), QStringLiteral("ui-psmp")}}}};
    auto* pane = namedObject(QStringLiteral("terminalPane")); QVERIFY(pane);
    const int before = pane->property("tabCount").toInt();
    QVERIFY(QMetaObject::invokeMethod(pane, "openProfile", Q_ARG(QVariant, profile)));
    auto* approval = namedObject(QStringLiteral("routeApprovalDialog"));
    QTRY_VERIFY(approval && approval->property("visible").toBool());
    QCOMPARE(pane->property("tabCount").toInt(), before);
    const auto fingerprint = sessions->prepareProfile(profile, personal->username()).value(QStringLiteral("fingerprint")).toString();
    QVERIFY(!fingerprint.isEmpty()); QVERIFY(!personal->routeApproved(fingerprint));
    captureFrame(QStringLiteral("cyberark-gateway-review.png"));
    QVERIFY(QMetaObject::invokeMethod(approval, "reject"));
    QTRY_VERIFY(!approval->property("visible").toBool());
    QCOMPARE(pane->property("tabCount").toInt(), before);
    QVERIFY(!personal->routeApproved(fingerprint)); // never authorize or contact a PSMP server in UI tests
    // Exercise the QML ListModel/JSON -> TerminalView bridge, blocked before process launch.
    const auto prepared = sessions->prepareProfile(profile, personal->username()).value(QStringLiteral("profile"));
    auto* previousTerminal = pane->property("activeTerminal").value<QObject*>();
    QVERIFY(QMetaObject::invokeMethod(pane, "appendPreparedProfile", Q_ARG(QVariant, prepared)));
    QTRY_COMPARE(pane->property("tabCount").toInt(), before + 1);
    QTRY_VERIFY(pane->property("activeTerminal").value<QObject*>() != previousTerminal);
    auto* blockedTerminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
    QTRY_VERIFY(blockedTerminal != nullptr);
    QTRY_VERIFY2(blockedTerminal->statusText().contains(QStringLiteral("Review and approve")), qPrintable(blockedTerminal->statusText()));
    QVERIFY(!blockedTerminal->isRunning());
    QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, pane->property("currentIndex"))));
    QTRY_COMPARE(pane->property("tabCount").toInt(), before);
    QTRY_VERIFY(pane->property("activeTerminal").value<QObject*>() != nullptr);
    QVERIFY(personal->setUsername({}));
    QVERIFY(QMetaObject::invokeMethod(pane, "openProfile", Q_ARG(QVariant, profile)));
    auto* error = namedObject(QStringLiteral("connectionErrorDialog"));
    QTRY_VERIFY(error && error->property("visible").toBool());
    QCOMPARE(pane->property("tabCount").toInt(), before);
    QVERIFY(QMetaObject::invokeMethod(error, "close"));
    QTRY_VERIFY(!error->property("visible").toBool());
    QVERIFY(sessions->removeRoute(QStringLiteral("ui-psmp")));
    QVERIFY(personal->removeAvatar());
}

void UiInteractionTest::sshKeySettingsCyberArkToggleAndAboutUi() {
    auto* personal = m_engine->singletonInstance<bitshell::core::PersonalProfile*>("BitShell", "PersonalProfile");
    auto* settings = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(settings && personal);
    personal->setCyberArkEnabled(false);
    QVERIFY(QMetaObject::invokeMethod(settings, "openSection", Q_ARG(QVariant, QStringLiteral("Authorization"))));
    QTRY_VERIFY(settings->property("opened").toBool());
    auto* cyberToggle = settings->findChild<QQuickItem*>(QStringLiteral("cyberArkSettingsToggle"));
    auto* cyberDetails = settings->findChild<QQuickItem*>(QStringLiteral("cyberArkSettingsDetails"));
    auto* keyButton = settings->findChild<QQuickItem*>(QStringLiteral("openSshKeyManagerButton"));
    auto* aboutButton = settings->findChild<QQuickItem*>(QStringLiteral("openAboutButton"));
    QVERIFY(cyberToggle && cyberDetails && keyButton && aboutButton);
    QVERIFY(!cyberDetails->isVisible());
    clickItem(cyberToggle);
    QTRY_VERIFY(personal->cyberArkEnabled());
    QTRY_VERIFY(cyberDetails->isVisible());
    captureFrame(QStringLiteral("settings-keys-cyberark-expanded.png"));

    auto* keyManager = namedObject(QStringLiteral("sshKeyManager"));
    QVERIFY(keyManager);
    clickItem(keyButton);
    QTRY_VERIFY(keyManager->property("opened").toBool());
    QVERIFY(keyManager->findChild<QObject*>(QStringLiteral("ppkPassphraseField")));
    QVERIFY(keyManager->findChild<QObject*>(QStringLiteral("importSshKeysButton")));
    QVERIFY(keyManager->findChild<QObject*>(QStringLiteral("sshKeyFilePicker")));
    captureFrame(QStringLiteral("ssh-key-library-empty.png"));
    QVERIFY(QMetaObject::invokeMethod(keyManager, "close"));
    QTRY_VERIFY(!keyManager->property("visible").toBool());

    auto* about = namedObject(QStringLiteral("aboutDialog"));
    QVERIFY(about);
    QVERIFY(QMetaObject::invokeMethod(settings, "openSection", Q_ARG(QVariant, QStringLiteral("Data & About"))));
    QTRY_VERIFY(aboutButton->isVisible());
    clickItem(aboutButton);
    QTRY_VERIFY(about->property("opened").toBool());
    QCOMPARE(about->property("padding").toReal(), 20.0);
    QVERIFY(about->findChild<QObject*>(QStringLiteral("aboutGithubButton")));
    captureFrame(QStringLiteral("about-bitshell.png"));
    QVERIFY(QMetaObject::invokeMethod(about, "close"));
    QTRY_VERIFY(!about->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(settings, "close"));
    QTRY_VERIFY(!settings->property("visible").toBool());

    auto* editor = namedObject(QStringLiteral("sessionEditorDialog"));
    QVERIFY(editor);
    QVERIFY(QMetaObject::invokeMethod(editor, "openCreate", Q_ARG(QVariant, QString{})));
    QTRY_VERIFY(editor->property("opened").toBool());
    auto* choice = editor->findChild<QObject*>(QStringLiteral("sessionConnectionChoice"));
    auto* routeSwitch = editor->findChild<QQuickItem*>(QStringLiteral("cyberArkRoutingSwitch"));
    auto* identityCombo = editor->findChild<QObject*>(QStringLiteral("sessionIdentityCombo"));
    QVERIFY(choice && routeSwitch && identityCombo);
    QCOMPARE(choice->property("mode").toString(), QStringLiteral("inherit"));
    clickItem(routeSwitch);
    QTRY_COMPARE(choice->property("mode").toString(), QStringLiteral("route"));
    captureFrame(QStringLiteral("session-cyberark-toggle.png"));
    clickItem(routeSwitch);
    QTRY_COMPARE(choice->property("mode").toString(), QStringLiteral("direct"));
    QVERIFY(QMetaObject::invokeMethod(editor, "close"));
    QTRY_VERIFY(!editor->property("visible").toBool());
}

void UiInteractionTest::sessionSearchDropdownRefreshesAndUsesStableIds() {
    auto* store = m_engine->singletonInstance<bitshell::core::SessionStore*>("BitShell", "SessionStore"); QVERIFY(store);
    const QVariantMap rule{{QStringLiteral("mode"), QStringLiteral("route")}, {QStringLiteral("route"), QStringLiteral("missing-offline-test")}};
    const int first = store->addSession({{QStringLiteral("name"), QStringLiteral("searchfixture")}, {QStringLiteral("host"), QStringLiteral("one.example.test")},
        {QStringLiteral("connection"), rule}, {QStringLiteral("group"), QStringLiteral("Search QA/Prod")}});
    const int second = store->addSession({{QStringLiteral("name"), QStringLiteral("searchfixture replica")}, {QStringLiteral("host"), QStringLiteral("two.example.test")},
        {QStringLiteral("connection"), rule}, {QStringLiteral("group"), QStringLiteral("Search QA/Prod")}});
    QVERIFY(first >= 0 && second >= 0);
    const auto secondId = store->get(second).value(QStringLiteral("id")).toString();
    auto* field = visualItem(QStringLiteral("quickConnectField")); QVERIFY(field);
    auto* top = namedObject(QStringLiteral("topBar")); QVERIFY(top);
    QSignalSpy selected(top, SIGNAL(savedSessionRequested(QString)));
    clickItem(field); field->setProperty("text", QString{});
    QTest::keyClick(m_window, Qt::Key_Return);
    QCOMPARE(selected.count(), 0); // Empty input must never pick the first saved session.
    QVERIFY(field->hasActiveFocus());
    for (const char key : QByteArrayLiteral("SEARCHFIXTURE")) QTest::keyClick(m_window, key);
    QVERIFY(field->hasActiveFocus());
    auto* popup = namedObject(QStringLiteral("sessionSearchPopup"));
    QTRY_VERIFY(popup && popup->property("visible").toBool());
    auto* results = namedObject(QStringLiteral("sessionSearchResults")); QVERIFY(results);
    QTRY_COMPARE(results->property("count").toInt(), 2);
    QTRY_COMPARE(results->property("currentIndex").toInt(), 0);
    QTest::qWait(100); captureFrame(QStringLiteral("session-search-results.png"));
    QTest::keyClick(m_window, Qt::Key_Down);
    QCOMPARE(results->property("currentIndex").toInt(), 1);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_COMPARE(selected.count(), 1); QCOMPARE(selected[0][0].toString(), secondId);
    auto* error = namedObject(QStringLiteral("connectionErrorDialog"));
    QTRY_VERIFY(error && error->property("visible").toBool()); // Missing route prevents any network launch.
    QVERIFY(QMetaObject::invokeMethod(error, "close")); QTRY_VERIFY(!error->property("visible").toBool());
    clickItem(field); for (const char key : QByteArrayLiteral("searchfixture prod")) QTest::keyClick(m_window, key);
    QTRY_VERIFY(popup->property("visible").toBool()); QTRY_COMPARE(results->property("count").toInt(), 2);
    QVERIFY(store->removeSession(second));
    QTRY_COMPARE(results->property("count").toInt(), 1);
    field->setProperty("text", QString{}); for (const char key : QByteArrayLiteral("no-such-fixture")) QTest::keyClick(m_window, key);
    QTRY_COMPARE(results->property("count").toInt(), 0);
    auto* pane = namedObject(QStringLiteral("terminalPane")); const int count = pane->property("tabCount").toInt();
    QTest::keyClick(m_window, Qt::Key_Return); QTest::qWait(100);
    QCOMPARE(pane->property("tabCount").toInt(), count); // A failed search is not a connection attempt.
    QTest::keyClick(m_window, Qt::Key_Escape); QTRY_VERIFY(!popup->property("visible").toBool());
    field->setProperty("text", QString{});
    QVERIFY(store->removeSession(first)); QVERIFY(store->removeFolder(QStringLiteral("Search QA")));
}

void UiInteractionTest::splitPanesAndReviewedMultiSsh() {
    using bitshell::terminal::TerminalView;
    using bitshell::terminal::TerminalTestAccess;
    auto* pane = namedObject(QStringLiteral("terminalPane")); QVERIFY(pane);
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings"); QVERIFY(settings);
    settings->setSidebarVisible(false); m_window->setProperty("inspectorMode", 0); m_window->setWidth(1510); m_window->setHeight(900);
    while (pane->property("tabCount").toInt() > 1) {
        const int previousCount = pane->property("tabCount").toInt();
        QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, previousCount - 1)));
        QTRY_COMPARE(pane->property("tabCount").toInt(), previousCount - 1);
    }
    QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, 0)));
    QList<TerminalView*> terminals; QStringList ids;
    std::vector<std::unique_ptr<QSignalSpy>> outputs;
    const auto bytes = [&](int index) { QByteArray result; for (const auto& sample : *outputs[static_cast<size_t>(index)]) result += sample[0].toByteArray(); return result; };
    for (int i = 0; i < 6; ++i) {
        const QString label = QStringLiteral("Server %1").arg(i + 1);
        // A deliberately invalid username blocks SshSecurity before any child SSH launch.
        const QVariantMap profile{{QStringLiteral("name"), label}, {QStringLiteral("host"), QStringLiteral("fixture-%1.example.test").arg(i)}, {QStringLiteral("user"), QStringLiteral("blocked%")}};
        QVERIFY(QMetaObject::invokeMethod(pane, "appendPreparedProfile", Q_ARG(QVariant, profile)));
        QVariant id; QVERIFY(QMetaObject::invokeMethod(pane, "tabIdAt", Q_RETURN_ARG(QVariant, id), Q_ARG(QVariant, i + 1))); ids << id.toString();
        QTRY_VERIFY(visualItem(QStringLiteral("terminalTile:") + ids.last()) != nullptr);
        auto* tile = visualItem(QStringLiteral("terminalTile:") + ids.last());
        auto* terminal = tile->findChild<TerminalView*>(); QVERIFY(terminal); QVERIFY(!terminal->isRunning());
        outputs.push_back(std::make_unique<QSignalSpy>(terminal, &TerminalView::rawOutputReceived)); terminals << terminal;
        QVERIFY(TerminalTestAccess::startBroadcastFixture(*terminal, label)); QTRY_VERIFY(bytes(i).contains("READY"));
    }
    QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(pane, "activate", Q_ARG(QVariant, 0)));
    QVERIFY(QMetaObject::invokeMethod(pane, "setSplitCount", Q_ARG(QVariant, 6)));
    QTest::qWait(180);
    for (int i = 0; i < 6; ++i) {
        auto* tile = visualItem(QStringLiteral("terminalTile:") + ids[i]); QVERIFY(tile && tile->isVisible());
        QVERIFY(tile->width() > 300); QVERIFY(tile->height() > 200);
        for (int j = 0; j < i; ++j) {
            auto* other = visualItem(QStringLiteral("terminalTile:") + ids[j]);
            QVERIFY(!tile->mapRectToScene(tile->boundingRect()).intersects(other->mapRectToScene(other->boundingRect())));
        }
    }
    auto* lock = visualItem(QStringLiteral("openTabSshLock:") + ids[0]); QVERIFY(lock && lock->isVisible());
    QVERIFY(!visualItem(QStringLiteral("sessionSshLock")));
    clickItem(visualItem(QStringLiteral("multiSshToggle")));
    QTRY_VERIFY(pane->property("multiSshVisible").toBool());
    auto* bar = namedObject(QStringLiteral("multiSshBar")); QVERIFY(bar);
    auto* sender = bar->findChild<bitshell::terminal::BroadcastController*>(); QVERIFY(sender);
    for (int i : {0, 2}) {
        auto* tile = visualItem(QStringLiteral("terminalTile:") + ids[i]);
        clickItem(tile->findChild<QQuickItem*>(QStringLiteral("tileBroadcastCheck")));
        QTRY_VERIFY(terminals[i]->broadcastArmed());
    }
    QTRY_COMPARE(bar->property("targetCount").toInt(), 2);
    auto* command = bar->findChild<QQuickItem*>(QStringLiteral("broadcastCommandField")); QVERIFY(command);
    command->setProperty("text", QStringLiteral("INCLUDED_ONLY"));
    QTest::qWait(80); captureFrame(QStringLiteral("six-panes-selected.png"));
    clickItem(bar->findChild<QQuickItem*>(QStringLiteral("broadcastReviewButton")));
    auto* review = namedObject(QStringLiteral("broadcastReviewDialog")); QTRY_VERIFY(review && review->property("opened").toBool());
    QCOMPARE(sender->targets().size(), 2); QCOMPARE(sender->command(), QStringLiteral("INCLUDED_ONLY"));
    QTRY_VERIFY(m_window->activeFocusItem() && m_window->activeFocusItem()->objectName() == QStringLiteral("broadcastCancelButton"));
    QTest::qWait(80); captureFrame(QStringLiteral("multi-ssh-review.png"));
    QTest::keyClick(m_window, Qt::Key_Return); // Cancel has default focus.
    QTRY_VERIFY(!review->property("visible").toBool());
    for (int i = 0; i < 6; ++i) QVERIFY(!bytes(i).contains("ACK:INCLUDED_ONLY"));
    clickItem(bar->findChild<QQuickItem*>(QStringLiteral("broadcastReviewButton")));
    QTRY_VERIFY(review->property("opened").toBool()); QTest::qWait(80);
    clickItem(visualItem(QStringLiteral("broadcastSendButton")));
    QTRY_VERIFY(!review->property("visible").toBool());
    QTRY_VERIFY(bytes(0).contains("ACK:INCLUDED_ONLY")); QTRY_VERIFY(bytes(2).contains("ACK:INCLUDED_ONLY"));
    for (int i : {1, 3, 4, 5}) QVERIFY(!bytes(i).contains("ACK:INCLUDED_ONLY"));
    terminals[1]->forceActiveFocus();
    QTRY_COMPARE(pane->property("activeTerminal").value<QObject*>(), terminals[1]);
    for (const char key : QByteArrayLiteral("NORMAL_INPUT")) QTest::keyClick(m_window, key);
    QTest::keyClick(m_window, Qt::Key_Return);
    QTRY_VERIFY(bytes(1).contains("ACK:NORMAL_INPUT"));
    for (int i : {0, 2, 3, 4, 5}) QVERIFY(!bytes(i).contains("ACK:NORMAL_INPUT"));
    QVariant moved;
    QVERIFY(QMetaObject::invokeMethod(pane, "moveTab", Q_RETURN_ARG(QVariant, moved), Q_ARG(QVariant, 0), Q_ARG(QVariant, 5)));
    QVERIFY(moved.toBool()); QTRY_COMPARE(bar->property("targetCount").toInt(), 2);
    for (int i = 0; i < 6; ++i) QCOMPARE(visualItem(QStringLiteral("terminalTile:") + ids[i])->findChild<TerminalView*>(), terminals[i]);

    clickItem(bar->findChild<QQuickItem*>(QStringLiteral("multiSshTargetsButton")));
    auto* popup = namedObject(QStringLiteral("broadcastTargetsPopup")); QTRY_VERIFY(popup->property("opened").toBool());
    QVERIFY(QMetaObject::invokeMethod(pane, "setBroadcastMode", Q_ARG(QVariant, QStringLiteral("exclude"))));
    QCOMPARE(bar->property("targetCount").toInt(), 0);
    clickItem(visualItem(QStringLiteral("broadcastSelectAll")));
    QTRY_COMPARE(bar->property("targetCount").toInt(), 6);
    QTest::qWait(80); clickItem(visualItem(QStringLiteral("broadcastTarget:") + ids[1]));
    QTRY_COMPARE(bar->property("targetCount").toInt(), 5);
    captureFrame(QStringLiteral("multi-ssh-exclusions.png"));
    clickItem(visualItem(QStringLiteral("broadcastTargetsDone"))); QTRY_VERIFY(!popup->property("visible").toBool());
    command->setProperty("text", QStringLiteral("EXCLUDED_ONE"));
    QVERIFY(QMetaObject::invokeMethod(bar, "reviewCommand")); QTRY_VERIFY(review->property("opened").toBool());
    QCOMPARE(sender->targets().size(), 5);
    QVERIFY(QMetaObject::invokeMethod(review, "accept"));
    for (int i : {0, 2, 3, 4, 5}) QTRY_VERIFY(bytes(i).contains("ACK:EXCLUDED_ONE"));
    QVERIFY(!bytes(1).contains("ACK:EXCLUDED_ONE"));

    for (int count : {1, 2, 4, 6}) {
        QVERIFY(QMetaObject::invokeMethod(pane, "setSplitCount", Q_ARG(QVariant, count))); QTest::qWait(50);
        for (int i = 0; i < 6; ++i) {
            QCOMPARE(visualItem(QStringLiteral("terminalTile:") + ids[i])->findChild<TerminalView*>(), terminals[i]);
            QVERIFY(terminals[i]->isRunning());
        }
    }
    command->setProperty("text", QStringLiteral("MUST_NOT_SEND"));
    QVERIFY(QMetaObject::invokeMethod(bar, "reviewCommand")); QTRY_VERIFY(review->property("visible").toBool());
    TerminalTestAccess::stopFixture(*terminals[0]);
    QVERIFY(QMetaObject::invokeMethod(review, "accept"));
    for (int i = 0; i < 6; ++i) QVERIFY(!bytes(i).contains("ACK:MUST_NOT_SEND"));
    QVERIFY(!sender->pending());
    QVERIFY(QMetaObject::invokeMethod(pane, "newLocalSession"));
    QTRY_COMPARE(pane->property("tabCount").toInt(), 7);
    pane->setProperty("broadcastScope", QStringLiteral("visible"));
    QTRY_COMPARE(bar->property("targetCount").toInt(), 0); // New page/local terminal cannot become a target.
    QVERIFY(QMetaObject::invokeMethod(pane, "pageSessions", Q_ARG(QVariant, -1)));
    QTRY_COMPARE(bar->property("targetCount").toInt(), 4);
    m_window->setWidth(1080); QTest::qWait(120); captureFrame(QStringLiteral("six-panes-compact.png"));
    settings->setSidebarVisible(true); m_window->setHeight(680);
    QTRY_COMPARE(namedObject(QStringLiteral("connectionsPanel"))->property("reveal").toReal(), 1.0);
    auto* stage = namedObject(QStringLiteral("terminalStage"));
    QTRY_COMPARE(stage->property("columns").toInt(), 2);
    QTRY_COMPARE(stage->property("rows").toInt(), 3);
    auto* activeTile = qobject_cast<TerminalView*>(pane->property("activeTerminal").value<QObject*>())->parentItem();
    QVERIFY(QMetaObject::invokeMethod(pane, "openSearch"));
    auto* findBar = activeTile->findChild<QQuickItem*>(QStringLiteral("terminalFindBar"));
    QTRY_VERIFY(findBar && findBar->isVisible());
    QVERIFY(activeTile->boundingRect().contains(activeTile->mapRectFromItem(findBar, findBar->boundingRect())));
    auto* stoppedTile = visualItem(QStringLiteral("terminalTile:") + ids[0]);
    auto* restart = stoppedTile->findChild<QQuickItem*>(QStringLiteral("terminalRestartOverlay"));
    QVERIFY(restart && restart->isVisible());
    QVERIFY(stoppedTile->boundingRect().contains(stoppedTile->mapRectFromItem(restart, restart->boundingRect())));
    captureFrame(QStringLiteral("six-panes-sidebar-compact.png"));
    QVERIFY(QMetaObject::invokeMethod(activeTile, "closeSearch"));
    QVERIFY(QMetaObject::invokeMethod(pane, "clearBroadcastTargets"));
    pane->setProperty("multiSshVisible", false);
    QVERIFY(QMetaObject::invokeMethod(pane, "setSplitCount", Q_ARG(QVariant, 1)));
    while (pane->property("tabCount").toInt() > 1) {
        const int previousCount = pane->property("tabCount").toInt();
        QVERIFY(QMetaObject::invokeMethod(pane, "closeTab", Q_ARG(QVariant, 0)));
        QTRY_COMPARE(pane->property("tabCount").toInt(), previousCount - 1);
    }
    QTRY_VERIFY(pane->property("activeTerminal").value<QObject*>() != nullptr);
    settings->setSidebarVisible(true);
}

void UiInteractionTest::deletesSingleSelectedFoldersAndWholeCatalogSafely() {
    auto* sidebar = namedObject(QStringLiteral("catalogSidebar"));
    auto* store = m_engine->singletonInstance<bitshell::core::SessionStore*>("BitShell", "SessionStore");
    auto* settings = m_engine->singletonInstance<bitshell::core::AppSettings*>("BitShell", "AppSettings");
    QVERIFY(sidebar && store && settings);
    settings->setSidebarVisible(true);
    QTest::qWait(250);
    const auto add = [&](const QString& id, const QString& name, const QString& folder) {
        return store->addSession({{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
                                 {QStringLiteral("host"), QStringLiteral("deletion.example.test")},
                                 {QStringLiteral("group"), folder}});
    };
    QVERIFY(add(QStringLiteral("delete-a"), QStringLiteral("delete-visible-a"), QStringLiteral("Delete QA/Child")) >= 0);
    QVERIFY(add(QStringLiteral("delete-b"), QStringLiteral("delete-visible-b"), QStringLiteral("Delete QA/Child")) >= 0);
    QVERIFY(add(QStringLiteral("hidden-keep"), QStringLiteral("hidden-keep"), QStringLiteral("Delete QA/Child")) >= 0);
    QVERIFY(add(QStringLiteral("prefix-keep"), QStringLiteral("prefix-keep"), QStringLiteral("Delete QAX")) >= 0);
    sidebar->setProperty("filterText", QStringLiteral("delete-visible"));
    QTRY_VERIFY(treeRow(QStringLiteral("delete-visible-a"), false));
    clickItem(visualItem(QStringLiteral("catalogMenuButton")));
    auto* catalogMenu = namedObject(QStringLiteral("catalogContextMenu"));
    QTRY_VERIFY(catalogMenu->property("opened").toBool());
    clickItem(qobject_cast<QQuickItem*>(namedObject(QStringLiteral("selectSessionsMenuItem"))));
    QTRY_VERIFY(sidebar->property("selectionMode").toBool());
    QTRY_VERIFY(!catalogMenu->property("visible").toBool());
    QTest::qWait(120); // Wait for menu exit and the newly visible checkbox layout.
    auto* row = treeRow(QStringLiteral("delete-visible-a"), false);
    captureFrame(QStringLiteral("catalog-before-selection.png"));
    clickItem(row->findChild<QQuickItem*>(QStringLiteral("catalogNodeCheckbox")));
    QTRY_COMPARE(sidebar->property("selectedCount").toInt(), 1);
    clickItem(visualItem(QStringLiteral("selectVisibleSessionsButton")));
    QTRY_COMPARE(sidebar->property("selectedCount").toInt(), 2);
    captureFrame(QStringLiteral("catalog-multi-selection.png"));
    const int before = store->count();
    clickItem(visualItem(QStringLiteral("deleteSelectedSessionsButton")));
    auto* dialog = namedObject(QStringLiteral("deleteCatalogEntriesDialog"));
    QTRY_VERIFY(dialog->property("opened").toBool());
    auto pending = sidebar->property("pendingDeletion").value<QJSValue>().toVariant().toMap();
    QCOMPARE(pending.value(QStringLiteral("sessions")).toInt(), 2);
    QCOMPARE(pending.value(QStringLiteral("folderCount")).toInt(), 0);
    auto* cancel = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("cancelCatalogDeletionButton")));
    auto* confirm = qobject_cast<QQuickItem*>(namedObject(QStringLiteral("confirmCatalogDeletionButton")));
    QVERIFY(cancel && confirm);
    QTRY_VERIFY(cancel->hasActiveFocus());
    captureFrame(QStringLiteral("catalog-delete-confirmation.png"));
    clickItem(cancel);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(store->count(), before);

    // The same UI confirmation must not delete a different revision of the tree.
    clickItem(visualItem(QStringLiteral("deleteSelectedSessionsButton")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    QVERIFY(add(QStringLiteral("new-keep"), QStringLiteral("new-keep"), {}) >= 0);
    clickItem(confirm);
    QTRY_VERIFY(!dialog->property("visible").toBool());
    QCOMPARE(store->count(), before + 1);
    QVERIFY(sidebar->property("noticeText").toString().contains(QStringLiteral("changed")));
    clickItem(visualItem(QStringLiteral("deleteSelectedSessionsButton")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    clickItem(confirm);
    QTRY_COMPARE(store->count(), before - 1);
    QVERIFY(!store->getById(QStringLiteral("hidden-keep")).isEmpty());
    QTRY_VERIFY(!treeRow(QStringLiteral("delete-visible-a"), false));
    clickItem(visualItem(QStringLiteral("finishSessionSelectionButton")));

    // Nonempty folder deletion includes descendants but never a prefix sibling.
    sidebar->setProperty("filterText", QStringLiteral("Delete QA"));
    QTRY_VERIFY(treeRow(QStringLiteral("Delete QA"), true));
    row = treeRow(QStringLiteral("Delete QA"), true);
    clickItem(row->findChild<QQuickItem*>(QStringLiteral("treeMenuButton")));
    auto* folderMenu = row->findChild<QObject*>(QStringLiteral("folderContextMenu"));
    QTRY_VERIFY(folderMenu->property("opened").toBool());
    clickItem(folderMenu->findChild<QQuickItem*>(QStringLiteral("deleteFolderMenuItem")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    pending = sidebar->property("pendingDeletion").value<QJSValue>().toVariant().toMap();
    QCOMPARE(pending.value(QStringLiteral("sessions")).toInt(), 1);
    QCOMPARE(pending.value(QStringLiteral("folderCount")).toInt(), 2);
    captureFrame(QStringLiteral("catalog-folder-deletion.png"));
    clickItem(confirm);
    QTRY_VERIFY(store->getById(QStringLiteral("hidden-keep")).isEmpty());
    QVERIFY(!store->getById(QStringLiteral("prefix-keep")).isEmpty());
    QTRY_VERIFY(!treeRow(QStringLiteral("Delete QA"), true));

    // Single entry deletion remains available through its own context menu.
    sidebar->setProperty("filterText", QStringLiteral("new-keep"));
    QTRY_VERIFY(treeRow(QStringLiteral("new-keep"), false));
    row = treeRow(QStringLiteral("new-keep"), false);
    clickItem(row->findChild<QQuickItem*>(QStringLiteral("treeMenuButton")));
    auto* sessionMenu = row->findChild<QObject*>(QStringLiteral("sessionContextMenu"));
    QTRY_VERIFY(sessionMenu->property("opened").toBool());
    clickItem(sessionMenu->findChild<QQuickItem*>(QStringLiteral("deleteSessionMenuItem")));
    QTRY_VERIFY(dialog->property("opened").toBool());
    clickItem(confirm);
    QTRY_VERIFY(store->getById(QStringLiteral("new-keep")).isEmpty());

    // Clear-all is explicit and includes entries hidden by the current search.
    clickItem(visualItem(QStringLiteral("catalogMenuButton")));
    QTRY_VERIFY(catalogMenu->property("opened").toBool());
    clickItem(qobject_cast<QQuickItem*>(namedObject(QStringLiteral("deleteAllSessionsMenuItem"))));
    QTRY_VERIFY(dialog->property("opened").toBool());
    pending = sidebar->property("pendingDeletion").value<QJSValue>().toVariant().toMap();
    QCOMPARE(pending.value(QStringLiteral("sessions")).toInt(), store->count());
    QCOMPARE(pending.value(QStringLiteral("folderCount")).toInt(), store->folderCount());
    clickItem(confirm);
    QTRY_COMPARE(store->count(), 0);
    QCOMPARE(store->folderCount(), 0);
    QVERIFY(store->reload());
    QCOMPARE(store->count(), 0);
    sidebar->setProperty("filterText", QString{});
    captureFrame(QStringLiteral("catalog-empty-after-delete.png"));
}

void UiInteractionTest::localProfilesDefaultMenuAndSettings() {
    auto* store = m_engine->singletonInstance<bitshell::core::LocalProfileStore*>("BitShell", "LocalProfileStore");
    QVERIFY(store);
    auto* pane = namedObject(QStringLiteral("terminalPane"));
    QVERIFY(pane);
    const auto oldDefault = store->defaultProfileId();
    auto profile = store->defaultProfile();
    profile[QStringLiteral("id")] = QString{};
    profile[QStringLiteral("name")] = QStringLiteral("Local profile UI fixture");
    profile[QStringLiteral("workingDirectory")] = m_configRoot;
#ifdef Q_OS_WIN
    profile[QStringLiteral("program")] = QCoreApplication::applicationDirPath() + QStringLiteral("/windows_terminal_fixture.exe");
    profile[QStringLiteral("arguments")] = QVariantList{QStringLiteral("profile")};
#else
    profile[QStringLiteral("program")] = QStringLiteral("/bin/sh");
    profile[QStringLiteral("arguments")] = QVariantList{};
#endif
    const auto id = store->saveProfile(profile);
    QVERIFY2(!id.isEmpty(), qPrintable(store->lastError()));
    QVERIFY(store->setDefaultProfile(id));
    const int before = pane->property("tabCount").toInt();
    clickItem(visualItem(QStringLiteral("newLocalTerminalButton")));
    QTRY_COMPARE(pane->property("tabCount").toInt(), before + 1);
    auto* terminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
    QVERIFY(terminal);
    QTRY_COMPARE(terminal->activeTarget(), QStringLiteral("Local profile UI fixture"));
    QTRY_VERIFY(terminal->isRunning());
    QSignalSpy output(terminal, &bitshell::terminal::TerminalView::rawOutputReceived);
    terminal->restartSession();
    QTRY_VERIFY(output.size() > 0);
    QTRY_COMPARE(terminal->statusText(), QStringLiteral("Local ready"));
    QCOMPARE(terminal->activeTarget(), QStringLiteral("Local profile UI fixture"));
    clickItem(visualItem(QStringLiteral("localProfileMenuButton")));
    auto* menu = namedObject(QStringLiteral("localProfileMenu"));
    QVERIFY(menu); QTRY_VERIFY(menu->property("opened").toBool());
    auto* launch = visualItem(QStringLiteral("launchLocalProfile:") + id);
    QVERIFY(launch);
    captureFrame(QStringLiteral("local-profile-launch-menu.png"));
    clickItem(launch);
    QTRY_COMPARE(pane->property("tabCount").toInt(), before + 2);
    QTRY_VERIFY(!menu->property("visible").toBool());
    clickItem(visualItem(QStringLiteral("localProfileMenuButton")));
    QTRY_VERIFY(menu->property("opened").toBool());
    clickItem(visualItem(QStringLiteral("manageLocalProfiles")));
    auto* drawer = namedObject(QStringLiteral("settingsDrawer"));
    QVERIFY(drawer); QTRY_VERIFY(drawer->property("opened").toBool());
    QVERIFY(namedObject(QStringLiteral("localProfileName")));
    clickItem(visualItem(QStringLiteral("addLocalProfile")));
    namedObject(QStringLiteral("localProfileName"))->setProperty("text", QStringLiteral("Edited local fixture"));
    namedObject(QStringLiteral("localProfileProgram"))->setProperty("text", profile.value(QStringLiteral("program")));
    namedObject(QStringLiteral("localProfileArguments"))->setProperty("text", profile.value(QStringLiteral("arguments")).toStringList().join(u'\n'));
    namedObject(QStringLiteral("localProfileDirectory"))->setProperty("text", m_configRoot);
    auto* scroll = namedObject(QStringLiteral("localProfilesSettingsScroll"));
    QVERIFY(scroll);
    auto* flickable = scroll->property("contentItem").value<QObject*>();
    QVERIFY(flickable);
    flickable->setProperty("contentY", std::max(0.0, flickable->property("contentHeight").toDouble() - flickable->property("height").toDouble()));
    QTest::qWait(100);
    clickItem(visualItem(QStringLiteral("saveLocalProfile")));
    QVERIFY2(store->lastError().isEmpty(), qPrintable(store->lastError()));
    QString edited;
    for (const auto& row : store->profiles())
        if (row.toMap().value(QStringLiteral("name")) == QStringLiteral("Edited local fixture")) edited = row.toMap().value(QStringLiteral("id")).toString();
    QVERIFY(!edited.isEmpty());
    auto* defaultCombo = namedObject(QStringLiteral("defaultLocalProfileCombo"));
    QVERIFY(defaultCombo);
    int selected = -1;
    const auto rows = store->profiles();
    for (int i = 0; i < rows.size(); ++i) if (rows[i].toMap().value(QStringLiteral("id")) == edited) selected = i;
    QVERIFY(selected >= 0);
    defaultCombo->setProperty("currentIndex", selected);
    QVERIFY(QMetaObject::invokeMethod(defaultCombo, "activated", Q_ARG(int, selected)));
    QCOMPARE(store->defaultProfileId(), edited);
    captureFrame(QStringLiteral("local-profile-settings.png"));
    QVERIFY(QMetaObject::invokeMethod(drawer, "close"));
    QTRY_VERIFY(!drawer->property("visible").toBool());
    QVERIFY(store->setDefaultProfile(oldDefault));
    QVERIFY(store->removeProfile(id));
    QVERIFY(store->removeProfile(edited));
#ifdef Q_OS_WIN
    // Optional real-machine acceptance: CI need not install or boot a WSL distribution.
    const auto distro = qEnvironmentVariable("BIT8SHELL_TEST_WSL_DISTRIBUTION");
    if (!distro.isEmpty()) {
        QTRY_VERIFY_WITH_TIMEOUT(!store->discovering(), 10000);
        bool discovered = false;
        for (const auto& row : store->profiles())
            discovered |= row.toMap().value(QStringLiteral("id")) == QStringLiteral("wsl:") + distro;
        QVERIFY2(discovered, qPrintable(store->discoveryMessage()));
        auto wslProfile = store->profile(QStringLiteral("wsl:") + distro);
        wslProfile[QStringLiteral("workingDirectory")] = QStringLiteral("/tmp");
        wslProfile[QStringLiteral("arguments")] = QVariantList{QStringLiteral("--exec"), QStringLiteral("/bin/sh"),
            QStringLiteral("-c"), QStringLiteral("printf 'BIT8SHELL_WSL_DIR='; pwd")};
        terminal = qobject_cast<bitshell::terminal::TerminalView*>(pane->property("activeTerminal").value<QObject*>());
        QVERIFY(terminal);
        QSignalSpy wslOutput(terminal, &bitshell::terminal::TerminalView::rawOutputReceived);
        QVERIFY(terminal->startLocalProfile(wslProfile));
        QByteArray received;
        QTRY_VERIFY2_WITH_TIMEOUT(([&] {
            received.clear();
            for (const auto& sample : wslOutput) received += sample.at(0).toByteArray();
            bitshell::core::AnsiStripper stripper;
            return stripper.filter(received).contains("BIT8SHELL_WSL_DIR=/tmp");
        }()), received.constData(), 20000);
        captureFrame(QStringLiteral("local-profile-wsl-live.png"));
    }
#endif
}

void UiInteractionTest::cleanupTestCase() {
    // Drain queued model work while the window can still incubate/render items.
    QCoreApplication::processEvents();
    if (m_engine && m_engine->incubationController()) {
        QTRY_COMPARE(m_engine->incubationController()->incubatingObjectCount(), 0);
    }
    if (m_window != nullptr) {
        m_window->close();
    }
    m_engine.reset();
    m_window = nullptr;
}

int main(int argc, char* argv[]) {
    QTemporaryDir configRoot(QDir::tempPath()
                                 + QStringLiteral("/bitshell-ui-test-XXXXXX"));
    if (!configRoot.isValid()) {
        return EXIT_FAILURE;
    }
    qputenv("XDG_CONFIG_HOME", configRoot.path().toUtf8());
    qputenv("XDG_CACHE_HOME", configRoot.filePath(QStringLiteral("cache")).toUtf8());
    qputenv("SHELL", QByteArrayLiteral("/bin/sh"));
#ifdef Q_OS_WIN
    // The offscreen plugin has no DirectWrite font database. Point its font
    // loader at the installed OS fonts; otherwise screenshots contain only tofu
    // glyphs and the font-picker model misleadingly contains one placeholder.
    if (qEnvironmentVariable("QT_QPA_PLATFORM") == QStringLiteral("offscreen"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("SystemRoot") + QStringLiteral("/Fonts")).toUtf8());
#endif

    QGuiApplication app(argc, argv);
    if (QFontDatabase::families().isEmpty()) {
        qCritical("UI tests require a real font database.");
        return EXIT_FAILURE;
    }
    UiInteractionTest test(configRoot.path());
    return QTest::qExec(&test, argc, argv);
}

#include "UiInteractionTest.moc"
