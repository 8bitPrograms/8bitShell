#include <bitshell/terminal/TerminalView.h>
#include "TerminalViewCredentials.h"
#include <bitshell/terminal/LocalSshCommand.h>
#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/BoundedRegex.h>
#include <bitshell/core/AppSettings.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/NativeSshCommand.h>
#include <bitshell/core/PasswordStore.h>
#include <bitshell/core/PasswordBroker.h>

#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFocusEvent>
#include <QFontDatabase>
#include <QFontInfo>
#include <QHash>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QPainter>
#include <QQuickWindow>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <climits>
#include <cstring>
#include <utility>

namespace bitshell::terminal {

namespace {

constexpr int kMinimumColumns = 10;
constexpr int kMinimumRows = 2;

QString resolvedTerminalFont(const QString& requested) {
    // Ask Qt to resolve this fallback list rather than enumerating every
    // installed font for every new session. All callers run on the GUI thread.
    static QHash<QString, QString> cache;
    static const auto connection = QObject::connect(qGuiApp, &QGuiApplication::fontDatabaseChanged,
        qGuiApp, [] { cache.clear(); });
    Q_UNUSED(connection);
    if (const auto found = cache.constFind(requested); found != cache.cend()) return *found;
    QStringList families;
    for (const auto& candidate : requested.split(u',', Qt::SkipEmptyParts)) {
        const auto trimmed = candidate.trimmed();
        if (!trimmed.isEmpty()) families << trimmed;
    }
    families << QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    QFont font;
    font.setFamilies(families);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    const QString resolved = QFontInfo(font).family();
    if (cache.size() >= 64) cache.clear();
    cache.insert(requested, resolved);
    return resolved;
}

const QStringList& defaultAnsiPalette() {
    static const QStringList colors{
        QStringLiteral("#151d29"), QStringLiteral("#f26d78"),
        QStringLiteral("#7bd88f"), QStringLiteral("#e5b567"),
        QStringLiteral("#70a5eb"), QStringLiteral("#ad8ee6"),
        QStringLiteral("#45c2b1"), QStringLiteral("#c8d3e1"),
        QStringLiteral("#5f6c7d"), QStringLiteral("#ff8791"),
        QStringLiteral("#9be7a8"), QStringLiteral("#f2ca86"),
        QStringLiteral("#91bdf2"), QStringLiteral("#c6a9f4"),
        QStringLiteral("#6ee7d5"), QStringLiteral("#f4f7fb"),
    };
    return colors;
}

QColor colorValue(const QVariantMap& scheme,
                  const QString& key,
                  const QColor& fallback) {
    const QVariant value = scheme.value(key);
    if (value.canConvert<QColor>()) {
        const QColor color = value.value<QColor>();
        if (color.isValid()) {
            return color;
        }
    }
    const QColor color(value.toString());
    return color.isValid() ? color : fallback;
}

} // namespace

QFont TerminalView::terminalFont(bool bold, bool italic) const {
    QFont font(m_fontFamily);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPixelSize(std::max(1, qRound(m_fontPixelSize)));
    font.setBold(bold);
    font.setItalic(italic);
    if (m_textAntialiasing == QStringLiteral("grayscale"))
        font.setStyleStrategy(QFont::StyleStrategy(QFont::PreferAntialias | QFont::NoSubpixelAntialias));
    else if (m_textAntialiasing == QStringLiteral("aliased"))
        font.setStyleStrategy(QFont::NoAntialias);
    if (m_fontHinting == QStringLiteral("none")) font.setHintingPreference(QFont::PreferNoHinting);
    else if (m_fontHinting == QStringLiteral("vertical")) font.setHintingPreference(QFont::PreferVerticalHinting);
    else if (m_fontHinting == QStringLiteral("full")) font.setHintingPreference(QFont::PreferFullHinting);
    return font;
}

TerminalView::TerminalView(QQuickItem* parent)
    : QQuickPaintedItem(parent)
    , m_process(this)
    , m_fontFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family()) {
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setFlag(ItemAcceptsInputMethod, true);
    setAntialiasing(false);
    setOpaquePainting(true);
    setFillColor(m_defaultBackground);
    m_keyBindings = bitshell::core::AppSettings::defaultKeyBindings();

    initializeTerminal();
    recalculateMetrics();

    connect(&m_process, &PtyProcess::dataReceived, this, &TerminalView::handleProcessData);
    connect(&m_process, &PtyProcess::runningChanged, this, [this](bool running) {
        if (!running) {
            // Explicit stop/restart does not emit finished on every PTY
            // backend. A stopped process must never retain a searchable host
            // or a dispatch waiting for a prompt that can no longer arrive.
            stopPasswordBroker();
            releaseCredential();
            setCurrentSshConnection({});
            cancelPendingLocalSsh();
            m_localPromptActive = false;
            clearLocalSshQuery();
        }
        emit runningChanged();
    });
    connect(&m_process, &PtyProcess::finished, this, &TerminalView::handleProcessFinished);
    connect(&m_process, &PtyProcess::errorOccurred, this, [this](const QString& message) {
        setStatusText(QStringLiteral("Terminal error: %1").arg(message));
    });

    connect(this, &QQuickItem::visibleChanged, this, &TerminalView::syncPresentationState);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow* target) {
        for (const auto& connection : m_windowConnections) disconnect(connection);
        m_windowConnections.clear();
        if (target) {
            m_windowConnections << connect(target, &QWindow::visibilityChanged, this, &TerminalView::syncPresentationState);
        }
        syncPresentationState();
    });
    if (window()) {
        m_windowConnections << connect(window(), &QWindow::visibilityChanged, this, &TerminalView::syncPresentationState);
    }
}

void TerminalView::update() {
    m_contentDirty = true;
    if (!m_processingOutput) requestSceneUpdate();
}

void TerminalView::requestSceneUpdate() {
    ++m_sceneUpdateCount;
    QQuickPaintedItem::update();
}

void TerminalView::noteCursorGeometry() {
    m_cursorGeometryDirty = true;
    m_cursorActivity = true;
    scheduleCursorFlush();
}

void TerminalView::noteCursorActivity() {
    m_cursorActivity = true;
    scheduleCursorFlush();
}

void TerminalView::scheduleCursorFlush() {
    if (m_cursorFlushQueued) return;
    m_cursorFlushQueued = true;
    QMetaObject::invokeMethod(this, [this] { flushCursorNotifications(); }, Qt::QueuedConnection);
}

void TerminalView::flushCursorNotifications() {
    m_cursorFlushQueued = false;
    const bool geometry = m_cursorGeometryDirty;
    const bool activity = m_cursorActivity;
    m_cursorGeometryDirty = false;
    m_cursorActivity = false;
    if (activity) {
        ++m_cursorEpoch;
        emit cursorEpochChanged();
    }
    if (geometry) emit cursorPresentationChanged();
}

bool TerminalView::cursorOverlayVisible() const {
    return m_cursorVisible && m_scrollOffset == 0
        && m_cursorPosition.row >= 0 && m_cursorPosition.row < m_rows
        && m_cursorPosition.col >= 0 && m_cursorPosition.col < m_columns;
}

qreal TerminalView::cursorX() const {
    return m_horizontalPadding + static_cast<qreal>(m_cursorPosition.col) * m_cellWidth;
}

qreal TerminalView::cursorY() const {
    qreal y = m_verticalPadding + static_cast<qreal>(m_cursorPosition.row) * m_cellHeight;
    if (m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE) y += m_cellHeight - 2.0;
    return y;
}

qreal TerminalView::cursorWidth() const {
    return m_cursorShape == VTERM_PROP_CURSORSHAPE_BAR_LEFT ? 2.0 : m_cellWidth;
}

qreal TerminalView::cursorHeight() const {
    return m_cursorShape == VTERM_PROP_CURSORSHAPE_UNDERLINE ? 2.0 : m_cellHeight;
}

QColor TerminalView::cursorColor() const {
    return m_cursorColor;
}

bool TerminalView::cursorBlinkEnabled() const {
    return m_cursorBlinkEnabled;
}

int TerminalView::cursorEpoch() const {
    return m_cursorEpoch;
}

void TerminalView::damageRows(int start, int end) {
    if (m_contentDirty) {
        if (!m_processingOutput) requestSceneUpdate();
        return;
    }
    if (start <= 0 && end >= m_rows) { update(); return; }
    if (m_scrollOffset != 0) { update(); return; }
    m_dirtyRows.resize(m_rows);
    for (int row = std::clamp(start, 0, m_rows); row < std::clamp(end, 0, m_rows); ++row)
        m_dirtyRows.setBit(row);
    if (!m_processingOutput) requestSceneUpdate();
}

void TerminalView::syncPresentationState() {
    if (!isVisible() || !window() || !window()->isVisible() || window()->visibility() == QWindow::Minimized) {
        QMutexLocker locker(&m_terminalMutex);
        m_contentCache = QImage();
        m_contentDirty = true;
    }
    requestSceneUpdate();
}

TerminalView::~TerminalView() {
    // QQuickItem's base destructor emits focus/window signals after our members
    // have been destroyed. Stop these callbacks while the derived object exists.
    disconnect(this, nullptr, this, nullptr);
    disconnect(&m_process, nullptr, this, nullptr);
    stopPasswordBroker();
    m_pendingPasswordBroker.reset();
    m_keyLoader.reset();
    m_credential.reset();
    for (const auto& connection : m_windowConnections) disconnect(connection);
    m_windowConnections.clear();
    m_process.stop();
    stopLog();
    QMutexLocker locker(&m_terminalMutex);
    if (m_terminal != nullptr) {
        vterm_free(m_terminal);
        m_terminal = nullptr;
        m_screen = nullptr;
    }
}

QString TerminalView::fontFamily() const {
    return m_fontFamily;
}

QVariantMap TerminalView::memoryDiagnostics() const {
    QMutexLocker locker(&m_terminalMutex);
    return {
        {QStringLiteral("history_accounted_bytes"), qint64(m_scrollbackBytes)},
        {QStringLiteral("history_budget_bytes"), qint64(scrollbackMemoryBudget)},
        {QStringLiteral("history_lines"), int(m_scrollback.size())},
        {QStringLiteral("viewport_cache_bytes"), qint64(m_contentCache.sizeInBytes())},
        {QStringLiteral("columns"), m_columns}, {QStringLiteral("rows"), m_rows},
        {QStringLiteral("logging_enabled"), m_logEnabled},
        {QStringLiteral("fixture_output_complete"), m_performanceFixtureComplete},
        {QStringLiteral("scene_updates"), qint64(m_sceneUpdateCount)},
        {QStringLiteral("content_paints"), qint64(m_contentPaintCount)}
    };
}

void TerminalView::setFontFamily(const QString& family) {
    const QString resolved = resolvedTerminalFont(family);
    if (m_fontFamily == resolved) {
        return;
    }
    m_fontFamily = resolved;
    recalculateMetrics();
    emit fontFamilyChanged();
    update();
}

qreal TerminalView::fontPixelSize() const {
    return m_fontPixelSize;
}

void TerminalView::setFontPixelSize(qreal size) {
    const qreal bounded = std::clamp(size, 7.0, 40.0);
    if (qFuzzyCompare(m_fontPixelSize, bounded)) {
        return;
    }
    m_fontPixelSize = bounded;
    recalculateMetrics();
    emit fontPixelSizeChanged();
    update();
}

void TerminalView::setTextAntialiasing(const QString& mode) {
    const auto normalized = mode.trimmed().toLower();
    if (normalized == m_textAntialiasing
        || !QStringList{QStringLiteral("system"), QStringLiteral("grayscale"), QStringLiteral("aliased")}.contains(normalized)) return;
    m_textAntialiasing = normalized;
    recalculateMetrics();
    emit renderingChanged();
    update();
}

void TerminalView::setFontHinting(const QString& mode) {
    const auto normalized = mode.trimmed().toLower();
    if (normalized == m_fontHinting
        || !QStringList{QStringLiteral("default"), QStringLiteral("none"), QStringLiteral("vertical"), QStringLiteral("full")}.contains(normalized)) return;
    m_fontHinting = normalized;
    recalculateMetrics();
    emit renderingChanged();
    update();
}

int TerminalView::columns() const {
    return m_columns;
}

int TerminalView::rows() const {
    return m_rows;
}

bool TerminalView::isRunning() const {
    return m_process.isRunning();
}

