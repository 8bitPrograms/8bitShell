#include <bitshell/core/AppSettings.h>

#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QStyleHints>
#include <QTime>
#include <QKeySequence>

#include <algorithm>

namespace bitshell::core {

QVariantMap AppSettings::defaultValues() {
    QVariantMap values{
        {QStringLiteral("terminal/fontFamily"), defaultFontFamily()},
        {QStringLiteral("terminal/fontSize"), 13.0},
        {QStringLiteral("terminal/textAntialiasing"), QStringLiteral("system")},
        {QStringLiteral("terminal/fontHinting"), QStringLiteral("default")},
        {QStringLiteral("terminal/scrollbackLines"), 10000},
        {QStringLiteral("terminal/backspaceMode"), QStringLiteral("del")},
        {QStringLiteral("terminal/copyOnSelect"), false},
        {QStringLiteral("appearance/uiTheme"), QStringLiteral("catppuccin-mocha")},
        {QStringLiteral("appearance/bonusThemesUnlocked"), false},
        {QStringLiteral("appearance/terminalTheme"), QStringLiteral("xterm")},
        {QStringLiteral("appearance/uiLightTheme"), QStringLiteral("catppuccin-latte")},
        {QStringLiteral("appearance/terminalLightTheme"), QStringLiteral("xterm")},
        {QStringLiteral("appearance/separateTerminalTheme"), false},
        {QStringLiteral("appearance/mode"), QStringLiteral("dark")},
        {QStringLiteral("appearance/dayStart"), QStringLiteral("07:00")},
        {QStringLiteral("appearance/nightStart"), QStringLiteral("19:00")},
        {QStringLiteral("logging/enabledByDefault"), true},
        {QStringLiteral("highlighting/enabledByDefault"), true},
        {QStringLiteral("logging/directory"), defaultLogDirectory()},
        {QStringLiteral("logging/directoryTemplate"), QStringLiteral("{session}")},
        {QStringLiteral("logging/fileNameTemplate"), QStringLiteral("{session}_{date}_{time}.log")},
        {QStringLiteral("logging/lineTimestamps"), false},
        {QStringLiteral("logging/append"), true},
        {QStringLiteral("logging/rotationMegabytes"), 25},
        {QStringLiteral("logging/retainedFiles"), 5},
        {QStringLiteral("navigation/expandedFolders"), QStringList{}},
        {QStringLiteral("navigation/sidebarVisible"), true},
        {QStringLiteral("navigation/fileBrowserVisible"), false},
        {QStringLiteral("monitoring/enabledByDefault"), false},
        {QStringLiteral("macros/barVisible"), true}
    };
    const auto keys = defaultKeyBindings();
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        values[QStringLiteral("shortcuts/") + it.key()] = it.value();
    return values;
}

AppSettings::AppSettings(QObject* parent)
    : QObject(parent), m_settings(defaultValues()) {
    m_systemColorScheme = QGuiApplication::styleHints()->colorScheme();
    applyValues();
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
            this, [this](Qt::ColorScheme scheme) {
                m_systemColorScheme = scheme;
                refreshAppearance();
            });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged,
            this, [this] { refreshAppearance(); });
    m_appearanceTimer.setInterval(1000);
    connect(&m_appearanceTimer, &QTimer::timeout, this, &AppSettings::refreshAppearance);
}

