#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <bitshell/core/SettingsFile.h>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace bitshell::core {

class AppSettings : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString applicationVersion READ applicationVersion CONSTANT)
    Q_PROPERTY(bool bonusThemesUnlocked READ bonusThemesUnlocked NOTIFY bonusThemesChanged)
    Q_PROPERTY(QString terminalFontFamily READ terminalFontFamily WRITE setTerminalFontFamily
                   NOTIFY terminalFontFamilyChanged)
    Q_PROPERTY(qreal terminalFontSize READ terminalFontSize WRITE setTerminalFontSize NOTIFY
                   terminalFontSizeChanged)
    Q_PROPERTY(QString terminalTextAntialiasing READ terminalTextAntialiasing WRITE setTerminalTextAntialiasing NOTIFY terminalRenderingChanged)
    Q_PROPERTY(QString terminalFontHinting READ terminalFontHinting WRITE setTerminalFontHinting NOTIFY terminalRenderingChanged)
    Q_PROPERTY(int scrollbackLines READ scrollbackLines WRITE setScrollbackLines NOTIFY
                   scrollbackLinesChanged)
    Q_PROPERTY(QString backspaceMode READ backspaceMode WRITE setBackspaceMode NOTIFY
                   backspaceModeChanged)
    Q_PROPERTY(QString uiThemeId READ uiThemeId WRITE setUiThemeId NOTIFY uiThemeIdChanged)
    Q_PROPERTY(QString terminalThemeId READ terminalThemeId WRITE setTerminalThemeId NOTIFY
                   terminalThemeIdChanged)
    Q_PROPERTY(QString uiLightThemeId READ uiLightThemeId WRITE setUiLightThemeId NOTIFY appearanceChanged)
    Q_PROPERTY(QString terminalLightThemeId READ terminalLightThemeId WRITE setTerminalLightThemeId NOTIFY appearanceChanged)
    Q_PROPERTY(bool separateTerminalTheme READ separateTerminalTheme WRITE setSeparateTerminalTheme NOTIFY appearanceChanged)
    Q_PROPERTY(QString appearanceMode READ appearanceMode WRITE setAppearanceMode NOTIFY appearanceChanged)
    Q_PROPERTY(QString dayStart READ dayStart WRITE setDayStart NOTIFY appearanceChanged)
    Q_PROPERTY(QString nightStart READ nightStart WRITE setNightStart NOTIFY appearanceChanged)
    Q_PROPERTY(bool darkAppearance READ darkAppearance NOTIFY appearanceChanged)
    Q_PROPERTY(bool systemThemeAvailable READ systemThemeAvailable NOTIFY appearanceChanged)
    Q_PROPERTY(QString effectiveUiThemeId READ effectiveUiThemeId NOTIFY appearanceChanged)
    Q_PROPERTY(QString effectiveTerminalThemeId READ effectiveTerminalThemeId NOTIFY appearanceChanged)
    Q_PROPERTY(bool logByDefault READ logByDefault WRITE setLogByDefault NOTIFY
                   logByDefaultChanged)
    Q_PROPERTY(bool highlightByDefault READ highlightByDefault WRITE setHighlightByDefault
                   NOTIFY highlightByDefaultChanged)
    Q_PROPERTY(QString logDirectory READ logDirectory WRITE setLogDirectory NOTIFY
                   logDirectoryChanged)
    Q_PROPERTY(QString logDirectoryTemplate READ logDirectoryTemplate WRITE
                   setLogDirectoryTemplate NOTIFY logDirectoryTemplateChanged)
    Q_PROPERTY(QString logFileNameTemplate READ logFileNameTemplate WRITE
                   setLogFileNameTemplate NOTIFY logFileNameTemplateChanged)
    Q_PROPERTY(bool logTimestamps READ logTimestamps WRITE setLogTimestamps NOTIFY
                   logTimestampsChanged)
    Q_PROPERTY(bool logAppend READ logAppend WRITE setLogAppend NOTIFY logAppendChanged)
    Q_PROPERTY(int logRotationMegabytes READ logRotationMegabytes WRITE
                   setLogRotationMegabytes NOTIFY logRotationMegabytesChanged)
    Q_PROPERTY(int logRetainedFiles READ logRetainedFiles WRITE setLogRetainedFiles NOTIFY
                   logRetainedFilesChanged)
    Q_PROPERTY(QStringList expandedSessionFolders READ expandedSessionFolders WRITE
                   setExpandedSessionFolders NOTIFY expandedSessionFoldersChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY storageErrorChanged)
    Q_PROPERTY(bool copyOnSelect READ copyOnSelect WRITE setCopyOnSelect NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool monitoringByDefault READ monitoringByDefault WRITE setMonitoringByDefault NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool sidebarVisible READ sidebarVisible WRITE setSidebarVisible NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool fileBrowserVisible READ fileBrowserVisible WRITE setFileBrowserVisible NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool macroBarVisible READ macroBarVisible WRITE setMacroBarVisible NOTIFY inputSettingsChanged)
    Q_PROPERTY(QVariantMap keyBindings READ keyBindings NOTIFY inputSettingsChanged)
    Q_PROPERTY(QString shortcutError READ shortcutError NOTIFY inputSettingsChanged)