QString TerminalView::title() const {
    return m_title;
}

QString TerminalView::activeTarget() const {
    return m_activeTarget;
}

QString TerminalView::statusText() const {
    return m_statusText;
}

bool TerminalView::logEnabled() const {
    return m_logEnabled;
}

void TerminalView::setLogEnabled(bool enabled) {
    if (m_logEnabled == enabled) {
        return;
    }
    m_logEnabled = enabled;
    if (enabled && isRunning()) {
        startLog(m_activeTarget);
    } else if (!enabled) {
        stopLog();
    }
    emit logEnabledChanged();
}

QString TerminalView::logFilePath() const {
    return m_logFilePath;
}

QString TerminalView::logError() const {
    return m_logError;
}

int TerminalView::scrollbackLines() const {
    return static_cast<int>(m_scrollback.size());
}

int TerminalView::scrollOffset() const {
    return m_scrollOffset;
}

bool TerminalView::syntaxHighlightEnabled() const {
    return m_syntaxHighlightEnabled;
}

void TerminalView::setSyntaxHighlightEnabled(bool enabled) {
    if (m_syntaxHighlightEnabled == enabled) {
        return;
    }
    m_syntaxHighlightEnabled = enabled;
    emit syntaxHighlightEnabledChanged();
    update();
}

QVariantList TerminalView::highlightRules() const {
    return m_highlightRuleValues;
}

void TerminalView::setHighlightRules(const QVariantList& rules) {
    if (m_highlightRuleValues == rules) {
        return;
    }

    QList<bitshell::core::HighlightRule> parsed;
    parsed.reserve(std::min(qsizetype(64), rules.size()));
    for (qsizetype index = 0; index < std::min(qsizetype(64), rules.size()); ++index) {
        const QVariantMap value = rules.at(index).toMap();
        const QString pattern = value.value(QStringLiteral("pattern")).toString();
        if (pattern.isEmpty()) {
            continue;
        }
        QRegularExpression::PatternOptions options =
            QRegularExpression::UseUnicodePropertiesOption;
        if (!value.value(QStringLiteral("caseSensitive")).toBool()) {
            options |= QRegularExpression::CaseInsensitiveOption;
        }
        QRegularExpression expression = core::boundedRegex(pattern, options);
        const QColor foreground(value.value(QStringLiteral("foreground")).toString());
        const QString backgroundName =
            value.value(QStringLiteral("background")).toString().trimmed();
        const QColor background = backgroundName.isEmpty() ? QColor(Qt::transparent)
                                                            : QColor(backgroundName);
        if (!expression.isValid() || !foreground.isValid() || !background.isValid()) {
            continue;
        }

        parsed.push_back(bitshell::core::HighlightRule{
            .id = value.value(QStringLiteral("id"),
                              QStringLiteral("rule-%1").arg(index))
                      .toString(),
            .displayName = value.value(QStringLiteral("name")).toString(),
            .pattern = std::move(expression),
            .foreground = foreground,
            .background = background,
            .bold = value.value(QStringLiteral("bold")).toBool(),
            .underline = value.value(QStringLiteral("underline")).toBool(),
            .enabled = value.value(QStringLiteral("enabled"), true).toBool(),
            .priority = value.value(QStringLiteral("priority")).toInt(),
        });
    }

    {
        QMutexLocker locker(&m_terminalMutex);
        m_highlightRuleValues = rules;
        m_highlightRules = std::move(parsed);
    }
    emit highlightRulesChanged();
    update();
}

QString TerminalView::searchText() const {
    return m_searchText;
}

void TerminalView::setSearchText(const QString& text) {
    if (m_searchText == text) {
        return;
    }
    {
        QMutexLocker locker(&m_terminalMutex);
        m_searchText = text;
        rebuildSearchMatchesLocked();
        revealCurrentSearchMatchLocked();
    }
    emit searchTextChanged();
    emit searchMatchesChanged();
    emit scrollOffsetChanged();
    update();
}

int TerminalView::searchMatchCount() const {
    return static_cast<int>(m_searchMatches.size());
}

int TerminalView::currentSearchMatch() const {
    return m_currentSearchMatch;
}

int TerminalView::scrollbackLimit() const {
    return m_scrollbackLimit;
}

QString TerminalView::backspaceMode() const {
    return m_backspaceMode;
}

void TerminalView::setBackspaceMode(const QString& mode) {
    const QString normalized = mode.trimmed().toLower() == QStringLiteral("bs")
                                   ? QStringLiteral("bs")
                                   : QStringLiteral("del");
    if (m_backspaceMode == normalized) {
        return;
    }
    m_backspaceMode = normalized;
    m_process.setEraseCharacter(normalized == QStringLiteral("bs") ? '\x08' : '\x7f');
    emit backspaceModeChanged();
}

QByteArray TerminalView::backspaceSequence(const QString& mode,
                                           Qt::KeyboardModifiers modifiers) {
    QByteArray sequence;
    if (modifiers.testFlag(Qt::AltModifier)) {
        sequence.append('\x1b');
    }
    sequence.append(mode.trimmed().compare(QStringLiteral("bs"),
                                           Qt::CaseInsensitive) == 0
                        ? '\x08' : '\x7f');
    return sequence;
}

QVariantMap TerminalView::colorScheme() const {
    return m_colorScheme;
}

void TerminalView::setColorScheme(const QVariantMap& scheme) {
    const QColor foreground = colorValue(scheme, QStringLiteral("foreground"),
                                         QColor(QStringLiteral("#e8eef7")));
    const QColor background = colorValue(scheme, QStringLiteral("background"),
                                         QColor(QStringLiteral("#070b11")));
    const QColor cursor = colorValue(scheme, QStringLiteral("cursor"),
                                     QColor(QStringLiteral("#6ee7d5")));
    const QColor selection = colorValue(scheme, QStringLiteral("selection"),
                                        QColor(QStringLiteral("#24504f")));

    QVariantList normalizedAnsi;
    const QVariantList requestedAnsi = scheme.value(QStringLiteral("ansi")).toList();
    normalizedAnsi.reserve(16);
    for (int index = 0; index < 16; ++index) {
        const QColor fallback(defaultAnsiPalette().at(index));
        QColor color = fallback;
        if (index < requestedAnsi.size()) {
            const QVariant value = requestedAnsi.at(index);
            if (value.canConvert<QColor>() && value.value<QColor>().isValid()) {
                color = value.value<QColor>();
            } else {
                const QColor parsed(value.toString());
                if (parsed.isValid()) {
                    color = parsed;
                }
            }
        }
        normalizedAnsi.push_back(color.name(QColor::HexRgb));
    }

    const QVariantMap normalized{
        {QStringLiteral("foreground"), foreground.name(QColor::HexRgb)},
        {QStringLiteral("background"), background.name(QColor::HexRgb)},
        {QStringLiteral("cursor"), cursor.name(QColor::HexRgb)},
        {QStringLiteral("selection"), selection.name(QColor::HexRgb)},
        {QStringLiteral("ansi"), normalizedAnsi},
    };
    if (m_colorScheme == normalized) {
        return;
    }

    {
        QMutexLocker locker(&m_terminalMutex);
        m_colorScheme = normalized;
        m_defaultForeground = foreground;
        m_defaultBackground = background;
        m_cursorColor = cursor;
        m_selectionColor = selection;

        if (m_terminal != nullptr) {
            VTermColor foregroundValue{};
            VTermColor backgroundValue{};
            vterm_color_rgb(&foregroundValue,
                            static_cast<uint8_t>(foreground.red()),
                            static_cast<uint8_t>(foreground.green()),
                            static_cast<uint8_t>(foreground.blue()));
            vterm_color_rgb(&backgroundValue,
                            static_cast<uint8_t>(background.red()),
                            static_cast<uint8_t>(background.green()),
                            static_cast<uint8_t>(background.blue()));
            VTermState* state = vterm_obtain_state(m_terminal);
            for (int index = 0; index < normalizedAnsi.size(); ++index) {
                const QColor color(normalizedAnsi.at(index).toString());
                VTermColor paletteValue{};
                vterm_color_rgb(&paletteValue,
                                static_cast<uint8_t>(color.red()),
                                static_cast<uint8_t>(color.green()),
                                static_cast<uint8_t>(color.blue()));
                vterm_state_set_palette_color(state, index, &paletteValue);
            }
            vterm_screen_set_default_colors(m_screen, &foregroundValue, &backgroundValue);
        }
    }
    setFillColor(background);
    emit colorSchemeChanged();
    noteCursorGeometry();
    update();
}

void TerminalView::setScrollbackLimit(int lines) {
    const int bounded = std::clamp(lines, 100, 100000);
    if (m_scrollbackLimit == bounded) {
        return;
    }
    {
        QMutexLocker locker(&m_terminalMutex);
        m_scrollbackLimit = bounded;
        while (static_cast<int>(m_scrollback.size()) > m_scrollbackLimit) {
            m_scrollbackBytes -= m_scrollback.front().memoryBytes();
            m_scrollback.pop_front();
            ++m_historyDropped;
            if (m_selectionAnchor.line >= 0) {
                --m_selectionAnchor.line;
                --m_selectionEnd.line;
            }
        }
        m_scrollOffset = std::min(m_scrollOffset, static_cast<int>(m_scrollback.size()));
        rebuildSearchMatchesLocked();
    }
    emit scrollbackLimitChanged();
    emit scrollbackChanged();
    emit scrollOffsetChanged();
    emit searchMatchesChanged();
    update();
}

QString TerminalView::logRootDirectory() const {
    return m_logRootDirectory;
}

void TerminalView::setLogRootDirectory(const QString& directory) {
    const QString trimmed = directory.trimmed();
    const QString normalized = trimmed.isEmpty() ? QString{} : QDir::cleanPath(trimmed);
    if (m_logRootDirectory == normalized) {
        return;
    }
    m_logRootDirectory = normalized;
    restartLogForOptionChange();
    emit logRootDirectoryChanged();
}

QString TerminalView::logDirectoryTemplate() const {
    return m_logDirectoryTemplate;
}

void TerminalView::setLogDirectoryTemplate(const QString& value) {
    const QString normalized = value.trimmed().isEmpty() ? QStringLiteral("{session}")
                                                          : value.trimmed();
    if (m_logDirectoryTemplate == normalized) {
        return;
    }
    m_logDirectoryTemplate = normalized;
    restartLogForOptionChange();
    emit logDirectoryTemplateChanged();
}

QString TerminalView::logFileNameTemplate() const {
    return m_logFileNameTemplate;
}

void TerminalView::setLogFileNameTemplate(const QString& value) {
    const QString normalized = value.trimmed().isEmpty()
                                   ? QStringLiteral("{session}_{date}_{time}.log")
                                   : value.trimmed();
    if (m_logFileNameTemplate == normalized) {
        return;
    }
    m_logFileNameTemplate = normalized;
    restartLogForOptionChange();
    emit logFileNameTemplateChanged();
}

bool TerminalView::logTimestamps() const {
    return m_logTimestamps;
}

void TerminalView::setLogTimestamps(bool enabled) {
    if (m_logTimestamps == enabled) {
        return;
    }
    m_logTimestamps = enabled;
    restartLogForOptionChange();
    emit logTimestampsChanged();
}

bool TerminalView::logAppend() const {
    return m_logAppend;
}

void TerminalView::setLogAppend(bool enabled) {
    if (m_logAppend == enabled) {
        return;
    }
    m_logAppend = enabled;
    restartLogForOptionChange();
    emit logAppendChanged();
}

int TerminalView::logRotationMegabytes() const {
    return m_logRotationMegabytes;
}

void TerminalView::setLogRotationMegabytes(int megabytes) {
    const int bounded = std::clamp(megabytes, 0, 4096);
    if (m_logRotationMegabytes == bounded) {
        return;
    }
    m_logRotationMegabytes = bounded;
    restartLogForOptionChange();
    emit logRotationMegabytesChanged();
}

int TerminalView::logRetainedFiles() const {
    return m_logRetainedFiles;
}

void TerminalView::setLogRetainedFiles(int files) {
    const int bounded = std::clamp(files, 0, 50);
    if (m_logRetainedFiles == bounded) {
        return;
    }
    m_logRetainedFiles = bounded;
    restartLogForOptionChange();
    emit logRetainedFilesChanged();
}

