#include <QtTest>

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTime>
#include <QSettings>
#include <QScopeGuard>

#include <bitshell/core/AppSettings.h>

using bitshell::core::AppSettings;

class AppSettingsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void clampsAndPersistsSettings();
    void appearanceScheduleBoundaries();
    void appearanceModesAndPersistence();
    void linkedThemesFollowAppearanceAndPreserveSeparateChoices();
    void themeLinkageLoadsOlderProfilesAndRoundTrips();
    void themeLinkageRejectsFailedSave();
    void renderingModesPersistAndRejectInvalid();
    void versionTapsUnlockAndPersistBonusThemes();
    void commandPaletteShortcutPersistsAndRejectsConflicts();
    void commandPaletteShortcutLoadsOlderProfiles();
    void commandPaletteShortcutMigratesLegacyCollision();

private:
    QTemporaryDir m_configRoot;
};

void AppSettingsTest::initTestCase() {
    QVERIFY(m_configRoot.isValid());
    qputenv("XDG_CONFIG_HOME", m_configRoot.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("app-settings-test"));
}

void AppSettingsTest::versionTapsUnlockAndPersistBonusThemes() {
    AppSettings settings;
    settings.restoreDefaults();
    QVERIFY(!settings.bonusThemesUnlocked());
    for (int tap = 0; tap < 6; ++tap) QVERIFY(!settings.registerVersionTap());
    QTest::qWait(1600);
    QVERIFY(!settings.registerVersionTap());
    QVERIFY(!settings.bonusThemesUnlocked());
    for (int tap = 0; tap < 5; ++tap) QVERIFY(!settings.registerVersionTap());
    QVERIFY(settings.registerVersionTap());
    QVERIFY(settings.bonusThemesUnlocked());
    AppSettings restarted;
    QVERIFY(restarted.bonusThemesUnlocked());
    QVERIFY(!restarted.registerVersionTap());
    settings.restoreDefaults();
}