void AppSettings::applyValues() {
    emit bonusThemesChanged();
    m_terminalFontFamily = m_settings.value(QStringLiteral("terminal/fontFamily")).toString();
    m_terminalFontSize = m_settings.value(QStringLiteral("terminal/fontSize")).toDouble();
    m_terminalTextAntialiasing = m_settings.value(QStringLiteral("terminal/textAntialiasing")).toString();
    m_terminalFontHinting = m_settings.value(QStringLiteral("terminal/fontHinting")).toString();
    m_scrollbackLines = m_settings.value(QStringLiteral("terminal/scrollbackLines")).toInt();
    m_backspaceMode = m_settings.value(QStringLiteral("terminal/backspaceMode")).toString();
    m_copyOnSelect = m_settings.value(QStringLiteral("terminal/copyOnSelect")).toBool();
    m_uiThemeId = m_settings.value(QStringLiteral("appearance/uiTheme")).toString();
    m_terminalThemeId = m_settings.value(QStringLiteral("appearance/terminalTheme")).toString();
    m_uiLightThemeId = m_settings.value(QStringLiteral("appearance/uiLightTheme")).toString();
    m_terminalLightThemeId = m_settings.value(QStringLiteral("appearance/terminalLightTheme")).toString();
    m_separateTerminalTheme = m_settings.value(QStringLiteral("appearance/separateTerminalTheme")).toBool();
    m_appearanceMode = m_settings.value(QStringLiteral("appearance/mode")).toString();
    m_dayStart = m_settings.value(QStringLiteral("appearance/dayStart")).toString();
    m_nightStart = m_settings.value(QStringLiteral("appearance/nightStart")).toString();
    m_logByDefault = m_settings.value(QStringLiteral("logging/enabledByDefault")).toBool();
    m_highlightByDefault = m_settings.value(QStringLiteral("highlighting/enabledByDefault")).toBool();
    m_logDirectory = m_settings.value(QStringLiteral("logging/directory")).toString();
    m_logDirectoryTemplate = m_settings.value(QStringLiteral("logging/directoryTemplate")).toString();
    m_logFileNameTemplate = m_settings.value(QStringLiteral("logging/fileNameTemplate")).toString();
    m_logTimestamps = m_settings.value(QStringLiteral("logging/lineTimestamps")).toBool();
    m_logAppend = m_settings.value(QStringLiteral("logging/append")).toBool();
    m_logRotationMegabytes = m_settings.value(QStringLiteral("logging/rotationMegabytes")).toInt();
    m_logRetainedFiles = m_settings.value(QStringLiteral("logging/retainedFiles")).toInt();
    m_expandedSessionFolders = m_settings.value(QStringLiteral("navigation/expandedFolders")).toStringList();
    m_sidebarVisible = m_settings.value(QStringLiteral("navigation/sidebarVisible")).toBool();
    m_fileBrowserVisible = m_settings.value(QStringLiteral("navigation/fileBrowserVisible")).toBool();
    m_monitoringByDefault = m_settings.value(QStringLiteral("monitoring/enabledByDefault")).toBool();
    m_macroBarVisible = m_settings.value(QStringLiteral("macros/barVisible")).toBool();
    m_keyBindings = defaultKeyBindings();
    for (auto it = m_keyBindings.begin(); it != m_keyBindings.end(); ++it)
        it.value() = m_settings.value(QStringLiteral("shortcuts/") + it.key());
    if (m_appearanceMode == QStringLiteral("schedule")) m_appearanceTimer.start(1000);
    else m_appearanceTimer.stop();
    refreshAppearance();
    emit terminalFontFamilyChanged();
    emit terminalFontSizeChanged();
    emit terminalRenderingChanged();
    emit scrollbackLinesChanged();
    emit backspaceModeChanged();
    emit inputSettingsChanged();
    emit uiThemeIdChanged();
    emit terminalThemeIdChanged();
    emit appearanceChanged();
    emit logByDefaultChanged();
    emit highlightByDefaultChanged();
    emit logDirectoryChanged();
    emit logDirectoryTemplateChanged();
    emit logFileNameTemplateChanged();
    emit logTimestampsChanged();
    emit logAppendChanged();
    emit logRotationMegabytesChanged();
    emit logRetainedFilesChanged();
    emit expandedSessionFoldersChanged();
}

QString AppSettings::applicationVersion() const { return QCoreApplication::applicationVersion(); }
bool AppSettings::bonusThemesUnlocked() const {
    return m_settings.value(QStringLiteral("appearance/bonusThemesUnlocked")).toBool();
}
bool AppSettings::registerVersionTap() {
    if (bonusThemesUnlocked()) return false;
    if (!m_versionTapTimer.isValid() || m_versionTapTimer.elapsed() > 1500) m_versionTapCount = 0;
    m_versionTapTimer.start();
    if (++m_versionTapCount < 7) return false;
    m_versionTapCount = 0;
    if (!storeValue(QStringLiteral("appearance/bonusThemesUnlocked"), true)) return false;
    emit bonusThemesChanged();
    return true;
}

bool AppSettings::storeValue(const QString& key, const QVariant& value) {
    const bool ok = m_settings.setValue(key, value);
    emit storageErrorChanged();
    return ok;
}
bool AppSettings::reload() {
    const bool ok = m_settings.reload();
    if (ok) applyValues();
    emit storageErrorChanged();
    return ok;
}
bool AppSettings::importYaml(const QString& source) {
    const bool ok = m_settings.importYaml(source);
    if (ok) applyValues();
    emit storageErrorChanged();
    return ok;
}
bool AppSettings::exportYaml(const QString& destination) {
    const bool ok = m_settings.exportYaml(destination);
    emit storageErrorChanged();
    return ok;
}