bool TerminalView::startLocalShell() {
    m_lastSshProfile.clear();
    return launchProcess(core::Platform::localShell(),
                         core::Platform::localShellArguments(),
                         QStringLiteral("Local"),
                         QStringLiteral("Starting local shell…"), false, true);
}

void TerminalView::restartLocalShell() {
    startLocalShell();
}

bool TerminalView::startLocalCommand(const QStringList& command) {
    if (command.isEmpty() || command.size() > 1024 || command.first().isEmpty()) {
        setStatusText(QStringLiteral("Supply a local executable after -e.")); return false;
    }
    qsizetype bytes = 0;
    for (const auto& arg : command) {
        bytes += arg.toUtf8().size();
        if (arg.contains(QChar::Null) || bytes > 1024 * 1024) {
            setStatusText(QStringLiteral("Invalid or oversized local command arguments.")); return false;
        }
    }
    const auto program = QDir::isAbsolutePath(command.first()) ? command.first() : QStandardPaths::findExecutable(command.first());
    if (program.isEmpty() || !QFileInfo(program).isExecutable() || !QFileInfo(program).isFile()) {
        setStatusText(QStringLiteral("Local executable was not found.")); return false;
    }
    m_lastSshProfile.clear();
    return launchProcess(program, command.sliced(1), QFileInfo(program).fileName(), QStringLiteral("Starting local command…"));
}

bool TerminalView::startLocalProfile(const QVariantMap& profile) {
    QString error;
    const auto launch = core::LocalProfileStore::prepareLaunch(profile, error);
    if (launch.isEmpty()) { setStatusText(error); return false; }
    m_lastSshProfile.clear();
    return launchProcess(launch.value(QStringLiteral("program")).toString(),
        launch.value(QStringLiteral("arguments")).toStringList(), launch.value(QStringLiteral("name")).toString(),
        QStringLiteral("Starting local profile…"), false, true,
        launch.value(QStringLiteral("workingDirectory")).toString());
}

void TerminalView::restartSession() {
    if (!m_lastSshProfile.isEmpty()) { launchSshProfile(m_lastSshProfile); return; }
    if (m_lastProgram.isEmpty()) {
        startLocalShell();
        return;
    }
    launchProcess(m_lastProgram,
                  m_lastArguments,
                  m_lastTargetLabel,
                  m_lastStartingStatus, m_managedSsh, m_lastInteractiveShell, m_lastWorkingDirectory);
}

void TerminalView::connectToTarget(const QString& target) {
    const QString trimmed = target.trimmed();
    if (trimmed.isEmpty()) {
        forceActiveFocus();
        return;
    }
    const auto profile = core::SshSecurity::quickProfile(trimmed);
    if (!profile) { setStatusText(profile.error()); return; }
    startSshProfile(*profile);
}

void TerminalView::startSshSession(const QString& host, const QString& user, int port) {
    startSshProfile({{QStringLiteral("host"), host.trimmed()}, {QStringLiteral("user"), user.trimmed()},
                     {QStringLiteral("port"), port}});
}

void TerminalView::startSshProfile(const QVariantMap& profile) {
    m_pendingX11Profile.clear();
    const auto error = core::SshSecurity::validateProfile(profile);
    if (!error.isEmpty()) { setStatusText(error); return; }
    if (profile.value(QStringLiteral("forwardX11")).toBool()) {
        m_pendingX11Profile = profile;
        emit x11ConfirmationRequested(profile.value(QStringLiteral("host")).toString());
        return;
    }
    launchSshProfile(profile);
}
void TerminalView::confirmX11(bool accepted) {
    const auto profile = std::exchange(m_pendingX11Profile, {});
    if (accepted && !profile.isEmpty()) launchSshProfile(profile);
    else if (!accepted) setStatusText(QStringLiteral("X11 connection cancelled."));
}
void TerminalView::launchSshProfile(const QVariantMap& profile) {
    // Without a credential or saved password, effective is the profile unchanged.
    // A secret that cannot be read never blocks the launch: ssh prompts as usual.
    QVariantMap effective;
    QString notice;
    prepareCredential(profile, effective, notice);
    const auto command = core::SshSecurity::command(effective, core::SshSecurity::Transport::Terminal);
    if (!command) { m_preparedCredential.reset(); setStatusText(command.error()); return; }
    const QString name = profile.value(QStringLiteral("name")).toString().trimmed();
    const QString label = name.isEmpty() ? command->target : name;
    if (launchProcess(command->program,
                  command->arguments,
                  label,
                  QStringLiteral("Connecting to %1…").arg(command->target), true, false, {}, {})) {
        armPreparedCredential();
        if (!notice.isEmpty()) setStatusText(notice);
        m_lastSshProfile = profile;
        setCurrentSshConnection(sshConnectionSummary(effective, QStringLiteral("managed")));
    } else m_preparedCredential.reset();
}

void TerminalView::stopPasswordBroker() {
    if (m_passwordBroker) m_passwordBroker->stop();
    m_passwordBroker.reset();
}

QVariantMap TerminalView::sshConnectionSummary(const QVariantMap& profile, const QString& mode) {
    const auto host = profile.value(QStringLiteral("host")).toString();
    if (host.isEmpty()) return {};
    // Only display/search metadata crosses this boundary. No keys, paths,
    // passwords, generated shell command or gateway login string is exposed.
    const auto plan = profile.value(QStringLiteral("psmp")).toMap();
    const auto route = plan.value(QStringLiteral("route")).toMap();
    const bool routed = !plan.isEmpty();
    QString account = profile.value(QStringLiteral("user")).toString();
    if (routed) {
        const auto rule = plan.value(QStringLiteral("account")).toMap();
        account = rule.value(QStringLiteral("source")) == QStringLiteral("identity")
            ? plan.value(QStringLiteral("operator")).toString() + rule.value(QStringLiteral("suffix")).toString()
            : rule.value(QStringLiteral("name")).toString();
        const auto domain = rule.value(QStringLiteral("domain")).toString();
        if (!domain.isEmpty()) account += u'@' + domain;
    }
    const int port = profile.value(QStringLiteral("port"), 22).toInt();
    return {{QStringLiteral("host"), host}, {QStringLiteral("user"), account},
        {QStringLiteral("port"), port > 0 ? port : 22},
        {QStringLiteral("name"), profile.value(QStringLiteral("name")).toString()},
        {QStringLiteral("catalogId"), profile.value(QStringLiteral("id")).toString()},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("connectionMode"), routed ? QStringLiteral("cyberark") : QStringLiteral("direct")},
        {QStringLiteral("gateway"), route.value(QStringLiteral("host")).toString()},
        {QStringLiteral("routeId"), plan.value(QStringLiteral("routeId")).toString()}};
}

void TerminalView::setCurrentSshConnection(const QVariantMap& connection) {
    if (m_currentSshConnection == connection) return;
    m_currentSshConnection = connection;
    if (m_credential && connection.value(QStringLiteral("mode")) == QStringLiteral("local")) [[unlikely]]
        targetBoundCredential(connection);
    // OSC callbacks run under the terminal mutex. Publish after that batch so
    // palette listeners can safely inspect the terminal or change focus.
    if (m_processingOutput) m_currentSshConnectionNotificationPending = true;
    else {
        m_currentSshConnectionNotificationPending = false;
        emit currentSshConnectionChanged();
    }
}

bool TerminalView::setBroadcastArmed(bool enabled) {
    if (enabled && (!m_managedSsh || !isRunning())) return false;
    if (m_broadcastArmed != enabled) {
        m_broadcastArmed = enabled; ++m_broadcastGeneration; emit broadcastStateChanged();
    }
    return true;
}

QString TerminalView::broadcastError(const QString& command, quint64 generation) const {
    if (!broadcastArmed() || generation != m_broadcastGeneration)
        return QStringLiteral("A target closed, restarted or was deselected. Review your targets again.");
    if (!m_pendingPaste.isEmpty() || !m_pendingX11Profile.isEmpty())
        return QStringLiteral("A target has an unresolved security prompt.");
    if (!m_process.canWrite(command.toUtf8().size() + 32))
        return QStringLiteral("A target's input queue is full. Nothing was sent.");
    return {};
}

void TerminalView::sendText(const QString& text) {
    if (isRunning() && !text.isEmpty() && m_pendingLocalSshCommand.isEmpty()) {
        ++m_localSshRefreshSerial;
        clearLocalSshQuery();
        if (text.contains(u'\r') || text.contains(u'\n') || text.contains(QChar(3))) m_localPromptActive = false;
        if (m_credential) [[unlikely]] noteCredentialInput();
        m_process.write(text.toUtf8());
    }
}

void TerminalView::clearLocalSshQuery() {
    if (m_localSshQuery.isEmpty()) return;
    m_localSshQuery.clear();
    emit localSshQueryChanged();
}

void TerminalView::dismissLocalSsh() {
    m_dismissedLocalSshCommand = m_localSshQuery.value(QStringLiteral("command")).toString();
    clearLocalSshQuery();
    emit localSshDismiss();
}

QString TerminalView::localInputTextLocked() const {
    if (!m_localPromptActive || m_alternateScreen || m_mouseMode != VTERM_PROP_MOUSE_NONE
        || m_managedSsh || !m_lastInteractiveShell || m_scrollOffset != 0
        || !m_pendingPaste.isEmpty() || !m_pendingX11Profile.isEmpty()
        || m_integratedShellKind.isEmpty() || m_localInputColumn < 0) return {};
    VTermPos cursor{};
    vterm_state_get_cursorpos(vterm_obtain_state(m_terminal), &cursor);
    const auto row = m_historyDropped + quint64(m_scrollback.size()) + quint64(cursor.row);
    // A partial/wrapped line or an editing cursor before the end is ambiguous.
    // Keep the normal shell behavior in those cases, including predictive tails.
    if (row != m_localInputRow || cursor.col < m_localInputColumn) return {};
    const auto cells = lineAt(int(m_scrollback.size()) + cursor.row);
    QString command = QStringLiteral("");
    for (int column = m_localInputColumn; column < m_columns; ++column) {
        const auto& cell = cells[size_t(column)];
        if (cell.width == 0) continue;
        const auto glyph = textForCell(cell);
        if (column >= cursor.col) {
            if (!glyph.trimmed().isEmpty()) return {};
        } else command += glyph.isEmpty() ? QStringLiteral(" ") : glyph;
    }
    return command;
}

void TerminalView::refreshLocalSshQuery() {
    QVariantMap query;
    if (isRunning() && m_pendingLocalSshCommand.isEmpty()) {
        QMutexLocker locker(&m_terminalMutex);
        const auto command = localInputTextLocked();
        if (command != m_lastLocalInput) {
            m_dismissedLocalSshCommand.clear();
            m_lastLocalInput = command;
        }
        if (!command.isNull() && command != m_dismissedLocalSshCommand) {
            query = LocalSshCommand::parse(command);
            if (!query.isEmpty()) {
                query.insert(QStringLiteral("shellKind"), m_integratedShellKind);
                query.insert(QStringLiteral("generation"), QVariant::fromValue(m_localPromptGeneration));
            }
        }
    }
    if (query != m_localSshQuery) {
        m_localSshQuery = query;
        emit localSshQueryChanged();
    }
}

void TerminalView::sendLocalControlC() {
    if (m_win32Input.enabled()) {
        QKeyEvent press(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier, QString(QChar(3)));
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_C, Qt::ControlModifier, QString(QChar(3)));
        QKeyEvent modifierRelease(QEvent::KeyRelease, Qt::Key_Control, Qt::NoModifier);
        m_process.write(Win32Input::encode(press, true) + Win32Input::encode(release, false)
                        + Win32Input::encode(modifierRelease, false));
    } else sendCodepoint('c', VTERM_MOD_CTRL);
}