void AppSettingsTest::clampsAndPersistsSettings() {
    AppSettings settings;
    QVERIFY(settings.storagePath().startsWith(m_configRoot.path()));
    QVERIFY(settings.storagePath().endsWith(QStringLiteral("8bitshell/settings.yaml")));
    settings.setTerminalFontFamily(QStringLiteral("Test Mono"));
    settings.setTerminalFontSize(999.0);
    settings.setScrollbackLines(1);
    settings.setBackspaceMode(QStringLiteral("bs"));
    settings.setUiThemeId(QStringLiteral("nord"));
    settings.setTerminalThemeId(QStringLiteral("dracula"));
    settings.setLogByDefault(false);
    settings.setHighlightByDefault(false);
    settings.setLogDirectory(m_configRoot.filePath(QStringLiteral("logs")));
    settings.setLogDirectoryTemplate(QStringLiteral("{host}/{date}"));
    settings.setLogFileNameTemplate(QStringLiteral("{session}.log"));
    settings.setLogTimestamps(true);
    settings.setLogAppend(false);
    settings.setLogRotationMegabytes(64);
    settings.setLogRetainedFiles(9);
    settings.setExpandedSessionFolders(
        {QStringLiteral("Production"), QStringLiteral("Production/Oracle")});
    settings.setFileBrowserVisible(true);

    QCOMPARE(settings.terminalFontSize(), 40.0);
    QCOMPARE(settings.scrollbackLines(), 100);

    AppSettings reloaded;
    QCOMPARE(reloaded.terminalFontFamily(), QStringLiteral("Test Mono"));
    QCOMPARE(reloaded.terminalFontSize(), 40.0);
    QCOMPARE(reloaded.scrollbackLines(), 100);
    QCOMPARE(reloaded.backspaceMode(), QStringLiteral("bs"));
    QCOMPARE(reloaded.uiThemeId(), QStringLiteral("nord"));
    QCOMPARE(reloaded.terminalThemeId(), QStringLiteral("dracula"));
    QVERIFY(!reloaded.logByDefault());
    QVERIFY(!reloaded.highlightByDefault());
    QCOMPARE(reloaded.logDirectory(), m_configRoot.filePath(QStringLiteral("logs")));
    QCOMPARE(reloaded.logDirectoryTemplate(), QStringLiteral("{host}/{date}"));
    QCOMPARE(reloaded.logFileNameTemplate(), QStringLiteral("{session}.log"));
    QVERIFY(reloaded.logTimestamps());
    QVERIFY(!reloaded.logAppend());
    QCOMPARE(reloaded.logRotationMegabytes(), 64);
    QCOMPARE(reloaded.logRetainedFiles(), 9);
    QCOMPARE(reloaded.expandedSessionFolders(),
             QStringList({QStringLiteral("Production"),
                          QStringLiteral("Production/Oracle")}));
    QVERIFY(reloaded.fileBrowserVisible());

    reloaded.restoreDefaults();
    QCOMPARE(reloaded.terminalFontSize(), 13.0);
    QCOMPARE(reloaded.scrollbackLines(), 10000);
    QCOMPARE(reloaded.backspaceMode(), QStringLiteral("del"));
    QCOMPARE(reloaded.uiThemeId(), QStringLiteral("catppuccin-mocha"));
    QCOMPARE(reloaded.terminalThemeId(), QStringLiteral("xterm"));
    QCOMPARE(reloaded.uiLightThemeId(), QStringLiteral("catppuccin-latte"));
    QCOMPARE(reloaded.terminalLightThemeId(), QStringLiteral("xterm"));
    QVERIFY(!reloaded.fileBrowserVisible());
    QVERIFY(reloaded.logByDefault());
    QVERIFY(reloaded.highlightByDefault());
    QVERIFY(!reloaded.terminalFontFamily().isEmpty());
    QVERIFY(!reloaded.logDirectory().isEmpty());
    QCOMPARE(reloaded.logDirectoryTemplate(), QStringLiteral("{session}"));
    QCOMPARE(reloaded.logFileNameTemplate(),
             QStringLiteral("{session}_{date}_{time}.log"));
    QVERIFY(!reloaded.logTimestamps());
    QVERIFY(reloaded.logAppend());
    QCOMPARE(reloaded.logRotationMegabytes(), 25);
    QCOMPARE(reloaded.logRetainedFiles(), 5);
}

void AppSettingsTest::appearanceScheduleBoundaries() {
    QVERIFY(!AppSettings::isDayAt(419, 420, 1140));
    QVERIFY(AppSettings::isDayAt(420, 420, 1140));
    QVERIFY(AppSettings::isDayAt(1139, 420, 1140));
    QVERIFY(!AppSettings::isDayAt(1140, 420, 1140));
    QVERIFY(!AppSettings::isDayAt(1439, 420, 1140));
    // Night-shift users can have a day interval crossing midnight.
    QVERIFY(AppSettings::isDayAt(1380, 1320, 360));
    QVERIFY(AppSettings::isDayAt(0, 1320, 360));
    QVERIFY(!AppSettings::isDayAt(360, 1320, 360));
    QVERIFY(!AppSettings::isDayAt(900, 1320, 360));
    QVERIFY(!AppSettings::isDayAt(720, 420, 420));
}