QString AppSettings::terminalFontFamily() const {
    return m_terminalFontFamily;
}

void AppSettings::setTerminalFontFamily(const QString& value) {
    const QString normalized = value.trimmed().isEmpty() ? defaultFontFamily() : value.trimmed();
    if (m_terminalFontFamily == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("terminal/fontFamily"), normalized)) return;
    m_terminalFontFamily = normalized;
    emit terminalFontFamilyChanged();
}

qreal AppSettings::terminalFontSize() const {
    return m_terminalFontSize;
}

void AppSettings::setTerminalFontSize(qreal value) {
    const qreal bounded = std::clamp(value, 7.0, 40.0);
    if (qFuzzyCompare(m_terminalFontSize, bounded)) {
        return;
    }
    if (!storeValue(QStringLiteral("terminal/fontSize"), bounded)) return;
    m_terminalFontSize = bounded;
    emit terminalFontSizeChanged();
}

void AppSettings::setTerminalTextAntialiasing(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == m_terminalTextAntialiasing) return;
    if (!storeValue(QStringLiteral("terminal/textAntialiasing"), normalized)) return;
    m_terminalTextAntialiasing = normalized;
    emit terminalRenderingChanged();
}

void AppSettings::setTerminalFontHinting(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == m_terminalFontHinting) return;
    if (!storeValue(QStringLiteral("terminal/fontHinting"), normalized)) return;
    m_terminalFontHinting = normalized;
    emit terminalRenderingChanged();
}

int AppSettings::scrollbackLines() const {
    return m_scrollbackLines;
}

void AppSettings::setScrollbackLines(int value) {
    const int bounded = std::clamp(value, 100, 100000);
    if (m_scrollbackLines == bounded) {
        return;
    }
    if (!storeValue(QStringLiteral("terminal/scrollbackLines"), bounded)) return;
    m_scrollbackLines = bounded;
    emit scrollbackLinesChanged();
}

QString AppSettings::backspaceMode() const {
    return m_backspaceMode;
}

void AppSettings::setBackspaceMode(const QString& value) {
    const QString normalized = value.trimmed().toLower() == QStringLiteral("bs")
                                   ? QStringLiteral("bs")
                                   : QStringLiteral("del");
    if (m_backspaceMode == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("terminal/backspaceMode"), normalized)) return;
    m_backspaceMode = normalized;
    emit backspaceModeChanged();
}

QString AppSettings::uiThemeId() const {
    return m_uiThemeId;
}

void AppSettings::setUiThemeId(const QString& value) {
    const QString normalized = value.trimmed().isEmpty()
                                   ? QStringLiteral("catppuccin-mocha")
                                   : value.trimmed().toLower();
    if (m_uiThemeId == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("appearance/uiTheme"), normalized)) return;
    m_uiThemeId = normalized;
    emit uiThemeIdChanged();
    emit appearanceChanged();
}

QString AppSettings::terminalThemeId() const {
    return m_terminalThemeId;
}

void AppSettings::setTerminalThemeId(const QString& value) {
    const QString normalized = value.trimmed().isEmpty()
                                   ? QStringLiteral("xterm")
                                   : value.trimmed().toLower();
    if (m_terminalThemeId == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("appearance/terminalTheme"), normalized)) return;
    m_terminalThemeId = normalized;
    emit terminalThemeIdChanged();
    emit appearanceChanged();
}

QString AppSettings::uiLightThemeId() const { return m_uiLightThemeId; }
QString AppSettings::terminalLightThemeId() const { return m_terminalLightThemeId; }
bool AppSettings::separateTerminalTheme() const { return m_separateTerminalTheme; }
QString AppSettings::appearanceMode() const { return m_appearanceMode; }
QString AppSettings::dayStart() const { return m_dayStart; }
QString AppSettings::nightStart() const { return m_nightStart; }
bool AppSettings::darkAppearance() const { return m_darkAppearance; }
bool AppSettings::systemThemeAvailable() const { return m_systemThemeAvailable; }
QString AppSettings::effectiveUiThemeId() const {
    return m_darkAppearance ? m_uiThemeId : m_uiLightThemeId;
}
QString AppSettings::effectiveTerminalThemeId() const {
    // Keep independent terminal choices stored so switching back to separate
    // themes restores them. Linked mode follows the active UI slot directly.
    if (!m_separateTerminalTheme) return effectiveUiThemeId();
    return m_darkAppearance ? m_terminalThemeId : m_terminalLightThemeId;
}