bool TerminalView::runLocalSsh(const QVariantMap& profile, const QVariantMap& expectedQuery) {
    // An input API may have invalidated the last visible suggestion before the
    // shell echoes that input. Never recreate and accept the older screen line.
    if (expectedQuery.isEmpty() || expectedQuery != m_localSshQuery) {
        emit localSshError(QStringLiteral("The local command changed. Select the SSH account again."));
        return false;
    }
    refreshLocalSshQuery();
    if (expectedQuery.isEmpty() || expectedQuery != m_localSshQuery || !isRunning()
        || !m_pendingLocalSshCommand.isEmpty()) {
        emit localSshError(QStringLiteral("The local command changed. Select the SSH account again."));
        return false;
    }
    QProcessEnvironment environment;
    std::unique_ptr<core::PasswordBroker> broker;
    const bool passwordMode = profile.value(QStringLiteral("authenticationMode")) == QStringLiteral("password");
    const bool wsl = m_integratedShellKind == QStringLiteral("wsl-bash");
    QString notice;
    if (passwordMode && !wsl && !core::PasswordStore::binding(profile).isEmpty()) {
        auto password = core::PasswordStore::read(profile);
        // An unreadable keyring never blocks ssh: the user types the password.
        if (!password)
            notice = QStringLiteral("Could not read the saved password: %1 Type it yourself.").arg(password.error());
        else if (password->isEmpty() && core::PasswordStore::has(profile))
            notice = QStringLiteral("The saved password is missing from %1. Type it yourself.").arg(core::PasswordStore::backendName());
        else if (!password->isEmpty()) {
            broker = std::make_unique<core::PasswordBroker>();
            const auto additions = broker->start(profile, *password);
            password->fill('\0'); password->clear();
            if (!additions) { emit localSshError(additions.error()); return false; }
            environment = *additions;
        }
    }
    const auto command = core::NativeSshCommand::command(profile, m_integratedShellKind, environment);
    if (!command) { emit localSshError(command.error()); return false; }
    // The builder's output is one native-shell command, never terminal controls.
    if (command->isEmpty() || command->size() > 32768
        || std::any_of(command->cbegin(), command->cend(), [](QChar c) { return c.unicode() < 32 || c.unicode() == 127; })) {
        emit localSshError(QStringLiteral("The SSH command could not be represented safely in this shell."));
        return false;
    }
    m_pendingLocalSshCommand = *command;
    m_pendingPasswordBroker = std::move(broker);
    m_pendingLocalSshStatus = passwordMode && wsl
        ? QStringLiteral("WSL SSH: enter the password in this terminal; saved Windows passwords are not forwarded to WSL.") : notice;
    m_pendingLocalSshConnection = sshConnectionSummary(profile, QStringLiteral("local"));
    m_pendingLocalSshShellKind = m_integratedShellKind;
    m_pendingLocalSshGeneration = m_localPromptGeneration;
    const auto request = ++m_localSshRequestSerial;
    m_localPromptActive = false;
    clearLocalSshQuery();
    sendLocalControlC();
    QTimer::singleShot(5000, this, [this, request] {
        if (request != m_localSshRequestSerial || m_pendingLocalSshCommand.isEmpty()) return;
        cancelPendingLocalSsh();
        emit localSshError(QStringLiteral("The local shell did not return to an empty prompt. Nothing was connected; enter the SSH command again."));
    });
    return true;
}

void TerminalView::cancelPendingLocalSsh() {
    ++m_localSshRequestSerial;
    m_pendingPasswordBroker.reset();
    m_pendingLocalSshCommand.clear();
    m_pendingLocalSshStatus.clear();
    m_pendingLocalSshConnection.clear();
    m_pendingLocalSshShellKind.clear();
}

void TerminalView::completePendingLocalSsh() {
    if (m_pendingLocalSshCommand.isEmpty() || !m_localPromptActive
        || m_localPromptGeneration <= m_pendingLocalSshGeneration) return;
    bool emptyPrompt = false;
    {
        QMutexLocker locker(&m_terminalMutex);
        const auto input = localInputTextLocked();
        emptyPrompt = !input.isNull() && input.isEmpty();
    }
    if (!emptyPrompt || m_integratedShellKind != m_pendingLocalSshShellKind) {
        cancelPendingLocalSsh();
        emit localSshError(QStringLiteral("The local shell prompt changed. Nothing was connected."));
        return;
    }
    QByteArray input;
    if (m_win32Input.enabled()) {
        // ConPTY coalesces adjacent identical key-down records. A complete
        // down/up pair per character preserves literal runs such as ssh, 2222
        // and quoted paths; these are synthetic keystrokes, not held keys.
        for (QChar character : m_pendingLocalSshCommand) {
            const QString text(character);
            QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
            input += Win32Input::encode(press, true) + Win32Input::encode(release, false);
        }
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier, QStringLiteral("\r"));
        input += Win32Input::encode(enter, true) + Win32Input::encode(release, false);
    } else input = m_pendingLocalSshCommand.toUtf8() + '\r';
    const auto connection = m_pendingLocalSshConnection;
    const auto status = m_pendingLocalSshStatus;
    auto broker = std::move(m_pendingPasswordBroker);
    cancelPendingLocalSsh();
    m_localPromptActive = false;
    clearLocalSshQuery();
    if (!m_process.canWrite(input.size())) {
        emit localSshError(QStringLiteral("The terminal input queue is full. Nothing was connected."));
        return;
    }
    m_process.write(input);
    stopPasswordBroker();
    m_passwordBroker = std::move(broker);
    if (!status.isEmpty()) setStatusText(status);
    setCurrentSshConnection(connection);
}

int TerminalView::oscFallbackCallback(int command, VTermStringFragment fragment, void* user) {
    if (command != 133 && command != 777) return 0;
    static_cast<TerminalView*>(user)->processShellMarker(command, fragment);
    return 1;
}

void TerminalView::processShellMarker(int command, VTermStringFragment fragment) {
    if (fragment.initial) m_shellMarkerFragment.clear();
    if (m_shellMarkerFragment.size() + qsizetype(fragment.len) > 256) {
        m_shellMarkerFragment = QByteArray(257, '!');
        return;
    }
    m_shellMarkerFragment.append(fragment.str, qsizetype(fragment.len));
    if (!fragment.final || m_shellIntegrationToken.isEmpty() || m_managedSsh || !m_lastInteractiveShell) return;
    const auto fields = QString::fromLatin1(m_shellMarkerFragment).split(u';');
    if (command == 777) {
        if (fields.size() == 3 && fields[0] == QStringLiteral("8bitshell") && fields[1] == m_shellIntegrationToken
            && (fields[2] == QStringLiteral("powershell") || fields[2] == QStringLiteral("bash") || fields[2] == QStringLiteral("wsl-bash")))
            m_integratedShellKind = fields[2];
        return;
    }
    if (fields.size() != 2 || fields[1] != m_shellIntegrationToken || m_integratedShellKind.isEmpty()) return;
    if (fields[0] == QStringLiteral("A") || fields[0] == QStringLiteral("C")) {
        m_localPromptActive = false;
        m_localInputColumn = -1;
    } else if (fields[0] == QStringLiteral("B") && !m_alternateScreen) {
        // Return to the authenticated local prompt expires a native child's
        // capability even when it never requested a password.
        stopPasswordBroker();
        if (m_credential) m_credential->restartPending = true;
        if (m_currentSshConnection.value(QStringLiteral("mode")) == QStringLiteral("local"))
            setCurrentSshConnection({});
        VTermPos cursor{};
        vterm_state_get_cursorpos(vterm_obtain_state(m_terminal), &cursor);
        m_localInputRow = m_historyDropped + quint64(m_scrollback.size()) + quint64(cursor.row);
        m_localInputColumn = cursor.col;
        ++m_localPromptGeneration;
        m_dismissedLocalSshCommand.clear();
        m_localPromptActive = true;
    }
}

void TerminalView::pasteClipboard() {
    const QString text = QGuiApplication::clipboard()->text(QClipboard::Clipboard);
    pasteText(text);
}

void TerminalView::pasteSelection() {
    pasteText(QGuiApplication::clipboard()->text(QClipboard::Selection));
}

void TerminalView::pasteText(const QString& text) {
    requestPaste(text, false);
}
void TerminalView::requestPaste(const QString& text, bool appendEnter) {
    if (text.isEmpty() || !isRunning() || !m_pendingLocalSshCommand.isEmpty()) return;
    ++m_localSshRefreshSerial;
    clearLocalSshQuery();
    m_pendingPaste.clear();
    if (text.size() > 256 * 1024 || text.toUtf8().size() > 1024 * 1024 - 32
        || std::any_of(text.cbegin(), text.cend(), [](QChar c) {
            return c.category() == QChar::Other_Control && c != u'\n' && c != u'\r' && c != u'\t';
        })) {
        setStatusText(QStringLiteral("Paste blocked: terminal control characters or oversized input. Nothing was sent.")); return;
    }
    const bool invisible = std::any_of(text.cbegin(), text.cend(), [](QChar c) {
        return c.category() == QChar::Other_Format || c == QChar(0x2028) || c == QChar(0x2029);
    });
    if (appendEnter || invisible || text.contains(u'\n') || text.contains(u'\r')) {
        m_pendingPaste = text;
        m_pendingPasteEnter = appendEnter;
        QString preview;
        for (const QChar c : text) {
            if (c.category() == QChar::Other_Format || c == u'\r')
                preview += QStringLiteral("[U+%1]").arg(static_cast<uint>(c.unicode()), 4, 16, QLatin1Char('0'));
            else preview += c;
        }
        emit pasteConfirmationRequested(preview, appendEnter);
        return;
    }
    writePaste(text, appendEnter);
}
void TerminalView::confirmPaste(bool accepted) {
    const QString text = std::exchange(m_pendingPaste, {});
    if (accepted && isRunning() && !text.isEmpty()) writePaste(text, m_pendingPasteEnter);
    m_pendingPasteEnter = false;
    forceActiveFocus();
}
void TerminalView::writePaste(const QString& text, bool appendEnter) {
    if (!m_pendingLocalSshCommand.isEmpty()) return;
    ++m_localSshRefreshSerial;
    clearLocalSshQuery();
    if (appendEnter || text.contains(u'\r') || text.contains(u'\n')) m_localPromptActive = false;
    if (m_credential) [[unlikely]] noteCredentialInput();
    QMutexLocker locker(&m_terminalMutex);
    vterm_keyboard_start_paste(m_terminal);
    m_process.write(text.toUtf8());
    vterm_keyboard_end_paste(m_terminal);
    if (appendEnter) m_process.write(QByteArray(1, '\r'));
}

void TerminalView::sendMacro(const QString& text, bool appendEnter) {
    requestPaste(text, appendEnter);
    if (m_pendingPaste.isEmpty()) forceActiveFocus();
}

void TerminalView::setCopyOnSelect(bool value) {
    if (m_copyOnSelect == value) return;
    m_copyOnSelect = value; emit inputSettingsChanged();
}
void TerminalView::setKeyBindings(const QVariantMap& value) {
    if (m_keyBindings == value) return;
    m_keyBindings = value; emit inputSettingsChanged();
}
bool TerminalView::hasSelection() const {
    return m_selectionAnchor.line >= 0 && m_selectionEnd.line >= 0
           && (m_selectionAnchor.line != m_selectionEnd.line
               || m_selectionAnchor.column != m_selectionEnd.column);
}
void TerminalView::selectAll() {
    QMutexLocker locker(&m_terminalMutex);
    m_selectionAnchor = {0, 0};
    m_selectionEnd = {static_cast<int>(m_scrollback.size()) + m_rows - 1, m_columns};
    emit selectionChanged(); update();
}

void TerminalView::copySelection() {
    QMutexLocker locker(&m_terminalMutex);
    const QString text = selectedText();
    if (!text.isEmpty()) {
        QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
        if (QGuiApplication::clipboard()->supportsSelection())
            QGuiApplication::clipboard()->setText(text, QClipboard::Selection);
    }
}

void TerminalView::clearScrollback() {
    QMutexLocker locker(&m_terminalMutex);
    if (m_scrollback.empty()) {
        return;
    }
    m_scrollback.clear();
    m_scrollbackBytes = 0;
    m_scrollOffset = 0;
    m_selectionAnchor = {};
    m_selectionEnd = {};
    rebuildSearchMatchesLocked();
    emit scrollbackChanged();
    emit scrollOffsetChanged();
    emit searchMatchesChanged();
    update();
}

void TerminalView::findNext() {
    {
        QMutexLocker locker(&m_terminalMutex);
        if (m_searchMatches.empty()) {
            return;
        }
        m_currentSearchMatch =
            (m_currentSearchMatch + 1) % static_cast<int>(m_searchMatches.size());
        revealCurrentSearchMatchLocked();
    }
    emit searchMatchesChanged();
    emit scrollOffsetChanged();
    update();
}