void AppSettingsTest::appearanceModesAndPersistence() {
    AppSettings settings;
    settings.restoreDefaults();
    settings.setSeparateTerminalTheme(true);
    settings.setUiThemeId(QStringLiteral("nord"));
    settings.setTerminalThemeId(QStringLiteral("dracula"));
    settings.setUiLightThemeId(QStringLiteral("github-light"));
    // A dark terminal in daytime is an intentional, supported matrix choice.
    settings.setTerminalLightThemeId(QStringLiteral("nord"));
    settings.setAppearanceMode(QStringLiteral("light"));
    QVERIFY(!settings.darkAppearance());
    QCOMPARE(settings.effectiveUiThemeId(), QStringLiteral("github-light"));
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("nord"));
    settings.toggleAppearance();
    QCOMPARE(settings.appearanceMode(), QStringLiteral("dark"));
    QCOMPARE(settings.effectiveUiThemeId(), QStringLiteral("nord"));
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("dracula"));

    const QTime now = QTime::currentTime();
    settings.setDayStart(now.addSecs(-3600).toString(QStringLiteral("HH:mm")));
    settings.setNightStart(now.addSecs(3600).toString(QStringLiteral("HH:mm")));
    settings.setAppearanceMode(QStringLiteral("schedule"));
    QVERIFY(!settings.darkAppearance());
    settings.setDayStart(now.addSecs(3600).toString(QStringLiteral("HH:mm")));
    settings.setNightStart(now.addSecs(-3600).toString(QStringLiteral("HH:mm")));
    QVERIFY(settings.darkAppearance());
    settings.toggleAppearance();
    QVERIFY(!settings.darkAppearance());
    QCOMPARE(settings.appearanceMode(), QStringLiteral("light"));

    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    settings.setAppearanceMode(QStringLiteral("system"));
    QCOMPARE(settings.darkAppearance(), scheme != Qt::ColorScheme::Light);
    QCOMPARE(settings.systemThemeAvailable(), scheme != Qt::ColorScheme::Unknown);
    // Deliver the same notifications as a platform plugin, without changing the desktop.
    auto* hints = QGuiApplication::styleHints();
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged",
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Light)));
    QVERIFY(!settings.darkAppearance());
    QCOMPARE(settings.effectiveUiThemeId(), QStringLiteral("github-light"));
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged",
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QVERIFY(settings.darkAppearance());
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("dracula"));
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged",
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Unknown)));
    QVERIFY(!settings.systemThemeAvailable());
    QVERIFY(settings.darkAppearance());
    settings.setAppearanceMode(QStringLiteral("light"));
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged",
                                      Q_ARG(Qt::ColorScheme, Qt::ColorScheme::Dark)));
    QVERIFY(!settings.darkAppearance());
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Q_ARG(Qt::ColorScheme, scheme)));
    settings.setAppearanceMode(QStringLiteral("system"));
    settings.setAppearanceMode(QStringLiteral("invalid"));
    QCOMPARE(settings.appearanceMode(), QStringLiteral("system"));
    settings.setDayStart(QStringLiteral("08:30"));
    settings.setNightStart(QStringLiteral("20:45"));
    settings.setDayStart(QStringLiteral("25:12"));
    QCOMPARE(settings.dayStart(), QStringLiteral("08:30"));
    {
        AppSettings reloaded;
        QCOMPARE(reloaded.appearanceMode(), QStringLiteral("system"));
        QCOMPARE(reloaded.dayStart(), QStringLiteral("08:30"));
        QCOMPARE(reloaded.nightStart(), QStringLiteral("20:45"));
        QCOMPARE(reloaded.uiThemeId(), QStringLiteral("nord"));
        QCOMPARE(reloaded.terminalThemeId(), QStringLiteral("dracula"));
        QCOMPARE(reloaded.uiLightThemeId(), QStringLiteral("github-light"));
        QCOMPARE(reloaded.terminalLightThemeId(), QStringLiteral("nord"));
        QVERIFY(reloaded.separateTerminalTheme());
    }
    settings.restoreDefaults();
    QCOMPARE(settings.appearanceMode(), QStringLiteral("dark"));
    QCOMPARE(settings.uiLightThemeId(), QStringLiteral("catppuccin-latte"));
    QVERIFY(!settings.separateTerminalTheme());
}