void AppSettings::setSeparateTerminalTheme(bool value) {
    if (value == m_separateTerminalTheme) return;
    if (!storeValue(QStringLiteral("appearance/separateTerminalTheme"), value)) return;
    m_separateTerminalTheme = value;
    emit appearanceChanged();
}

void AppSettings::setUiLightThemeId(const QString& value) {
    const QString id = value.trimmed().isEmpty() ? QStringLiteral("catppuccin-latte")
                                               : value.trimmed().toLower();
    if (id == m_uiLightThemeId) return;
    if (!storeValue(QStringLiteral("appearance/uiLightTheme"), id)) return;
    m_uiLightThemeId = id;
    emit appearanceChanged();
}

void AppSettings::setTerminalLightThemeId(const QString& value) {
    const QString id = value.trimmed().isEmpty() ? QStringLiteral("xterm")
                                               : value.trimmed().toLower();
    if (id == m_terminalLightThemeId) return;
    if (!storeValue(QStringLiteral("appearance/terminalLightTheme"), id)) return;
    m_terminalLightThemeId = id;
    emit appearanceChanged();
}

void AppSettings::setAppearanceMode(const QString& value) {
    const QString mode = value.trimmed().toLower();
    if (mode != QStringLiteral("dark") && mode != QStringLiteral("light")
        && mode != QStringLiteral("system") && mode != QStringLiteral("schedule")) return;
    if (mode == m_appearanceMode) return;
    if (!storeValue(QStringLiteral("appearance/mode"), mode)) return;
    m_appearanceMode = mode;
    if (mode == QStringLiteral("schedule")) m_appearanceTimer.start(1000);
    else m_appearanceTimer.stop();
    refreshAppearance();
    emit appearanceChanged();
}

void AppSettings::setDayStart(const QString& value) {
    const QTime time = QTime::fromString(value, QStringLiteral("HH:mm"));
    if (!time.isValid() || value == m_dayStart) return;
    if (!storeValue(QStringLiteral("appearance/dayStart"), time.toString(QStringLiteral("HH:mm")))) return;
    m_dayStart = time.toString(QStringLiteral("HH:mm"));
    refreshAppearance();
    emit appearanceChanged();
}

void AppSettings::setNightStart(const QString& value) {
    const QTime time = QTime::fromString(value, QStringLiteral("HH:mm"));
    if (!time.isValid() || value == m_nightStart) return;
    if (!storeValue(QStringLiteral("appearance/nightStart"), time.toString(QStringLiteral("HH:mm")))) return;
    m_nightStart = time.toString(QStringLiteral("HH:mm"));
    refreshAppearance();
    emit appearanceChanged();
}

bool AppSettings::isDayAt(int minute, int dayStart, int nightStart) {
    if (dayStart == nightStart) return false; // Equal boundaries mean all-night.
    return dayStart < nightStart ? minute >= dayStart && minute < nightStart
                                : minute >= dayStart || minute < nightStart;
}

void AppSettings::refreshAppearance() {
    const auto scheme = m_systemColorScheme;
    const bool available = scheme != Qt::ColorScheme::Unknown;
    bool dark = m_appearanceMode != QStringLiteral("light");
    if (m_appearanceMode == QStringLiteral("system")) {
        // A platform without a color preference keeps the original dark appearance.
        dark = scheme != Qt::ColorScheme::Light;
    } else if (m_appearanceMode == QStringLiteral("schedule")) {
        const QTime now = QTime::currentTime();
        const QTime day = QTime::fromString(m_dayStart, QStringLiteral("HH:mm"));
        const QTime night = QTime::fromString(m_nightStart, QStringLiteral("HH:mm"));
        dark = !isDayAt(now.hour() * 60 + now.minute(), day.hour() * 60 + day.minute(),
                       night.hour() * 60 + night.minute());
    }
    if (dark != m_darkAppearance || available != m_systemThemeAvailable) {
        m_darkAppearance = dark;
        m_systemThemeAvailable = available;
        emit appearanceChanged();
    }
}

void AppSettings::toggleAppearance() {
    setAppearanceMode(m_darkAppearance ? QStringLiteral("light") : QStringLiteral("dark"));
}

bool AppSettings::logByDefault() const {
    return m_logByDefault;
}

void AppSettings::setLogByDefault(bool value) {
    if (m_logByDefault == value) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/enabledByDefault"), value)) return;
    m_logByDefault = value;
    emit logByDefaultChanged();
}

bool AppSettings::highlightByDefault() const {
    return m_highlightByDefault;
}

