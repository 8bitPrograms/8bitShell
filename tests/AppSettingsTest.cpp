#include <QtTest>

#include <QCoreApplication>
#include <QTemporaryDir>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTime>

#include <bitshell/core/AppSettings.h>

using bitshell::core::AppSettings;

class AppSettingsTest final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void clampsAndPersistsSettings();
    void appearanceScheduleBoundaries();
    void appearanceModesAndPersistence();
    void renderingModesPersistAndRejectInvalid();

private:
    QTemporaryDir m_configRoot;
};

void AppSettingsTest::initTestCase() {
    QVERIFY(m_configRoot.isValid());
    qputenv("XDG_CONFIG_HOME", m_configRoot.path().toUtf8());
    QCoreApplication::setOrganizationName(QStringLiteral("bitshell-tests"));
    QCoreApplication::setApplicationName(QStringLiteral("app-settings-test"));
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
    }
    settings.restoreDefaults();
    QCOMPARE(settings.appearanceMode(), QStringLiteral("dark"));
    QCOMPARE(settings.uiLightThemeId(), QStringLiteral("catppuccin-latte"));
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

QTEST_MAIN(AppSettingsTest)

#include "AppSettingsTest.moc"