void AppSettingsTest::linkedThemesFollowAppearanceAndPreserveSeparateChoices() {
    AppSettings settings;
    settings.restoreDefaults();
    QVERIFY(!settings.separateTerminalTheme());
    QCOMPARE(settings.effectiveTerminalThemeId(), settings.effectiveUiThemeId());
    settings.setTerminalThemeId(QStringLiteral("dracula"));
    settings.setTerminalLightThemeId(QStringLiteral("xterm"));
    settings.setUiThemeId(QStringLiteral("cyberpunk-silverhand"));
    settings.setUiLightThemeId(QStringLiteral("github-light"));
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("cyberpunk-silverhand"));
    settings.toggleAppearance();
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("github-light"));

    QSignalSpy appearance(&settings, &AppSettings::appearanceChanged);
    settings.setSeparateTerminalTheme(true);
    QCOMPARE(appearance.count(), 1);
    QCOMPARE(settings.effectiveUiThemeId(), QStringLiteral("github-light"));
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("xterm"));
    settings.setSeparateTerminalTheme(true);
    QCOMPARE(appearance.count(), 1); // Selecting the current mode is a no-op.
    settings.toggleAppearance();
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("dracula"));
    appearance.clear();
    settings.setSeparateTerminalTheme(false);
    QCOMPARE(appearance.count(), 1);
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("cyberpunk-silverhand"));
    QCOMPARE(settings.terminalThemeId(), QStringLiteral("dracula"));
    QCOMPARE(settings.terminalLightThemeId(), QStringLiteral("xterm"));

    const QTime now = QTime::currentTime();
    settings.setDayStart(now.addSecs(-3600).toString(QStringLiteral("HH:mm")));
    settings.setNightStart(now.addSecs(3600).toString(QStringLiteral("HH:mm")));
    settings.setAppearanceMode(QStringLiteral("schedule"));
    QVERIFY(!settings.darkAppearance());
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("github-light"));
    settings.setDayStart(now.addSecs(3600).toString(QStringLiteral("HH:mm")));
    settings.setNightStart(now.addSecs(-3600).toString(QStringLiteral("HH:mm")));
    QVERIFY(settings.darkAppearance());
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("cyberpunk-silverhand"));

    auto* hints = QGuiApplication::styleHints();
    const auto originalScheme = hints->colorScheme();
    settings.setAppearanceMode(QStringLiteral("system"));
    for (const auto scheme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark, Qt::ColorScheme::Unknown}) {
        QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Q_ARG(Qt::ColorScheme, scheme)));
        QCOMPARE(settings.effectiveTerminalThemeId(), settings.effectiveUiThemeId());
    }
    QVERIFY(QMetaObject::invokeMethod(hints, "colorSchemeChanged", Q_ARG(Qt::ColorScheme, originalScheme)));
    settings.setAppearanceMode(QStringLiteral("dark"));
    AppSettings restarted;
    QVERIFY(!restarted.separateTerminalTheme());
    QCOMPARE(restarted.effectiveTerminalThemeId(), QStringLiteral("cyberpunk-silverhand"));
    restarted.setSeparateTerminalTheme(true);
    QCOMPARE(restarted.effectiveTerminalThemeId(), QStringLiteral("dracula"));
    restarted.restoreDefaults();
}

void AppSettingsTest::themeLinkageLoadsOlderProfilesAndRoundTrips() {
    AppSettings settings;
    settings.restoreDefaults();
    // Missing linkage preference in an older profile defaults to linked mode,
    // without discarding its previously independent terminal choices.
    QFile saved(settings.storagePath());
    QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray older = "version: 1\nkind: 8bitshell/settings\nappearance:\n"
        "  uiTheme: nord\n  terminalTheme: dracula\n  uiLightTheme: github-light\n"
        "  terminalLightTheme: xterm\n";
    QCOMPARE(saved.write(older), qint64(older.size()));
    saved.close();
    QVERIFY2(settings.reload(), qPrintable(settings.lastError()));
    QVERIFY(!settings.separateTerminalTheme());
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("nord"));
    settings.setSeparateTerminalTheme(true);
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("dracula"));
    const QString exported = m_configRoot.filePath(QStringLiteral("separate-themes.yaml"));
    QVERIFY(settings.exportYaml(exported));
    settings.restoreDefaults();
    QVERIFY(!settings.separateTerminalTheme());
    QVERIFY(settings.importYaml(exported));
    QVERIFY(settings.separateTerminalTheme());
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("dracula"));
    settings.setAppearanceMode(QStringLiteral("light"));
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("xterm"));

    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto before = saved.readAll();
    saved.close();
    QFile invalid(m_configRoot.filePath(QStringLiteral("invalid-theme-linkage.yaml")));
    QVERIFY(invalid.open(QIODevice::WriteOnly));
    invalid.write("version: 1\nkind: 8bitshell/settings\nappearance:\n"
                  "  separateTerminalTheme: \"false\"\n  uiTheme: monokai\n");
    invalid.close();
    QSignalSpy appearance(&settings, &AppSettings::appearanceChanged);
    QVERIFY(!settings.importYaml(invalid.fileName()));
    QCOMPARE(appearance.count(), 0);
    QVERIFY(settings.separateTerminalTheme());
    QCOMPARE(settings.uiThemeId(), QStringLiteral("nord"));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), before);
    saved.close();

    QVERIFY(invalid.open(QIODevice::WriteOnly | QIODevice::Truncate));
    invalid.write("version: 1\nkind: 8bitshell/settings\nappearance:\n  separateTerminalTheme: false\n");
    invalid.close();
    QVERIFY(settings.importYaml(invalid.fileName()));
    QVERIFY(!appearance.isEmpty());
    QVERIFY(!settings.separateTerminalTheme());
    QCOMPARE(settings.effectiveTerminalThemeId(), QStringLiteral("github-light"));
    QCOMPARE(settings.terminalThemeId(), QStringLiteral("dracula"));
    settings.restoreDefaults();
}