public:
    explicit AppSettings(QObject* parent = nullptr);
    QString applicationVersion() const;
    bool bonusThemesUnlocked() const;
    Q_INVOKABLE bool registerVersionTap();

    [[nodiscard]] QString terminalFontFamily() const;
    void setTerminalFontFamily(const QString& value);
    [[nodiscard]] qreal terminalFontSize() const;
    void setTerminalFontSize(qreal value);
    QString terminalTextAntialiasing() const { return m_terminalTextAntialiasing; }
    void setTerminalTextAntialiasing(const QString& value);
    QString terminalFontHinting() const { return m_terminalFontHinting; }
    void setTerminalFontHinting(const QString& value);
    [[nodiscard]] int scrollbackLines() const;
    void setScrollbackLines(int value);
    [[nodiscard]] QString backspaceMode() const;
    void setBackspaceMode(const QString& value);
    [[nodiscard]] QString uiThemeId() const;
    void setUiThemeId(const QString& value);
    [[nodiscard]] QString terminalThemeId() const;
    void setTerminalThemeId(const QString& value);
    [[nodiscard]] QString uiLightThemeId() const;
    void setUiLightThemeId(const QString& value);
    [[nodiscard]] QString terminalLightThemeId() const;
    void setTerminalLightThemeId(const QString& value);
    [[nodiscard]] bool separateTerminalTheme() const;
    void setSeparateTerminalTheme(bool value);
    [[nodiscard]] QString appearanceMode() const;
    void setAppearanceMode(const QString& value);
    [[nodiscard]] QString dayStart() const;
    void setDayStart(const QString& value);
    [[nodiscard]] QString nightStart() const;
    void setNightStart(const QString& value);
    [[nodiscard]] bool darkAppearance() const;
    [[nodiscard]] bool systemThemeAvailable() const;
    [[nodiscard]] QString effectiveUiThemeId() const;
    [[nodiscard]] QString effectiveTerminalThemeId() const;
    [[nodiscard]] static bool isDayAt(int minute, int dayStart, int nightStart);
    Q_INVOKABLE void toggleAppearance();
    [[nodiscard]] bool logByDefault() const;
    void setLogByDefault(bool value);
    [[nodiscard]] bool highlightByDefault() const;
    void setHighlightByDefault(bool value);
    [[nodiscard]] QString logDirectory() const;
    void setLogDirectory(const QString& value);
    [[nodiscard]] QString logDirectoryTemplate() const;
    void setLogDirectoryTemplate(const QString& value);
    [[nodiscard]] QString logFileNameTemplate() const;
    void setLogFileNameTemplate(const QString& value);
    [[nodiscard]] bool logTimestamps() const;
    void setLogTimestamps(bool value);
    [[nodiscard]] bool logAppend() const;
    void setLogAppend(bool value);
    [[nodiscard]] int logRotationMegabytes() const;
    void setLogRotationMegabytes(int value);
    [[nodiscard]] int logRetainedFiles() const;
    void setLogRetainedFiles(int value);
    [[nodiscard]] QStringList expandedSessionFolders() const;
    void setExpandedSessionFolders(const QStringList& value);
    [[nodiscard]] QString storagePath() const;

    Q_INVOKABLE void restoreDefaults();
    QString lastError() const { return m_settings.error(); }
    Q_INVOKABLE bool reload();
    Q_INVOKABLE bool importYaml(const QString& source);
    Q_INVOKABLE bool exportYaml(const QString& destination);
    bool copyOnSelect() const { return m_copyOnSelect; }
    void setCopyOnSelect(bool value);
    bool monitoringByDefault() const { return m_monitoringByDefault; }
    void setMonitoringByDefault(bool value);
    bool sidebarVisible() const { return m_sidebarVisible; }
    void setSidebarVisible(bool value);
    bool fileBrowserVisible() const { return m_fileBrowserVisible; }
    void setFileBrowserVisible(bool value);
    bool macroBarVisible() const { return m_macroBarVisible; }
    void setMacroBarVisible(bool value);
    QVariantMap keyBindings() const { return m_keyBindings; }
    QString shortcutError() const { return m_shortcutError; }
    static QVariantMap defaultKeyBindings();
    Q_INVOKABLE bool setKeyBinding(const QString& action, const QString& sequence);
    Q_INVOKABLE void applyKeyPreset(const QString& preset);
    Q_INVOKABLE QString shortcutFromKey(int key, int modifiers) const;