void TerminalView::findPrevious() {
    {
        QMutexLocker locker(&m_terminalMutex);
        if (m_searchMatches.empty()) {
            return;
        }
        const int count = static_cast<int>(m_searchMatches.size());
        m_currentSearchMatch = m_currentSearchMatch <= 0 ? count - 1
                                                         : m_currentSearchMatch - 1;
        revealCurrentSearchMatchLocked();
    }
    emit searchMatchesChanged();
    emit scrollOffsetChanged();
    update();
}

void TerminalView::clearSearch() {
    if (m_searchText.isEmpty() && m_searchMatches.empty()) {
        return;
    }
    {
        QMutexLocker locker(&m_terminalMutex);
        m_searchText.clear();
        m_searchMatches.clear();
        m_currentSearchMatch = -1;
    }
    emit searchTextChanged();
    emit searchMatchesChanged();
    update();
}

void TerminalView::scrollToBottom() {
    setScrollOffset(0);
}

void TerminalView::paintContent(QPainter* painter, const QBitArray* dirtyRows) {
    ++m_contentPaintCount;
    painter->fillRect(boundingRect(), m_defaultBackground);
    painter->setRenderHint(QPainter::TextAntialiasing, m_textAntialiasing != QStringLiteral("aliased"));
    if (m_screen == nullptr) {
        return;
    }
    const QFont fonts[]{terminalFont(false, false), terminalFont(true, false),
        terminalFont(false, true), terminalFont(true, true)};
    int fontIndex = -1;

    const int historySize = static_cast<int>(m_scrollback.size());
    const int totalLines = historySize + m_rows;
    const int viewportStart = std::max(0, totalLines - m_rows - m_scrollOffset);

    for (int displayRow = 0; displayRow < m_rows; ++displayRow) {
        if (dirtyRows && !dirtyRows->testBit(displayRow)) continue;
        const int globalLine = viewportStart + displayRow;
        const CellLine line = lineAt(globalLine);
        const qreal y = m_verticalPadding + static_cast<qreal>(displayRow) * m_cellHeight;

        struct CellHighlight {
            QColor foreground;
            QColor background;
            bool bold = false;
            bool underline = false;
            bool active = false;
        };
        std::vector<CellHighlight> highlights(static_cast<std::size_t>(m_columns));
        if (m_syntaxHighlightEnabled) {
            QString plainLine;
            QVector<int> offsetToColumn;
            for (int column = 0;
                 column < m_columns && column < static_cast<int>(line.size());
                 ++column) {
                const auto& cell = line[static_cast<std::size_t>(column)];
                if (cell.width == 0) {
                    continue;
                }
                QString glyph = textForCell(cell);
                if (glyph.isEmpty()) {
                    glyph = QStringLiteral(" ");
                }
                plainLine += glyph;
                for (qsizetype index = 0; index < glyph.size(); ++index) {
                    offsetToColumn.push_back(column);
                }
            }

            const auto spans = m_highlightEngine.highlight(plainLine, m_highlightRules);
            for (const auto& span : spans) {
                const qsizetype end = std::min(span.start + span.length,
                                               offsetToColumn.size());
                for (qsizetype offset = std::max<qsizetype>(0, span.start);
                     offset < end;
                     ++offset) {
                    const int column = offsetToColumn.at(offset);
                    auto& highlight = highlights[static_cast<std::size_t>(column)];
                    highlight.foreground = span.foreground;
                    highlight.background = span.background;
                    highlight.bold = span.bold;
                    highlight.underline = span.underline;
                    highlight.active = true;
                }
            }
        }

        for (int column = 0; column < m_columns && column < static_cast<int>(line.size());
             ++column) {
            const auto& cell = line[static_cast<std::size_t>(column)];
            if (cell.width == 0 && cell.chars[0] == 0) {
                continue;
            }

            QColor foreground = terminalColor(cell.fg, m_defaultForeground);
            QColor background = terminalColor(cell.bg, m_defaultBackground);
            if (cell.attrs.reverse != 0U) {
                std::swap(foreground, background);
            }
            if (cell.attrs.conceal != 0U) {
                foreground = background;
            }
            const auto& highlight = highlights[static_cast<std::size_t>(column)];
            if (highlight.active) {
                if (highlight.foreground.isValid() && highlight.foreground.alpha() > 0) {
                    foreground = highlight.foreground;
                }
                if (highlight.background.isValid() && highlight.background.alpha() > 0) {
                    background = highlight.background;
                }
            }
            const int searchState = searchStateForCell(globalLine, column);
            if (searchState == 1) {
                background = QColor(QStringLiteral("#3c3420"));
            } else if (searchState == 2) {
                background = QColor(QStringLiteral("#705922"));
            }
            if (cellIsSelected(globalLine, column)) {
                background = m_selectionColor;
            }

            const int cellSpan = std::max(1, static_cast<int>(cell.width));
            const qreal x = m_horizontalPadding + static_cast<qreal>(column) * m_cellWidth;
            const QRectF cellRect(x,
                                  y,
                                  m_cellWidth * static_cast<qreal>(cellSpan),
                                  m_cellHeight);
            if (background != m_defaultBackground) {
                painter->fillRect(cellRect, background);
            }

            const QString glyph = textForCell(cell);
            if (glyph.isEmpty()) {
                continue;
            }
            const int nextFont = (cell.attrs.bold != 0U || highlight.bold ? 1 : 0)
                | (cell.attrs.italic != 0U ? 2 : 0);
            if (fontIndex != nextFont) { painter->setFont(fonts[nextFont]); fontIndex = nextFont; }
            painter->setPen(foreground);
            painter->drawText(QPointF(x, y + m_ascent), glyph);

            if (cell.attrs.underline != 0U || highlight.underline) {
                const qreal underlineY = y + m_ascent + 1.0;
                painter->drawLine(QPointF(x, underlineY),
                                  QPointF(cellRect.right(), underlineY));
            }
            if (cell.attrs.strike != 0U) {
                const qreal strikeY = y + m_ascent * 0.58;
                painter->drawLine(QPointF(x, strikeY), QPointF(cellRect.right(), strikeY));
            }
        }
    }

}

void TerminalView::paint(QPainter* painter) {
    QMutexLocker locker(&m_terminalMutex);
    // A scene-graph update queued before hiding can still call paint once.
    if (!isVisible() || (window() && (!window()->isVisible() || window()->visibility() == QWindow::Minimized))) {
        m_contentCache = QImage();
        m_contentDirty = true;
        painter->fillRect(boundingRect(), m_defaultBackground);
        return;
    }
    const qreal ratio = painter->device()->devicePixelRatioF();
    const qreal pixelWidth = std::ceil(width() * ratio), pixelHeight = std::ceil(height() * ratio);
    // Never trade the bounded terminal for an unbounded high-DPI raster cache.
    const bool cacheable = pixelWidth > 0 && pixelHeight > 0
        && pixelWidth <= 16384 && pixelHeight <= 16384
        && pixelWidth * pixelHeight * 4 <= 16 * 1024 * 1024;
    if (cacheable) {
        const QSize size{int(pixelWidth), int(pixelHeight)};
        if (m_contentCache.size() != size || m_contentCache.devicePixelRatio() != ratio) {
            m_contentCache = QImage(size, QImage::Format_ARGB32_Premultiplied);
            m_contentCache.setDevicePixelRatio(ratio);
            m_contentCache.fill(m_defaultBackground);
            m_contentDirty = true;
        }
        if (!m_contentCache.isNull()) {
            if (m_contentDirty || m_dirtyRows.count(true) > 0) {
                QPainter cached(&m_contentCache);
                if (m_contentDirty) paintContent(&cached);
                else {
                    QRegion changed;
                    for (int row = 0; row < m_dirtyRows.size(); ++row) {
                        if (m_dirtyRows.testBit(row)) changed += QRectF(0,
                            m_verticalPadding + row * m_cellHeight, width(), m_cellHeight).toAlignedRect();
                    }
                    cached.setClipRegion(changed);
                    paintContent(&cached, &m_dirtyRows);
                }
                m_contentDirty = false;
                m_dirtyRows.fill(false);
            }
            painter->drawImage(QPointF(0, 0), m_contentCache);
        } else paintContent(painter);
    } else {
        m_contentCache = QImage();
        paintContent(painter);
    }
}

void TerminalView::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    recalculateGrid();
}

void TerminalView::keyPressEvent(QKeyEvent* event) {
    const auto modifiers = event->modifiers();
    const bool control = modifiers.testFlag(Qt::ControlModifier);
    const auto matches = [this, event](const QString& action) {
        const QString configured = m_keyBindings.value(action).toString();
        return !configured.isEmpty() && QKeySequence(event->keyCombination())
               == QKeySequence::fromString(configured, QKeySequence::PortableText);
    };
    const bool plainControlC = event->key() == Qt::Key_C && modifiers == Qt::ControlModifier;
    if (matches(QStringLiteral("copy")) && (!plainControlC || hasSelection())) {
        copySelection();
        event->accept();
        return;
    }
    if (matches(QStringLiteral("paste"))) {
        pasteClipboard();
        event->accept();
        return;
    }
    if (matches(QStringLiteral("pasteSelection"))) {
        pasteSelection(); event->accept(); return;
    }
    if (matches(QStringLiteral("zoomIn"))
        || (m_keyBindings.value(QStringLiteral("zoomIn")) == QStringLiteral("Ctrl++")
            && control && (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal))) {
        emit zoomRequested(1);
        event->accept();
        return;
    }
    if (matches(QStringLiteral("zoomOut"))) {
        emit zoomRequested(-1);
        event->accept();
        return;
    }
    if (!isRunning()) {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            restartSession();
            event->accept();
        } else {
            event->ignore();
        }
        return;
    }

    if (!m_pendingLocalSshCommand.isEmpty()) {
        if (event->key() == Qt::Key_Escape || plainControlC) cancelPendingLocalSsh();
        m_consumedLocalSshKeys.insert(event->key());
        event->accept();
        return;
    }
    if (m_localSshSuggestionsActive && !m_localSshQuery.isEmpty()) {
        const bool plain = modifiers == Qt::NoModifier;
        const bool backwardsTab = event->key() == Qt::Key_Backtab
            || (event->key() == Qt::Key_Tab && modifiers == Qt::ShiftModifier);
        bool handled = true;
        if ((plain && event->key() == Qt::Key_Up) || backwardsTab) emit localSshNavigate(-1);
        else if (plain && (event->key() == Qt::Key_Down || event->key() == Qt::Key_Tab)) emit localSshNavigate(1);
        else if (plain && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
                 && m_localSshSelectionReady) emit localSshAccept();
        else if (plain && event->key() == Qt::Key_Escape) dismissLocalSsh();
        else handled = false;
        if (handled) {
            m_consumedLocalSshKeys.insert(event->key());
            event->accept();
            return;
        }
    }
    // A keystroke ends a bound credential's window unless ssh is waiting for
    // the user. Noted before the Enter below names a typed ssh's target, so
    // the Enter that launches it never closes the new window.
    if (m_credential) [[unlikely]] {
        const int key = event->key();
        if (key != Qt::Key_Shift && key != Qt::Key_Control && key != Qt::Key_Alt && key != Qt::Key_Meta
            && key != Qt::Key_AltGr && key != Qt::Key_CapsLock)
            noteCredentialInput();
    }
    // Suggestions describe rendered, settled input. Invalidate immediately on
    // typing so a fast Enter never selects a host from an older visible line.
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && modifiers == Qt::NoModifier && !m_localSshQuery.isEmpty()) {
        QVariantMap requested;
        {
            QMutexLocker locker(&m_terminalMutex);
            const auto input = localInputTextLocked();
            if (!input.isNull() && input == m_localSshQuery.value(QStringLiteral("command")).toString())
                requested = LocalSshCommand::parse(input);
        }
        if (!requested.isEmpty())
            setCurrentSshConnection(sshConnectionSummary(requested, QStringLiteral("local")));
    }
    ++m_localSshRefreshSerial;
    clearLocalSshQuery();
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || plainControlC)
        m_localPromptActive = false;

    setScrollOffset(0);
    noteCursorActivity();
    const VTermModifier terminalModifiers = modifiersFor(modifiers);
    if (m_win32Input.enabled()) {
        m_process.write(Win32Input::encode(*event, true));
        event->accept();
        return;
    }
    VTermKey key = VTERM_KEY_NONE;
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        key = VTERM_KEY_ENTER;
        break;
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
        key = VTERM_KEY_TAB;
        break;
    case Qt::Key_Backspace:
        sendBackspace(modifiers);
        event->accept();
        return;
    case Qt::Key_Escape:
        key = VTERM_KEY_ESCAPE;
        break;
    case Qt::Key_Up:
        key = VTERM_KEY_UP;
        break;
    case Qt::Key_Down:
        key = VTERM_KEY_DOWN;
        break;
    case Qt::Key_Left:
        key = VTERM_KEY_LEFT;
        break;
    case Qt::Key_Right:
        key = VTERM_KEY_RIGHT;
        break;
    case Qt::Key_Insert:
        key = VTERM_KEY_INS;
        break;
    case Qt::Key_Delete:
        key = VTERM_KEY_DEL;
        break;
    case Qt::Key_Home:
        key = VTERM_KEY_HOME;
        break;
    case Qt::Key_End:
        key = VTERM_KEY_END;
        break;
    case Qt::Key_PageUp:
        key = VTERM_KEY_PAGEUP;
        break;
    case Qt::Key_PageDown:
        key = VTERM_KEY_PAGEDOWN;
        break;
    default:
        break;
    }

    if (event->key() >= Qt::Key_F1 && event->key() <= Qt::Key_F35) {
        const int functionNumber = event->key() - Qt::Key_F1 + 1;
        key = static_cast<VTermKey>(VTERM_KEY_FUNCTION(functionNumber));
    }

    if (key != VTERM_KEY_NONE) {
        sendKey(key, terminalModifiers);
        event->accept();
        return;
    }

    if (control && event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
        const uint codepoint = static_cast<uint>('a' + event->key() - Qt::Key_A);
        sendCodepoint(codepoint, terminalModifiers);
        event->accept();
        return;
    }
    if (control && event->key() == Qt::Key_Space) {
        sendCodepoint(0U, terminalModifiers);
        event->accept();
        return;
    }

    const auto codepoints = event->text().toUcs4();
    if (!codepoints.isEmpty()) {
        QMutexLocker locker(&m_terminalMutex);
        for (const uint codepoint : codepoints) {
            vterm_keyboard_unichar(m_terminal, codepoint, terminalModifiers);
        }
        event->accept();
        return;
    }

    event->ignore();
}