void AppSettingsTest::themeLinkageRejectsFailedSave() {
    AppSettings settings;
    settings.restoreDefaults();
    // Simulate another settings editor changing the file since it was loaded.
    // A rejected save must not change the displayed linkage state or its signals.
    QFile saved(settings.storagePath());
    QVERIFY(saved.open(QIODevice::Append));
    QVERIFY(saved.write("\n# External settings edit\n") > 0);
    saved.close();
    QSignalSpy appearance(&settings, &AppSettings::appearanceChanged);
    settings.setSeparateTerminalTheme(true);
    QVERIFY(!settings.lastError().isEmpty());
    QVERIFY(!settings.separateTerminalTheme());
    QCOMPARE(appearance.count(), 0);
    QCOMPARE(settings.effectiveTerminalThemeId(), settings.effectiveUiThemeId());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(saved.readAll().endsWith("# External settings edit\n"));
    saved.close();
    QVERIFY(settings.reload());
    settings.setSeparateTerminalTheme(true);
    QVERIFY(settings.separateTerminalTheme());
    settings.restoreDefaults();
}

void AppSettingsTest::renderingModesPersistAndRejectInvalid() {
    AppSettings settings;
    QCOMPARE(settings.terminalTextAntialiasing(), QStringLiteral("system"));
    QCOMPARE(settings.terminalFontHinting(), QStringLiteral("default"));
    for (const auto& smoothing : {QStringLiteral("system"), QStringLiteral("grayscale"), QStringLiteral("aliased")}) {
        for (const auto& hinting : {QStringLiteral("default"), QStringLiteral("none"), QStringLiteral("vertical"), QStringLiteral("full")}) {
            settings.setTerminalTextAntialiasing(smoothing);
            settings.setTerminalFontHinting(hinting);
            AppSettings loaded;
            QCOMPARE(loaded.terminalTextAntialiasing(), smoothing);
            QCOMPARE(loaded.terminalFontHinting(), hinting);
            QVERIFY(loaded.lastError().isEmpty());
        }
    }
    QFile original(settings.storagePath());
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto before = original.readAll();
    original.close();
    settings.setTerminalTextAntialiasing(QStringLiteral("unsafe-unknown"));
    QVERIFY(!settings.lastError().isEmpty());
    QCOMPARE(settings.terminalTextAntialiasing(), QStringLiteral("aliased"));
    settings.setTerminalFontHinting(QStringLiteral("invalid"));
    QCOMPARE(settings.terminalFontHinting(), QStringLiteral("full"));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), before);
    original.close();
    const auto exported = m_configRoot.filePath(QStringLiteral("rendering.yaml"));
    QVERIFY(settings.exportYaml(exported));
    settings.restoreDefaults();
    QCOMPARE(settings.terminalTextAntialiasing(), QStringLiteral("system"));
    QVERIFY(settings.importYaml(exported));
    QCOMPARE(settings.terminalTextAntialiasing(), QStringLiteral("aliased"));
    QCOMPARE(settings.terminalFontHinting(), QStringLiteral("full"));
    settings.restoreDefaults();
}