void AppSettings::setHighlightByDefault(bool value) {
    if (m_highlightByDefault == value) {
        return;
    }
    if (!storeValue(QStringLiteral("highlighting/enabledByDefault"), value)) return;
    m_highlightByDefault = value;
    emit highlightByDefaultChanged();
}

QString AppSettings::logDirectory() const {
    return m_logDirectory;
}

void AppSettings::setLogDirectory(const QString& value) {
    const QString normalized = value.trimmed().isEmpty() ? defaultLogDirectory()
                                                          : QDir::cleanPath(value.trimmed());
    if (m_logDirectory == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/directory"), normalized)) return;
    m_logDirectory = normalized;
    emit logDirectoryChanged();
}

QString AppSettings::logDirectoryTemplate() const {
    return m_logDirectoryTemplate;
}

void AppSettings::setLogDirectoryTemplate(const QString& value) {
    const QString normalized = value.trimmed().isEmpty() ? QStringLiteral("{session}")
                                                          : value.trimmed();
    if (m_logDirectoryTemplate == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/directoryTemplate"), normalized)) return;
    m_logDirectoryTemplate = normalized;
    emit logDirectoryTemplateChanged();
}

QString AppSettings::logFileNameTemplate() const {
    return m_logFileNameTemplate;
}

void AppSettings::setLogFileNameTemplate(const QString& value) {
    const QString normalized = value.trimmed().isEmpty()
                                   ? QStringLiteral("{session}_{date}_{time}.log")
                                   : value.trimmed();
    if (m_logFileNameTemplate == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/fileNameTemplate"), normalized)) return;
    m_logFileNameTemplate = normalized;
    emit logFileNameTemplateChanged();
}

bool AppSettings::logTimestamps() const {
    return m_logTimestamps;
}

void AppSettings::setLogTimestamps(bool value) {
    if (m_logTimestamps == value) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/lineTimestamps"), value)) return;
    m_logTimestamps = value;
    emit logTimestampsChanged();
}

bool AppSettings::logAppend() const {
    return m_logAppend;
}

void AppSettings::setLogAppend(bool value) {
    if (m_logAppend == value) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/append"), value)) return;
    m_logAppend = value;
    emit logAppendChanged();
}

int AppSettings::logRotationMegabytes() const {
    return m_logRotationMegabytes;
}

void AppSettings::setLogRotationMegabytes(int value) {
    const int bounded = std::clamp(value, 0, 4096);
    if (m_logRotationMegabytes == bounded) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/rotationMegabytes"), bounded)) return;
    m_logRotationMegabytes = bounded;
    emit logRotationMegabytesChanged();
}

int AppSettings::logRetainedFiles() const {
    return m_logRetainedFiles;
}

QStringList AppSettings::expandedSessionFolders() const {
    return m_expandedSessionFolders;
}

void AppSettings::setExpandedSessionFolders(const QStringList& value) {
    QStringList normalized = value;
    normalized.removeDuplicates();
    if (m_expandedSessionFolders == normalized) {
        return;
    }
    if (!storeValue(QStringLiteral("navigation/expandedFolders"), normalized)) return;
    m_expandedSessionFolders = std::move(normalized);
    emit expandedSessionFoldersChanged();
}

void AppSettings::setLogRetainedFiles(int value) {
    const int bounded = std::clamp(value, 0, 50);
    if (m_logRetainedFiles == bounded) {
        return;
    }
    if (!storeValue(QStringLiteral("logging/retainedFiles"), bounded)) return;
    m_logRetainedFiles = bounded;
    emit logRetainedFilesChanged();
}

QString AppSettings::storagePath() const {
    return m_settings.fileName();
}

void AppSettings::restoreDefaults() {
    auto values = defaultValues();
    values[QStringLiteral("navigation/expandedFolders")] = m_expandedSessionFolders;
    if (m_settings.replace(values)) applyValues();
    emit storageErrorChanged();
}

QVariantMap AppSettings::defaultKeyBindings() {
    return {
        {QStringLiteral("copy"), QStringLiteral("Ctrl+Shift+C")},
        {QStringLiteral("paste"), QStringLiteral("Ctrl+Shift+V")},
        {QStringLiteral("pasteSelection"), QStringLiteral("Ctrl+Shift+Insert")},
        {QStringLiteral("find"), QStringLiteral("Ctrl+F")},
        {QStringLiteral("newTab"), QStringLiteral("Ctrl+Shift+T")},
        {QStringLiteral("closeTab"), QStringLiteral("Ctrl+Shift+W")},
        {QStringLiteral("nextTab"), QStringLiteral("Ctrl+Tab")},
        {QStringLiteral("quickConnect"), QStringLiteral("Ctrl+K")},
        {QStringLiteral("commandPalette"), QStringLiteral("Ctrl+Shift+P")},
        {QStringLiteral("zoomIn"), QStringLiteral("Ctrl++")},
        {QStringLiteral("zoomOut"), QStringLiteral("Ctrl+-")}
    };
}
bool AppSettings::setKeyBinding(const QString& action, const QString& sequence) {
    if (!defaultKeyBindings().contains(action)) return false;
    const QKeySequence key = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    const QString normalized = key.toString(QKeySequence::PortableText);
    if (!sequence.isEmpty() && (key.count() != 1 || key[0].key() == Qt::Key_unknown)) {
        m_shortcutError = QStringLiteral("Use one key combination, such as Ctrl+Shift+C.");
        emit inputSettingsChanged(); return false;
    }
    for (auto it = m_keyBindings.cbegin(); it != m_keyBindings.cend(); ++it) {
        if (it.key() != action && !normalized.isEmpty()
            && QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText) == key) {
            m_shortcutError = QStringLiteral("Already assigned to %1. Clear that binding first.").arg(it.key());
            emit inputSettingsChanged(); return false;
        }
    }
    if (!storeValue(QStringLiteral("shortcuts/") + action, normalized)) {
        m_shortcutError = m_settings.error(); emit inputSettingsChanged(); return false;
    }
    m_keyBindings[action] = normalized;
    m_shortcutError.clear();
    emit inputSettingsChanged(); return true;
}
void AppSettings::applyKeyPreset(const QString& preset) {
    auto keys = defaultKeyBindings();
    if (preset == QStringLiteral("insert")) {
        keys[QStringLiteral("copy")] = QStringLiteral("Ctrl+Insert");
        keys[QStringLiteral("paste")] = QStringLiteral("Shift+Insert");
    }
    auto values = m_settings.values();
    for (auto it = keys.cbegin(); it != keys.cend(); ++it)
        values[QStringLiteral("shortcuts/") + it.key()] = it.value();
    const bool ok = m_settings.replace(values);
    emit storageErrorChanged();
    if (!ok) return;
    m_keyBindings = keys;
    m_shortcutError.clear();
    emit inputSettingsChanged();
}
QString AppSettings::shortcutFromKey(int key, int modifiers) const {
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta)
        return {};
    return QKeySequence(QKeyCombination(Qt::KeyboardModifiers(modifiers), Qt::Key(key)))
        .toString(QKeySequence::PortableText);
}

void AppSettings::setCopyOnSelect(bool value) {
    if (m_copyOnSelect == value) return;
    if (!storeValue(QStringLiteral("terminal/copyOnSelect"), value)) return;
    m_copyOnSelect = value;
    emit inputSettingsChanged();
}

void AppSettings::setMonitoringByDefault(bool value) {
    if (m_monitoringByDefault == value) return;
    if (!storeValue(QStringLiteral("monitoring/enabledByDefault"), value)) return;
    m_monitoringByDefault = value;
    emit inputSettingsChanged();
}

void AppSettings::setSidebarVisible(bool value) {
    if (m_sidebarVisible == value) return;
    if (!storeValue(QStringLiteral("navigation/sidebarVisible"), value)) return;
    m_sidebarVisible = value;
    emit inputSettingsChanged();
}

void AppSettings::setFileBrowserVisible(bool value) {
    if (m_fileBrowserVisible == value) return;
    if (!storeValue(QStringLiteral("navigation/fileBrowserVisible"), value)) return;
    m_fileBrowserVisible = value;
    emit inputSettingsChanged();
}

void AppSettings::setMacroBarVisible(bool value) {
    if (m_macroBarVisible == value) return;
    if (!storeValue(QStringLiteral("macros/barVisible"), value)) return;
    m_macroBarVisible = value;
    emit inputSettingsChanged();
}

QString AppSettings::defaultFontFamily() {
#ifdef Q_OS_WIN
    // Windows' generic FixedFont can be a legacy raster/Courier face. Consolas
    // ships with Windows 11 and has predictable TrueType terminal metrics.
    return QStringLiteral("Consolas");
#else
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
#endif
}

QString AppSettings::defaultLogDirectory() {
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }
    return QDir(documents).filePath(QStringLiteral("8bitShell/Logs"));
}

} // namespace bitshell::core