void TerminalView::focusInEvent(QFocusEvent* event) {
    QQuickPaintedItem::focusInEvent(event);
    noteCursorActivity();
    QMutexLocker locker(&m_terminalMutex);
    if (m_terminal != nullptr) {
        vterm_state_focus_in(vterm_obtain_state(m_terminal));
    }
}

void TerminalView::focusOutEvent(QFocusEvent* event) {
    QQuickPaintedItem::focusOutEvent(event);
    QMutexLocker locker(&m_terminalMutex);
    if (m_terminal != nullptr) {
        vterm_state_focus_out(vterm_obtain_state(m_terminal));
    }
}

void TerminalView::mousePressEvent(QMouseEvent* event) {
    forceActiveFocus();
    if (event->button() == Qt::RightButton
        && (m_mouseMode == VTERM_PROP_MOUSE_NONE || event->modifiers().testFlag(Qt::ShiftModifier))) {
        emit contextMenuRequested(event->position());
        event->accept(); return;
    }
    if (event->button() == Qt::MiddleButton && m_mouseMode == VTERM_PROP_MOUSE_NONE) {
        pasteSelection();
        event->accept();
        return;
    }

    QMutexLocker locker(&m_terminalMutex);
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE
        && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        sendMousePosition(event->position(), event->modifiers());
        const int button = event->button() == Qt::LeftButton
                               ? 1
                               : event->button() == Qt::MiddleButton ? 2 : 3;
        vterm_mouse_button(m_terminal, button, true, modifiersFor(event->modifiers()));
        m_terminalMousePressed = true;
    } else if (event->button() == Qt::LeftButton) {
        m_selectionAnchor = textPointAt(event->position());
        m_selectionEnd = m_selectionAnchor;
        m_selecting = true;
        emit selectionChanged();
        update();
    }
    event->accept();
}

void TerminalView::mouseMoveEvent(QMouseEvent* event) {
    QMutexLocker locker(&m_terminalMutex);
    if (m_terminalMousePressed && m_mouseMode >= VTERM_PROP_MOUSE_DRAG) {
        sendMousePosition(event->position(), event->modifiers());
    } else if (m_selecting) {
        m_selectionEnd = textPointAt(event->position());
        update();
    } else if (m_mouseMode == VTERM_PROP_MOUSE_MOVE
               && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        sendMousePosition(event->position(), event->modifiers());
    }
    event->accept();
}

void TerminalView::mouseReleaseEvent(QMouseEvent* event) {
    QMutexLocker locker(&m_terminalMutex);
    if (m_terminalMousePressed) {
        sendMousePosition(event->position(), event->modifiers());
        const int button = event->button() == Qt::LeftButton
                               ? 1
                               : event->button() == Qt::MiddleButton ? 2 : 3;
        vterm_mouse_button(m_terminal, button, false, modifiersFor(event->modifiers()));
        m_terminalMousePressed = false;
    }
    if (m_selecting) {
        m_selectionEnd = textPointAt(event->position());
        m_selecting = false;
        if (m_selectionAnchor.line == m_selectionEnd.line
            && m_selectionAnchor.column == m_selectionEnd.column) {
            m_selectionAnchor = {};
            m_selectionEnd = {};
        }
        update();
        emit selectionChanged();
        if (m_copyOnSelect && hasSelection()) {
            const QString text = selectedText();
            if (!text.isEmpty()) {
                QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
                if (QGuiApplication::clipboard()->supportsSelection())
                    QGuiApplication::clipboard()->setText(text, QClipboard::Selection);
            }
        }
    }
    event->accept();
}

void TerminalView::wheelEvent(QWheelEvent* event) {
    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        m_zoomWheelRemainder += event->pixelDelta().y() != 0
                               ? event->pixelDelta().y() / 40.0 : event->angleDelta().y() / 120.0;
        const int zoomSteps = static_cast<int>(m_zoomWheelRemainder);
        if (zoomSteps != 0) { emit zoomRequested(zoomSteps); m_zoomWheelRemainder -= zoomSteps; }
        event->accept(); return;
    }
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0) {
        event->ignore();
        return;
    }
    if (m_mouseMode != VTERM_PROP_MOUSE_NONE
        && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        QMutexLocker locker(&m_terminalMutex);
        sendMousePosition(event->position(), event->modifiers());
        const int button = steps > 0 ? 4 : 5;
        for (int index = 0; index < std::abs(steps); ++index) {
            vterm_mouse_button(m_terminal, button, true, modifiersFor(event->modifiers()));
            vterm_mouse_button(m_terminal, button, false, modifiersFor(event->modifiers()));
        }
    } else {
        setScrollOffset(m_scrollOffset + steps * 3);
    }
    event->accept();
}

void TerminalView::initializeTerminal() {
    QMutexLocker locker(&m_terminalMutex);
    m_terminal = vterm_new(m_rows, m_columns);
    vterm_set_utf8(m_terminal, 1);
    vterm_output_set_callback(m_terminal, &TerminalView::outputCallback, this);

    m_screen = vterm_obtain_screen(m_terminal);
    m_screenCallbacks.damage = &TerminalView::damageCallback;
    m_screenCallbacks.moverect = &TerminalView::moveRectCallback;
    m_screenCallbacks.movecursor = &TerminalView::moveCursorCallback;
    m_screenCallbacks.settermprop = &TerminalView::setTermPropertyCallback;
    m_screenCallbacks.bell = &TerminalView::bellCallback;
    m_screenCallbacks.resize = &TerminalView::resizeCallback;
    m_screenCallbacks.sb_pushline = &TerminalView::scrollbackPushCallback;
    m_screenCallbacks.sb_popline = &TerminalView::scrollbackPopCallback;
    m_screenCallbacks.sb_clear = &TerminalView::scrollbackClearCallback;
    vterm_screen_set_callbacks(m_screen, &m_screenCallbacks, this);
    m_stateFallbacks.osc = &TerminalView::oscFallbackCallback;
    vterm_screen_set_unrecognised_fallbacks(m_screen, &m_stateFallbacks, this);
    vterm_screen_enable_altscreen(m_screen, 1);
    vterm_screen_enable_reflow(m_screen, true);
    vterm_screen_set_damage_merge(m_screen, VTERM_DAMAGE_SCROLL);

    VTermColor foreground{};
    VTermColor background{};
    vterm_color_rgb(&foreground,
                    static_cast<uint8_t>(m_defaultForeground.red()),
                    static_cast<uint8_t>(m_defaultForeground.green()),
                    static_cast<uint8_t>(m_defaultForeground.blue()));
    vterm_color_rgb(&background,
                    static_cast<uint8_t>(m_defaultBackground.red()),
                    static_cast<uint8_t>(m_defaultBackground.green()),
                    static_cast<uint8_t>(m_defaultBackground.blue()));
    vterm_screen_set_default_colors(m_screen, &foreground, &background);
    vterm_screen_reset(m_screen, 1);
}

void TerminalView::resetTerminal() {
    stopPasswordBroker();
    setCurrentSshConnection({});
    QMutexLocker locker(&m_terminalMutex);
    cancelPendingLocalSsh();
    clearLocalSshQuery();
    m_localPromptActive = false;
    m_alternateScreen = false;
    m_localInputColumn = -1;
    m_integratedShellKind.clear();
    m_shellMarkerFragment.clear();
    m_consumedLocalSshKeys.clear();
    m_dismissedLocalSshCommand.clear();
    m_lastLocalInput.clear();
    ++m_localSshRefreshSerial;
    m_performanceFixtureComplete = false;
    m_performanceTail.clear();
    m_scrollback.clear();
    m_scrollbackBytes = 0;
    m_scrollOffset = 0;
    m_selectionAnchor = {};
    m_selectionEnd = {};
    m_cursorPosition = {0, 0};
    m_cursorVisible = true;
    noteCursorGeometry();
    m_titleFragment.clear();
    m_pendingPaste.clear();
    m_searchText.clear();
    m_searchMatches.clear();
    m_currentSearchMatch = -1;
    vterm_screen_reset(m_screen, 1);
    emit scrollbackChanged();
    emit scrollOffsetChanged();
    emit searchTextChanged();
    emit searchMatchesChanged();
    update();
}

void TerminalView::recalculateMetrics() {
    const QFont font = terminalFont();
    const QFontMetricsF metrics(font);
    m_cellWidth = std::max(4.0, std::ceil(metrics.horizontalAdvance(u'M')));
    m_cellHeight = std::max(8.0, std::ceil(metrics.height() + 1.0));
    m_ascent = std::ceil(metrics.ascent());
    recalculateGrid();
    noteCursorGeometry();
}

void TerminalView::recalculateGrid() {
    if (width() <= m_horizontalPadding * 2.0 || height() <= m_verticalPadding * 2.0) {
        return;
    }
    const int columns = std::clamp(
                                 static_cast<int>(std::floor(
                                     (width() - m_horizontalPadding * 2.0) / m_cellWidth)), kMinimumColumns, 1000);
    const int rows = std::clamp(
                              static_cast<int>(std::floor(
                                  (height() - m_verticalPadding * 2.0) / m_cellHeight)), kMinimumRows, 500);
    if (columns == m_columns && rows == m_rows) {
        return;
    }
    // An empty authenticated prompt can be relocated after reflow. Any typed
    // or ambiguous input must wait for the next prompt instead of guessing its
    // new boundary. This keeps maximize-before-typing working normally.
    bool retainEmptyPrompt = false;
    {
        QMutexLocker locker(&m_terminalMutex);
        const auto input = localInputTextLocked();
        retainEmptyPrompt = !input.isNull() && input.isEmpty() && m_pendingLocalSshCommand.isEmpty();
    }
    m_localPromptActive = false;
    clearLocalSshQuery();

    const int oldHistorySize = scrollbackLines(), oldScrollOffset = m_scrollOffset;
    {
        QMutexLocker locker(&m_terminalMutex);
        m_columns = columns;
        m_rows = rows;
        vterm_set_size(m_terminal, m_rows, m_columns);
        vterm_screen_flush_damage(m_screen);
        if (retainEmptyPrompt && !m_alternateScreen) {
            VTermPos cursor{};
            vterm_state_get_cursorpos(vterm_obtain_state(m_terminal), &cursor);
            m_localInputRow = m_historyDropped + quint64(m_scrollback.size()) + quint64(cursor.row);
            m_localInputColumn = cursor.col;
            ++m_localPromptGeneration;
            m_localPromptActive = true;
        }
        rebuildSearchMatchesLocked();
    }
    if (oldHistorySize != scrollbackLines()) emit scrollbackChanged();
    if (oldScrollOffset != m_scrollOffset) emit scrollOffsetChanged();
    if (!m_searchText.isEmpty()) emit searchMatchesChanged();
    m_process.resize(m_columns, m_rows);
    emit gridSizeChanged();
    update();
}