void AppSettingsTest::commandPaletteShortcutPersistsAndRejectsConflicts() {
    const QString action = QStringLiteral("commandPalette");
    const QString normal = QStringLiteral("Ctrl+Shift+P");
    const QString customized = QStringLiteral("Ctrl+Alt+P");
    AppSettings settings;
    settings.restoreDefaults();
    QCOMPARE(settings.keyBindings().value(action).toString(), normal);
    QVERIFY(settings.setKeyBinding(action, customized));
    QVERIFY(!settings.setKeyBinding(QStringLiteral("copy"), customized));
    QVERIFY(settings.shortcutError().contains(action));
    QCOMPARE(settings.keyBindings().value(QStringLiteral("copy")).toString(), QStringLiteral("Ctrl+Shift+C"));
    AppSettings restarted;
    QCOMPARE(restarted.keyBindings().value(action).toString(), customized);

    const QString exported = m_configRoot.filePath(QStringLiteral("palette-shortcut.yaml"));
    QVERIFY(settings.exportYaml(exported));
    settings.restoreDefaults();
    QCOMPARE(settings.keyBindings().value(action).toString(), normal);
    QVERIFY(settings.importYaml(exported));
    QCOMPARE(settings.keyBindings().value(action).toString(), customized);

    QFile partial(m_configRoot.filePath(QStringLiteral("palette-partial.yaml")));
    QVERIFY(partial.open(QIODevice::WriteOnly));
    partial.write("version: 1\nkind: 8bitshell/settings\nterminal:\n  fontSize: 17\n");
    partial.close();
    QVERIFY(settings.importYaml(partial.fileName()));
    QCOMPARE(settings.keyBindings().value(action).toString(), customized);

    QFile saved(settings.storagePath());
    QVERIFY(saved.open(QIODevice::ReadOnly));
    const auto before = saved.readAll();
    saved.close();
    QVERIFY(partial.open(QIODevice::WriteOnly | QIODevice::Truncate));
    partial.write("version: 1\nkind: 8bitshell/settings\nshortcuts:\n  commandPalette: Ctrl+Shift+C\n");
    partial.close();
    QVERIFY(!settings.importYaml(partial.fileName()));
    QCOMPARE(settings.keyBindings().value(action).toString(), customized);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), before);
    saved.close();

    QVERIFY(settings.setKeyBinding(action, QString{}));
    AppSettings disabled;
    QVERIFY(disabled.keyBindings().value(action).toString().isEmpty());
    settings.applyKeyPreset(QStringLiteral("insert"));
    QCOMPARE(settings.keyBindings().value(action).toString(), normal);
    QVERIFY(settings.setKeyBinding(action, customized));
    settings.applyKeyPreset(QStringLiteral("linux"));
    QCOMPARE(settings.keyBindings().value(action).toString(), normal);
    settings.restoreDefaults();
}