signals:
    void bonusThemesChanged();
    void storageErrorChanged();
    void inputSettingsChanged();
    void appearanceChanged();
    void terminalFontFamilyChanged();
    void terminalFontSizeChanged();
    void terminalRenderingChanged();
    void scrollbackLinesChanged();
    void backspaceModeChanged();
    void uiThemeIdChanged();
    void terminalThemeIdChanged();
    void logByDefaultChanged();
    void highlightByDefaultChanged();
    void logDirectoryChanged();
    void logDirectoryTemplateChanged();
    void logFileNameTemplateChanged();
    void logTimestampsChanged();
    void logAppendChanged();
    void logRotationMegabytesChanged();
    void logRetainedFilesChanged();
    void expandedSessionFoldersChanged();

private:
    static QVariantMap defaultValues();
    void applyValues();
    bool storeValue(const QString& key, const QVariant& value);
    void refreshAppearance();
    [[nodiscard]] static QString defaultFontFamily();
    [[nodiscard]] static QString defaultLogDirectory();

    SettingsFile m_settings;
    QString m_terminalFontFamily;
    qreal m_terminalFontSize = 13.0;
    QString m_terminalTextAntialiasing = QStringLiteral("system");
    QString m_terminalFontHinting = QStringLiteral("default");
    int m_scrollbackLines = 10000;
    QString m_backspaceMode = QStringLiteral("del");
    QString m_uiThemeId = QStringLiteral("catppuccin-mocha");
    QString m_terminalThemeId = QStringLiteral("xterm");
    QString m_uiLightThemeId = QStringLiteral("catppuccin-latte");
    QString m_terminalLightThemeId = QStringLiteral("xterm");
    bool m_separateTerminalTheme = false;
    QString m_appearanceMode = QStringLiteral("dark");
    QString m_dayStart = QStringLiteral("07:00");
    QString m_nightStart = QStringLiteral("19:00");
    bool m_darkAppearance = true;
    bool m_systemThemeAvailable = false;
    Qt::ColorScheme m_systemColorScheme = Qt::ColorScheme::Unknown;
    QTimer m_appearanceTimer;
    bool m_logByDefault = true;
    bool m_highlightByDefault = true;
    QString m_logDirectory;
    QString m_logDirectoryTemplate = QStringLiteral("{session}");
    QString m_logFileNameTemplate = QStringLiteral("{session}_{date}_{time}.log");
    bool m_logTimestamps = false;
    bool m_logAppend = true;
    int m_logRotationMegabytes = 25;
    int m_logRetainedFiles = 5;
    QStringList m_expandedSessionFolders;
    bool m_copyOnSelect = false;
    bool m_monitoringByDefault = false;
    bool m_sidebarVisible = true;
    bool m_fileBrowserVisible = false;
    bool m_macroBarVisible = true;
    QVariantMap m_keyBindings;
    QString m_shortcutError;
    QElapsedTimer m_versionTapTimer;
    int m_versionTapCount = 0;
};

} // namespace bitshell::core