void TerminalView::handleProcessData(const QByteArray& data) {
    m_win32Input.feed(QByteArrayView(data));
    if (m_performanceProbe && !m_performanceFixtureComplete) {
        const QByteArray marker = QByteArrayLiteral("FLOOD_END");
        m_performanceFixtureComplete = data.contains(marker)
            || (m_performanceTail + data.left(marker.size())).contains(marker);
        m_performanceTail = data.right(marker.size() - 1);
    }
    emit rawOutputReceived(data);
    if (m_logger.isOpen()) {
        QString error;
        if (!m_logger.writeOutput(QByteArrayView(data), &error)) {
            setLogError(error);
            stopLog();
        }
    }

    const bool searchWasActive = !m_searchText.isEmpty();
    const int oldHistorySize = scrollbackLines(), oldScrollOffset = m_scrollOffset;
    {
        QMutexLocker locker(&m_terminalMutex);
        m_processingOutput = true;
        vterm_input_write(m_terminal,
                          data.constData(),
                          static_cast<std::size_t>(data.size()));
        vterm_screen_flush_damage(m_screen);
        m_processingOutput = false;
        if (searchWasActive) {
            rebuildSearchMatchesLocked(true);
        }
    }
    if (m_currentSshConnectionNotificationPending) {
        m_currentSshConnectionNotificationPending = false;
        emit currentSshConnectionChanged();
    }
    completePendingLocalSsh();
    if (!m_localPromptActive || m_alternateScreen) clearLocalSshQuery();
    const auto refresh = ++m_localSshRefreshSerial;
    QTimer::singleShot(45, this, [this, refresh] {
        if (refresh == m_localSshRefreshSerial) refreshLocalSshQuery();
    });
    if (oldHistorySize != scrollbackLines()) emit scrollbackChanged();
    if (oldScrollOffset != m_scrollOffset) {
        emit scrollOffsetChanged();
        noteCursorGeometry();
    }
    if (searchWasActive) {
        emit searchMatchesChanged();
    }
    if (m_statusText.startsWith(QStringLiteral("Connecting to "))) {
            setStatusText(QStringLiteral("SSH process running · verify authentication in the terminal"));
    } else if (m_statusText == QStringLiteral("Starting local shell…")
               || m_statusText == QStringLiteral("Starting local profile…")) {
        setStatusText(QStringLiteral("Local ready"));
    }
    if (m_credential) [[unlikely]] answerCredentialPrompt(data);
    // The cursor is an overlay. Upload the terminal texture only when cells changed.
    if (searchWasActive || oldScrollOffset != m_scrollOffset) update();
    else if (m_contentDirty || m_dirtyRows.count(true) > 0) requestSceneUpdate();
}

void TerminalView::handleProcessFinished(int exitCode) {
    stopPasswordBroker();
    releaseCredential();
    setCurrentSshConnection({});
    cancelPendingLocalSsh();
    m_localPromptActive = false;
    clearLocalSshQuery();
    m_broadcastArmed = false; ++m_broadcastGeneration; emit broadcastStateChanged();
    stopLog();
    setStatusText(exitCode >= 0 ? QStringLiteral("Session ended (exit %1) — press Enter to restart")
                                     .arg(exitCode)
                               : QStringLiteral("Session ended — press Enter to restart"));
    update();
}

void TerminalView::setTitle(const QString& titleValue) {
    if (m_title == titleValue) {
        return;
    }
    m_title = titleValue;
    emit titleChanged();
}

void TerminalView::setActiveTarget(const QString& target) {
    if (m_activeTarget == target) {
        return;
    }
    m_activeTarget = target;
    emit activeTargetChanged();
}

void TerminalView::setStatusText(const QString& status) {
    if (m_statusText == status) {
        return;
    }
    m_statusText = status;
    emit statusTextChanged();
}

void TerminalView::setLogError(const QString& error) {
    if (m_logError == error) {
        return;
    }
    m_logError = error;
    emit logErrorChanged();
}

void TerminalView::startLog(const QString& sessionName) {
    stopLog();
    setLogError(QString{});

    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        documents = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    }

    bitshell::core::LogOptions options;
    options.rootDirectory = m_logRootDirectory.isEmpty()
                                ? QDir(documents).filePath(QStringLiteral("8bitShell/Logs"))
                                : m_logRootDirectory;
    options.directoryTemplate = m_logDirectoryTemplate;
    options.fileNameTemplate = m_logFileNameTemplate;
    options.existingFileMode = m_logAppend ? bitshell::core::ExistingLogMode::Append
                                           : bitshell::core::ExistingLogMode::Truncate;
    options.timestampMode = m_logTimestamps ? bitshell::core::LogTimestampMode::EachLine
                                            : bitshell::core::LogTimestampMode::None;
    options.stripAnsi = true;
    options.rotateAfterBytes = static_cast<qint64>(m_logRotationMegabytes) * 1024 * 1024;
    options.retainedRotations = m_logRetainedFiles;

    bitshell::core::LogContext context;
    context.session = sessionName;
    context.host = sessionName;
    context.user = qEnvironmentVariable("USER");

    QString error;
    if (!m_logger.start(options, context, &error)) {
        setLogError(error);
        return;
    }
    m_logFilePath = m_logger.filePath();
    emit logFilePathChanged();
}

void TerminalView::stopLog() {
    m_logger.stop();
    if (!m_logFilePath.isEmpty()) {
        m_logFilePath.clear();
        emit logFilePathChanged();
    }
}

void TerminalView::restartLogForOptionChange() {
    if (m_logEnabled && isRunning()) {
        startLog(m_activeTarget);
    }
}

void TerminalView::setScrollOffset(int offset) {
    const int bounded = std::clamp(offset, 0, static_cast<int>(m_scrollback.size()));
    if (m_scrollOffset == bounded) {
        return;
    }
    m_scrollOffset = bounded;
    if (m_scrollOffset != 0) clearLocalSshQuery();
    else refreshLocalSshQuery();
    noteCursorGeometry();
    emit scrollOffsetChanged();
    update();
}

bool TerminalView::launchProcess(const QString& program,
                                 const QStringList& arguments,
                                 const QString& targetLabel,
                                 const QString& startingStatus, bool managedSsh,
                                 bool interactiveShell, const QString& workingDirectory,
                                 const QProcessEnvironment& environmentOverrides) {
    m_process.stop();
    m_win32Input.reset();
    m_managedSsh = managedSsh;
    m_broadcastArmed = false; ++m_broadcastGeneration; emit broadcastStateChanged();
    m_process.setEraseCharacter(m_backspaceMode == QStringLiteral("bs") ? '\x08' : '\x7f');
    m_process.setInteractiveShell(interactiveShell);
    stopLog();
    resetTerminal();
    m_lastInteractiveShell = interactiveShell;
    m_shellIntegrationToken = !managedSsh && interactiveShell
        ? QUuid::createUuid().toString(QUuid::Id128) : QString{};
    m_process.setShellIntegrationToken(m_shellIntegrationToken);

    QString error;
    if (!m_process.start(program, arguments, m_columns, m_rows, &error, workingDirectory, environmentOverrides)) {
        setStatusText(QStringLiteral("Could not start session: %1").arg(error));
        return false;
    }

    m_lastProgram = program;
    m_lastArguments = arguments;
    m_lastTargetLabel = targetLabel;
    m_lastStartingStatus = startingStatus;
    m_lastInteractiveShell = interactiveShell;
    m_lastWorkingDirectory = workingDirectory;
    setActiveTarget(targetLabel);
    setStatusText(startingStatus);
    setTitle(QFileInfo(program).fileName());
    if (m_logEnabled) {
        startLog(targetLabel);
    }
    forceActiveFocus();
    update();
    return true;
}

void TerminalView::rebuildSearchMatchesLocked(bool incremental) {
    TextPoint previous{-1, 0};
    if (incremental && m_currentSearchMatch >= 0 && m_currentSearchMatch < int(m_searchMatches.size()))
        previous = m_searchMatches[std::size_t(m_currentSearchMatch)].start;
    if (!incremental) {
        m_searchMatches.clear();
        m_searchedHistoryLines = 0;
        m_historySearchMatchCount = 0;
        m_searchHistoryDropped = m_historyDropped;
    } else {
        // Scrollback lines are immutable. Retain their matches, discard only the
        // previous live-screen suffix, and account for bounded-history eviction.
        m_searchMatches.resize(std::min(m_historySearchMatchCount, m_searchMatches.size()));
        const int removed = int(std::min<quint64>(m_historyDropped - m_searchHistoryDropped, quint64(INT_MAX)));
        previous.line -= removed;
        const auto first = std::lower_bound(m_searchMatches.begin(), m_searchMatches.end(), removed,
            [](const TextRange& match, int line) { return match.start.line < line; });
        m_searchMatches.erase(m_searchMatches.begin(), first);
        for (auto& match : m_searchMatches) { match.start.line -= removed; match.end.line -= removed; }
        m_searchedHistoryLines = std::clamp(m_searchedHistoryLines - removed, 0, scrollbackLines());
        const auto last = std::lower_bound(m_searchMatches.begin(), m_searchMatches.end(), m_searchedHistoryLines,
            [](const TextRange& match, int line) { return match.start.line < line; });
        m_searchMatches.erase(last, m_searchMatches.end());
        m_searchHistoryDropped = m_historyDropped;
    }
    m_currentSearchMatch = -1;
    if (m_searchText.isEmpty()) {
        m_searchMatches.clear();
        m_historySearchMatchCount = 0;
        m_searchedHistoryLines = 0;
        return;
    }

    const int totalLines = static_cast<int>(m_scrollback.size()) + m_rows;
    for (int lineIndex = m_searchedHistoryLines; lineIndex < totalLines; ++lineIndex) {
        if (lineIndex == scrollbackLines()) m_historySearchMatchCount = m_searchMatches.size();
        const CellLine line = lineAt(lineIndex);
        QString plainLine;
        QVector<int> offsetToColumn;
        for (int column = 0;
             column < m_columns && column < static_cast<int>(line.size());
             ++column) {
            const auto& cell = line[static_cast<std::size_t>(column)];
            if (cell.width == 0) {
                continue;
            }
            QString glyph = textForCell(cell);
            if (glyph.isEmpty()) {
                glyph = QStringLiteral(" ");
            }
            plainLine += glyph;
            for (qsizetype offset = 0; offset < glyph.size(); ++offset) {
                offsetToColumn.push_back(column);
            }
        }

        qsizetype from = 0;
        while (from < plainLine.size()) {
            const qsizetype match = plainLine.indexOf(m_searchText, from, Qt::CaseInsensitive);
            if (match < 0) {
                break;
            }
            const qsizetype lastOffset = match + m_searchText.size() - 1;
            if (match < offsetToColumn.size() && lastOffset < offsetToColumn.size()) {
                m_searchMatches.push_back(TextRange{
                    .start = TextPoint{lineIndex, offsetToColumn.at(match)},
                    .end = TextPoint{lineIndex, offsetToColumn.at(lastOffset)},
                });
            }
            from = match + std::max<qsizetype>(1, m_searchText.size());
        }
    }
    m_searchedHistoryLines = scrollbackLines();

    if (!m_searchMatches.empty()) {
        m_currentSearchMatch = 0;
        if (previous.line >= 0) {
            const auto found = std::lower_bound(m_searchMatches.begin(), m_searchMatches.end(), previous,
                [](const TextRange& match, const TextPoint& point) { return pointBefore(match.start, point); });
            if (found != m_searchMatches.end() && found->start.line == previous.line && found->start.column == previous.column)
                m_currentSearchMatch = int(std::distance(m_searchMatches.begin(), found));
        }
    }
}