void AppSettingsTest::commandPaletteShortcutLoadsOlderProfiles() {
    AppSettings settings;
    settings.restoreDefaults();
    QFile saved(settings.storagePath());
    QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray old = "version: 1\nkind: 8bitshell/settings\nappearance:\n  uiTheme: nord\n"
        "shortcuts:\n  quickConnect: Ctrl+Alt+K\n";
    QCOMPARE(saved.write(old), qint64(old.size()));
    saved.close();
    QVERIFY2(settings.reload(), qPrintable(settings.lastError()));
    QCOMPARE(settings.keyBindings().value(QStringLiteral("commandPalette")).toString(), QStringLiteral("Ctrl+Shift+P"));
    QCOMPARE(settings.keyBindings().value(QStringLiteral("quickConnect")).toString(), QStringLiteral("Ctrl+Alt+K"));
    QCOMPARE(settings.uiThemeId(), QStringLiteral("nord"));

    // An existing customized shortcut owns its combination. Adding the palette
    // must not reject the whole profile or discard unrelated preferences.
    const QByteArray occupied = old + "  copy: Ctrl+Shift+P\n";
    QVERIFY(saved.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(saved.write(occupied), qint64(occupied.size()));
    saved.close();
    QVERIFY2(settings.reload(), qPrintable(settings.lastError()));
    QVERIFY(settings.keyBindings().value(QStringLiteral("commandPalette")).toString().isEmpty());
    QCOMPARE(settings.keyBindings().value(QStringLiteral("copy")).toString(), QStringLiteral("Ctrl+Shift+P"));
    QCOMPARE(settings.uiThemeId(), QStringLiteral("nord"));
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), occupied); // Reload does not rewrite the older file.
    saved.close();
    AppSettings restarted;
    QVERIFY2(restarted.lastError().isEmpty(), qPrintable(restarted.lastError()));
    QVERIFY(restarted.keyBindings().value(QStringLiteral("commandPalette")).toString().isEmpty());
    QVERIFY(restarted.setKeyBinding(QStringLiteral("commandPalette"), QStringLiteral("Ctrl+Alt+P")));
    QVERIFY(settings.reload());
    QCOMPARE(settings.keyBindings().value(QStringLiteral("commandPalette")).toString(), QStringLiteral("Ctrl+Alt+P"));

    QFile duplicate(m_configRoot.filePath(QStringLiteral("palette-explicit-conflict.yaml")));
    QVERIFY(duplicate.open(QIODevice::WriteOnly));
    duplicate.write("version: 1\nkind: 8bitshell/settings\nshortcuts:\n"
                    "  copy: Ctrl+Shift+P\n  commandPalette: Ctrl+Shift+P\n");
    duplicate.close();
    QVERIFY(!settings.importYaml(duplicate.fileName()));
    QCOMPARE(settings.keyBindings().value(QStringLiteral("commandPalette")).toString(), QStringLiteral("Ctrl+Alt+P"));
    settings.restoreDefaults();
}

void AppSettingsTest::commandPaletteShortcutMigratesLegacyCollision() {
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    const auto previous = qgetenv("BIT8SHELL_PROFILE");
    qputenv("BIT8SHELL_PROFILE", profile.path().toUtf8());
    const auto restore = qScopeGuard([previous] {
        if (previous.isNull()) qunsetenv("BIT8SHELL_PROFILE");
        else qputenv("BIT8SHELL_PROFILE", previous);
    });
    const auto iniPath = profile.filePath(QStringLiteral("settings.ini"));
    {
        QSettings legacy(iniPath, QSettings::IniFormat);
        legacy.setValue(QStringLiteral("appearance/uiTheme"), QStringLiteral("nord"));
        legacy.setValue(QStringLiteral("shortcuts/quickConnect"), QStringLiteral("Ctrl+Shift+P"));
        legacy.sync();
        QCOMPARE(legacy.status(), QSettings::NoError);
    }
    QFile original(iniPath);
    QVERIFY(original.open(QIODevice::ReadOnly));
    const auto before = original.readAll();
    original.close();
    AppSettings settings;
    QVERIFY2(settings.lastError().isEmpty(), qPrintable(settings.lastError()));
    QCOMPARE(settings.uiThemeId(), QStringLiteral("nord"));
    QCOMPARE(settings.keyBindings().value(QStringLiteral("quickConnect")).toString(), QStringLiteral("Ctrl+Shift+P"));
    QVERIFY(settings.keyBindings().value(QStringLiteral("commandPalette")).toString().isEmpty());
    QVERIFY(QFileInfo::exists(profile.filePath(QStringLiteral("settings.yaml"))));
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), before);
    AppSettings restarted;
    QVERIFY(restarted.lastError().isEmpty());
    QVERIFY(restarted.keyBindings().value(QStringLiteral("commandPalette")).toString().isEmpty());
}

QTEST_MAIN(AppSettingsTest)

#include "AppSettingsTest.moc"