void TerminalView::revealCurrentSearchMatchLocked() {
    if (m_currentSearchMatch < 0
        || m_currentSearchMatch >= static_cast<int>(m_searchMatches.size())) {
        return;
    }
    const int historySize = static_cast<int>(m_scrollback.size());
    const int matchLine = m_searchMatches[static_cast<std::size_t>(m_currentSearchMatch)].start.line;
    const int desiredStart = std::clamp(matchLine - m_rows / 2, 0, historySize);
    m_scrollOffset = historySize - desiredStart;
}

TerminalView::CellLine TerminalView::lineAt(int globalLine) const {
    const int historySize = static_cast<int>(m_scrollback.size());
    if (globalLine >= 0 && globalLine < historySize) {
        return m_scrollback[static_cast<std::size_t>(globalLine)].decoded();
    }

    CellLine line(static_cast<std::size_t>(m_columns));
    const int screenRow = globalLine - historySize;
    if (screenRow < 0 || screenRow >= m_rows) {
        return line;
    }
    for (int column = 0; column < m_columns; ++column) {
        vterm_screen_get_cell(m_screen,
                              VTermPos{screenRow, column},
                              &line[static_cast<std::size_t>(column)]);
    }
    return line;
}

QString TerminalView::textForCell(const VTermScreenCell& cell) const {
    if (cell.chars[0] == 0) return {};
    if (cell.chars[1] == 0 && cell.chars[0] < 0x10000U
        && (cell.chars[0] < 0xd800U || cell.chars[0] > 0xdfffU))
        return QString(QChar(ushort(cell.chars[0])));
    char32_t codepoints[VTERM_MAX_CHARS_PER_CELL];
    qsizetype count = 0;
    for (const uint32_t codepoint : cell.chars) {
        if (codepoint == 0U) {
            break;
        }
        codepoints[count++] = static_cast<char32_t>(codepoint);
    }
    return QString::fromUcs4(codepoints, count);
}

QString TerminalView::selectedText() const {
    if (m_selectionAnchor.line < 0 || m_selectionEnd.line < 0) {
        return {};
    }
    if (m_selectionAnchor.line == m_selectionEnd.line
        && m_selectionAnchor.column == m_selectionEnd.column) {
        return {};
    }

    TextPoint start = m_selectionAnchor;
    TextPoint end = m_selectionEnd;
    if (pointBefore(end, start)) {
        std::swap(start, end);
    }

    QString result;
    for (int lineIndex = start.line; lineIndex <= end.line; ++lineIndex) {
        const CellLine line = lineAt(lineIndex);
        const int firstColumn = lineIndex == start.line ? start.column : 0;
        const int lastColumn = lineIndex == end.line ? end.column : m_columns - 1;
        QString lineText;
        for (int column = std::max(0, firstColumn);
             column <= std::min(lastColumn, m_columns - 1);
             ++column) {
            const auto& cell = line[static_cast<std::size_t>(column)];
            if (cell.width == 0) {
                continue;
            }
            const QString glyph = textForCell(cell);
            lineText += glyph.isEmpty() ? QStringLiteral(" ") : glyph;
        }
        while (lineText.endsWith(u' ')) {
            lineText.chop(1);
        }
        result += lineText;
        if (lineIndex != end.line) {
            result += u'\n';
        }
    }
    return result;
}

TerminalView::TextPoint TerminalView::textPointAt(const QPointF& position) const {
    const int displayRow = std::clamp(
        static_cast<int>((position.y() - m_verticalPadding) / m_cellHeight),
        0,
        m_rows - 1);
    const int column = std::clamp(
        static_cast<int>((position.x() - m_horizontalPadding) / m_cellWidth),
        0,
        m_columns - 1);
    const int totalLines = static_cast<int>(m_scrollback.size()) + m_rows;
    const int viewportStart = std::max(0, totalLines - m_rows - m_scrollOffset);
    return {viewportStart + displayRow, column};
}

bool TerminalView::cellIsSelected(int globalLine, int column) const {
    if (m_selectionAnchor.line < 0 || m_selectionEnd.line < 0) {
        return false;
    }
    if (m_selectionAnchor.line == m_selectionEnd.line
        && m_selectionAnchor.column == m_selectionEnd.column) {
        return false;
    }
    TextPoint start = m_selectionAnchor;
    TextPoint end = m_selectionEnd;
    if (pointBefore(end, start)) {
        std::swap(start, end);
    }
    const TextPoint point{globalLine, column};
    return !pointBefore(point, start) && !pointBefore(end, point);
}

int TerminalView::searchStateForCell(int globalLine, int column) const {
    const TextPoint point{globalLine, column};
    const auto match = std::lower_bound(m_searchMatches.cbegin(), m_searchMatches.cend(), point,
        [](const TextRange& candidate, const TextPoint& value) { return pointBefore(candidate.end, value); });
    if (match != m_searchMatches.cend() && !pointBefore(point, match->start))
        return std::distance(m_searchMatches.cbegin(), match) == m_currentSearchMatch ? 2 : 1;
    return 0;
}

QColor TerminalView::terminalColor(const VTermColor& source, const QColor& fallback) const {
    VTermColor color = source;
    if (VTERM_COLOR_IS_DEFAULT_FG(&color) || VTERM_COLOR_IS_DEFAULT_BG(&color)) {
        return fallback;
    }
    vterm_screen_convert_color_to_rgb(m_screen, &color);
    return QColor(color.rgb.red, color.rgb.green, color.rgb.blue);
}

VTermModifier TerminalView::modifiersFor(Qt::KeyboardModifiers modifiers) const {
    int result = VTERM_MOD_NONE;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        result |= VTERM_MOD_SHIFT;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        result |= VTERM_MOD_ALT;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        result |= VTERM_MOD_CTRL;
    }
    return static_cast<VTermModifier>(result);
}

bool TerminalView::pointBefore(const TextPoint& left, const TextPoint& right) {
    return left.line < right.line || (left.line == right.line && left.column < right.column);
}

void TerminalView::sendKey(VTermKey key, VTermModifier modifiers) {
    QMutexLocker locker(&m_terminalMutex);
    vterm_keyboard_key(m_terminal, key, modifiers);
}

void TerminalView::sendCodepoint(uint codepoint, VTermModifier modifiers) {
    QMutexLocker locker(&m_terminalMutex);
    vterm_keyboard_unichar(m_terminal, codepoint, modifiers);
}

void TerminalView::sendBackspace(Qt::KeyboardModifiers modifiers) {
    m_process.write(backspaceSequence(m_backspaceMode, modifiers));
}

void TerminalView::sendMousePosition(const QPointF& position,
                                     Qt::KeyboardModifiers modifiers) {
    const int row = std::clamp(
        static_cast<int>((position.y() - m_verticalPadding) / m_cellHeight),
        0,
        m_rows - 1);
    const int column = std::clamp(
        static_cast<int>((position.x() - m_horizontalPadding) / m_cellWidth),
        0,
        m_columns - 1);
    vterm_mouse_move(m_terminal, row, column, modifiersFor(modifiers));
}

void TerminalView::keyReleaseEvent(QKeyEvent* event) {
    if (m_consumedLocalSshKeys.remove(event->key())) { event->accept(); return; }
    if (isRunning() && m_win32Input.enabled()) {
        m_process.write(Win32Input::encode(*event, false));
        event->accept();
    } else event->ignore();
}

void TerminalView::outputCallback(const char* bytes, size_t length, void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    terminal->m_process.write(
        QByteArray(bytes, static_cast<qsizetype>(length)));
}

int TerminalView::damageCallback(VTermRect rect, void* user) {
    static_cast<TerminalView*>(user)->damageRows(rect.start_row, rect.end_row);
    return 1;
}

int TerminalView::moveRectCallback(VTermRect destination, VTermRect source, void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    terminal->damageRows(std::min(destination.start_row, source.start_row), std::max(destination.end_row, source.end_row));
    return 1;
}

int TerminalView::moveCursorCallback(VTermPos position, VTermPos, int visible, void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    terminal->m_cursorPosition = position;
    terminal->m_cursorVisible = visible != 0;
    terminal->noteCursorGeometry();
    return 1;
}

int TerminalView::setTermPropertyCallback(VTermProp property, VTermValue* value, void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    switch (property) {
    case VTERM_PROP_ALTSCREEN:
        terminal->m_alternateScreen = value->boolean != 0;
        terminal->m_localPromptActive = false;
        break;
    case VTERM_PROP_CURSORVISIBLE:
        terminal->m_cursorVisible = value->boolean != 0;
        terminal->noteCursorGeometry();
        return 1;
    case VTERM_PROP_CURSORBLINK:
        terminal->m_cursorBlinkEnabled = value->boolean != 0;
        terminal->noteCursorGeometry();
        return 1;
    case VTERM_PROP_CURSORSHAPE:
        terminal->m_cursorShape = value->number;
        terminal->noteCursorGeometry();
        return 1;
    case VTERM_PROP_MOUSE:
        terminal->m_mouseMode = value->number;
        break;
    case VTERM_PROP_TITLE: {
        const auto fragment = value->string;
        if (fragment.initial) {
            terminal->m_titleFragment.clear();
        }
        const auto remaining = qsizetype(4096) - terminal->m_titleFragment.size();
        terminal->m_titleFragment.append(fragment.str, std::min(remaining, static_cast<qsizetype>(fragment.len)));
        if (fragment.final) {
            terminal->setTitle(QString::fromUtf8(terminal->m_titleFragment));
        }
        break;
    }
    default:
        break;
    }
    terminal->update();
    return 1;
}

int TerminalView::bellCallback(void* user) {
    emit static_cast<TerminalView*>(user)->bell();
    return 1;
}

int TerminalView::resizeCallback(int, int, void* user) {
    static_cast<TerminalView*>(user)->update();
    return 1;
}

int TerminalView::scrollbackPushCallback(int columns,
                                         const VTermScreenCell* cells,
                                         void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    ScrollbackLine line(columns, cells);
    terminal->m_scrollbackBytes += line.memoryBytes();
    terminal->m_scrollback.push_back(std::move(line));
    if (terminal->m_scrollOffset > 0) {
        ++terminal->m_scrollOffset;
    }
    while (static_cast<int>(terminal->m_scrollback.size()) > terminal->m_scrollbackLimit
           || terminal->m_scrollbackBytes > scrollbackMemoryBudget) {
        terminal->m_scrollbackBytes -= terminal->m_scrollback.front().memoryBytes();
        terminal->m_scrollback.pop_front();
        ++terminal->m_historyDropped;
        if (terminal->m_scrollOffset > 0) {
            --terminal->m_scrollOffset;
        }
        if (terminal->m_selectionAnchor.line >= 0) {
            --terminal->m_selectionAnchor.line;
            --terminal->m_selectionEnd.line;
        }
    }
    return 1;
}

int TerminalView::scrollbackPopCallback(int columns, VTermScreenCell* cells, void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    if (terminal->m_scrollback.empty()) {
        return 0;
    }
    CellLine line = terminal->m_scrollback.back().decoded();
    terminal->m_scrollbackBytes -= terminal->m_scrollback.back().memoryBytes();
    terminal->m_scrollback.pop_back();
    const int available = std::min(columns, static_cast<int>(line.size()));
    if (available > 0) {
        std::memcpy(cells,
                    line.data(),
                    static_cast<std::size_t>(available) * sizeof(VTermScreenCell));
    }
    if (available < columns) {
        VTermScreenCell blank{};
        // libvterm advances through restored history by cell.width. Zero-width
        // padding after a narrow row would make resize loop forever.
        blank.width = 1;
        vterm_state_get_default_colors(vterm_obtain_state(terminal->m_terminal), &blank.fg, &blank.bg);
        std::fill(cells + available, cells + columns, blank);
    }
    return 1;
}

int TerminalView::scrollbackClearCallback(void* user) {
    auto* terminal = static_cast<TerminalView*>(user);
    terminal->m_scrollback.clear();
    terminal->m_scrollbackBytes = 0;
    terminal->m_scrollOffset = 0;
    terminal->m_selectionAnchor = {};
    terminal->m_selectionEnd = {};
    terminal->m_searchMatches.clear();
    terminal->m_currentSearchMatch = -1;
    emit terminal->scrollbackChanged();
    emit terminal->scrollOffsetChanged();
    emit terminal->searchMatchesChanged();
    terminal->update();
    return 1;
}

} // namespace bitshell::terminal
