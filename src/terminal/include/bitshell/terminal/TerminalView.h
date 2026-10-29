#pragma once
#include <bitshell/terminal/Win32Input.h>

#include <QColor>
#include <QByteArray>
#include <QMutex>
#include <QImage>
#include <QFont>
#include <QBitArray>
#include <QPoint>
#include <QQuickPaintedItem>
#include <QString>
#include <QSet>
#include <QVariantList>
#include <QVariantMap>

#include <bitshell/core/SessionLogger.h>
#include <bitshell/core/HighlightEngine.h>
#include <bitshell/terminal/PtyProcess.h>
#include <bitshell/terminal/ScrollbackLine.h>

#include <deque>
#include <memory>
#include <vector>

#include <vterm.h>

class QFocusEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;
class QWheelEvent;

namespace bitshell::core { class PasswordBroker; }

namespace bitshell::terminal {

class TerminalView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool performanceProbe MEMBER m_performanceProbe)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(qreal fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY
                   fontPixelSizeChanged)
    Q_PROPERTY(QString textAntialiasing READ textAntialiasing WRITE setTextAntialiasing NOTIFY renderingChanged)
    Q_PROPERTY(QString fontHinting READ fontHinting WRITE setFontHinting NOTIFY renderingChanged)
    Q_PROPERTY(int columns READ columns NOTIFY gridSizeChanged)
    Q_PROPERTY(int rows READ rows NOTIFY gridSizeChanged)
    Q_PROPERTY(bool running READ isRunning NOTIFY runningChanged)
    Q_PROPERTY(bool remoteSession READ remoteSession NOTIFY broadcastStateChanged)
    Q_PROPERTY(QVariantMap currentSshConnection READ currentSshConnection NOTIFY currentSshConnectionChanged)
    Q_PROPERTY(QVariantMap localSshQuery READ localSshQuery NOTIFY localSshQueryChanged)
    Q_PROPERTY(bool localSshSuggestionsActive MEMBER m_localSshSuggestionsActive)
    Q_PROPERTY(bool localSshSelectionReady MEMBER m_localSshSelectionReady)
    Q_PROPERTY(bool broadcastArmed READ broadcastArmed NOTIFY broadcastStateChanged)
    Q_PROPERTY(QString title READ title NOTIFY titleChanged)
    Q_PROPERTY(QString activeTarget READ activeTarget NOTIFY activeTargetChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool logEnabled READ logEnabled WRITE setLogEnabled NOTIFY logEnabledChanged)
    Q_PROPERTY(QString logFilePath READ logFilePath NOTIFY logFilePathChanged)
    Q_PROPERTY(QString logError READ logError NOTIFY logErrorChanged)
    Q_PROPERTY(int scrollbackLines READ scrollbackLines NOTIFY scrollbackChanged)
    Q_PROPERTY(int scrollOffset READ scrollOffset NOTIFY scrollOffsetChanged)
    Q_PROPERTY(bool syntaxHighlightEnabled READ syntaxHighlightEnabled WRITE
                   setSyntaxHighlightEnabled NOTIFY syntaxHighlightEnabledChanged)
    Q_PROPERTY(QVariantList highlightRules READ highlightRules WRITE setHighlightRules NOTIFY
                   highlightRulesChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(int searchMatchCount READ searchMatchCount NOTIFY searchMatchesChanged)
    Q_PROPERTY(int currentSearchMatch READ currentSearchMatch NOTIFY searchMatchesChanged)
    Q_PROPERTY(int scrollbackLimit READ scrollbackLimit WRITE setScrollbackLimit NOTIFY
                   scrollbackLimitChanged)
    Q_PROPERTY(QString backspaceMode READ backspaceMode WRITE setBackspaceMode NOTIFY
                   backspaceModeChanged)
    Q_PROPERTY(QVariantMap colorScheme READ colorScheme WRITE setColorScheme NOTIFY
                   colorSchemeChanged)
    Q_PROPERTY(bool cursorOverlayVisible READ cursorOverlayVisible NOTIFY cursorPresentationChanged)
    Q_PROPERTY(qreal cursorX READ cursorX NOTIFY cursorPresentationChanged)
    Q_PROPERTY(qreal cursorY READ cursorY NOTIFY cursorPresentationChanged)
    Q_PROPERTY(qreal cursorWidth READ cursorWidth NOTIFY cursorPresentationChanged)
    Q_PROPERTY(qreal cursorHeight READ cursorHeight NOTIFY cursorPresentationChanged)
    Q_PROPERTY(QColor cursorColor READ cursorColor NOTIFY cursorPresentationChanged)
    Q_PROPERTY(bool cursorBlinkEnabled READ cursorBlinkEnabled NOTIFY cursorPresentationChanged)
    Q_PROPERTY(int cursorEpoch READ cursorEpoch NOTIFY cursorEpochChanged)
    Q_PROPERTY(bool copyOnSelect READ copyOnSelect WRITE setCopyOnSelect NOTIFY inputSettingsChanged)
    Q_PROPERTY(QVariantMap keyBindings READ keyBindings WRITE setKeyBindings NOTIFY inputSettingsChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(QString logRootDirectory READ logRootDirectory WRITE setLogRootDirectory NOTIFY
                   logRootDirectoryChanged)
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

public:
    explicit TerminalView(QQuickItem* parent = nullptr);
    ~TerminalView() override;
    Q_INVOKABLE QVariantMap memoryDiagnostics() const;

    [[nodiscard]] QString fontFamily() const;
    void setFontFamily(const QString& family);

    [[nodiscard]] qreal fontPixelSize() const;
    void setFontPixelSize(qreal size);
    QString textAntialiasing() const { return m_textAntialiasing; }
    void setTextAntialiasing(const QString& mode);
    QString fontHinting() const { return m_fontHinting; }
    void setFontHinting(const QString& mode);

    [[nodiscard]] int columns() const;
    [[nodiscard]] int rows() const;
    [[nodiscard]] bool isRunning() const;
    bool remoteSession() const { return m_managedSsh; }
    QVariantMap currentSshConnection() const { return m_currentSshConnection; }
    QVariantMap localSshQuery() const { return m_localSshQuery; }
    Q_INVOKABLE void dismissLocalSsh();
    Q_INVOKABLE bool runLocalSsh(const QVariantMap& profile, const QVariantMap& expectedQuery);
    bool broadcastArmed() const { return m_broadcastArmed && m_managedSsh && isRunning(); }
    Q_INVOKABLE bool setBroadcastArmed(bool enabled);
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString activeTarget() const;
    [[nodiscard]] QString statusText() const;

    [[nodiscard]] bool logEnabled() const;
    void setLogEnabled(bool enabled);
    [[nodiscard]] QString logFilePath() const;
    [[nodiscard]] QString logError() const;

    [[nodiscard]] int scrollbackLines() const;
    [[nodiscard]] int scrollOffset() const;
    [[nodiscard]] bool syntaxHighlightEnabled() const;
    void setSyntaxHighlightEnabled(bool enabled);
    [[nodiscard]] QVariantList highlightRules() const;
    void setHighlightRules(const QVariantList& rules);
    [[nodiscard]] QString searchText() const;
    void setSearchText(const QString& text);
    [[nodiscard]] int searchMatchCount() const;
    [[nodiscard]] int currentSearchMatch() const;
    [[nodiscard]] int scrollbackLimit() const;
    void setScrollbackLimit(int lines);
    [[nodiscard]] QString backspaceMode() const;
    void setBackspaceMode(const QString& mode);
    [[nodiscard]] QVariantMap colorScheme() const;
    void setColorScheme(const QVariantMap& scheme);
    [[nodiscard]] static QByteArray backspaceSequence(
        const QString& mode,
        Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    [[nodiscard]] QString logRootDirectory() const;
    void setLogRootDirectory(const QString& directory);
    [[nodiscard]] QString logDirectoryTemplate() const;
    void setLogDirectoryTemplate(const QString& value);
    [[nodiscard]] QString logFileNameTemplate() const;
    void setLogFileNameTemplate(const QString& value);
    [[nodiscard]] bool logTimestamps() const;
    void setLogTimestamps(bool enabled);
    [[nodiscard]] bool logAppend() const;
    void setLogAppend(bool enabled);
    [[nodiscard]] int logRotationMegabytes() const;
    void setLogRotationMegabytes(int megabytes);
    [[nodiscard]] int logRetainedFiles() const;
    void setLogRetainedFiles(int files);

    Q_INVOKABLE bool startLocalShell();
    Q_INVOKABLE bool startLocalCommand(const QStringList& command);
    Q_INVOKABLE bool startLocalProfile(const QVariantMap& profile);
    Q_INVOKABLE void restartLocalShell();
    Q_INVOKABLE void restartSession();
    Q_INVOKABLE void connectToTarget(const QString& target);
    Q_INVOKABLE void startSshSession(const QString& host,
                                     const QString& user = {},
                                     int port = 22);
    Q_INVOKABLE void startSshProfile(const QVariantMap& profile);
    Q_INVOKABLE void confirmX11(bool accepted);
    Q_INVOKABLE void sendText(const QString& text);
    Q_INVOKABLE void pasteClipboard();
    Q_INVOKABLE void copySelection();
    Q_INVOKABLE void pasteSelection();
    Q_INVOKABLE void pasteText(const QString& text);
    Q_INVOKABLE void sendMacro(const QString& text, bool appendEnter);
    Q_INVOKABLE void confirmPaste(bool accepted);
    Q_INVOKABLE void selectAll();
    bool copyOnSelect() const { return m_copyOnSelect; }
    void setCopyOnSelect(bool value);
    QVariantMap keyBindings() const { return m_keyBindings; }
    void setKeyBindings(const QVariantMap& value);
    bool hasSelection() const;
    [[nodiscard]] bool cursorOverlayVisible() const;
    [[nodiscard]] qreal cursorX() const;
    [[nodiscard]] qreal cursorY() const;
    [[nodiscard]] qreal cursorWidth() const;
    [[nodiscard]] qreal cursorHeight() const;
    [[nodiscard]] QColor cursorColor() const;
    [[nodiscard]] bool cursorBlinkEnabled() const;
    [[nodiscard]] int cursorEpoch() const;
    Q_INVOKABLE void clearScrollback();
    Q_INVOKABLE void findNext();
    Q_INVOKABLE void findPrevious();
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void scrollToBottom();

    void paint(QPainter* painter) override;

signals:
    void currentSshConnectionChanged();
    void localSshQueryChanged();
    void localSshNavigate(int direction);
    void localSshAccept();
    void localSshDismiss();
    void localSshError(const QString& message);
    void broadcastStateChanged();
    void inputSettingsChanged();
    void selectionChanged();
    void contextMenuRequested(const QPointF& position);
    void pasteConfirmationRequested(const QString& preview, bool appendEnter);
    void x11ConfirmationRequested(const QString& target);
    void fontFamilyChanged();
    void fontPixelSizeChanged();
    void renderingChanged();
    void gridSizeChanged();
    void runningChanged();
    void titleChanged();
    void activeTargetChanged();
    void statusTextChanged();
    void logEnabledChanged();
    void logFilePathChanged();
    void logErrorChanged();
    void scrollbackChanged();
    void scrollOffsetChanged();
    void syntaxHighlightEnabledChanged();
    void highlightRulesChanged();
    void searchTextChanged();
    void searchMatchesChanged();
    void scrollbackLimitChanged();
    void backspaceModeChanged();
    void colorSchemeChanged();
    void cursorPresentationChanged();
    void cursorEpochChanged();
    void logRootDirectoryChanged();
    void logDirectoryTemplateChanged();
    void logFileNameTemplateChanged();
    void logTimestampsChanged();
    void logAppendChanged();
    void logRotationMegabytesChanged();
    void logRetainedFilesChanged();
    void zoomRequested(int steps);
    void bell();
    void rawOutputReceived(const QByteArray& data);

protected:
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    // Native test/benchmark access only; never exposed through QML, CLI or an environment hook.
    friend class TerminalTestAccess;
    friend class BroadcastController;
    QString broadcastError(const QString& command, quint64 generation) const;
    using CellLine = std::vector<VTermScreenCell>;

    struct TextPoint {
        int line = -1;
        int column = 0;
    };

    struct TextRange {
        TextPoint start;
        TextPoint end;
    };

    void initializeTerminal();
    void launchSshProfile(const QVariantMap& profile);
    static QVariantMap sshConnectionSummary(const QVariantMap& profile, const QString& mode);
    void setCurrentSshConnection(const QVariantMap& connection);
    void requestPaste(const QString& text, bool appendEnter);
    void writePaste(const QString& text, bool appendEnter);
    void resetTerminal();
    void recalculateMetrics();
    QFont terminalFont(bool bold = false, bool italic = false) const;
    void recalculateGrid();
    void handleProcessData(const QByteArray& data);
    void refreshLocalSshQuery();
    void clearLocalSshQuery();
    QString localInputTextLocked() const;
    void processShellMarker(int command, VTermStringFragment fragment);
    void completePendingLocalSsh();
    void cancelPendingLocalSsh();
    void sendLocalControlC();
    void handleProcessFinished(int exitCode);
    void setTitle(const QString& title);
    void setActiveTarget(const QString& target);
    void setStatusText(const QString& status);
    void setLogError(const QString& error);
    void startLog(const QString& sessionName);
    void stopLog();
    void restartLogForOptionChange();
    void setScrollOffset(int offset);
    bool launchProcess(const QString& program,
                       const QStringList& arguments,
                       const QString& targetLabel,
                       const QString& startingStatus, bool managedSsh = false,
                       bool interactiveShell = false, const QString& workingDirectory = {},
                       const QProcessEnvironment& environmentOverrides = {});
    void stopPasswordBroker();
    void rebuildSearchMatchesLocked(bool incremental = false);
    void revealCurrentSearchMatchLocked();
    void paintContent(QPainter* painter, const QBitArray* dirtyRows = nullptr);
    void damageRows(int start, int end);
    void update(); // Mark cached text dirty and request one scene update.
    void requestSceneUpdate();
    void noteCursorGeometry();
    void noteCursorActivity();
    void scheduleCursorFlush();
    void flushCursorNotifications();
    void syncPresentationState();

    [[nodiscard]] CellLine lineAt(int globalLine) const;
    [[nodiscard]] QString textForCell(const VTermScreenCell& cell) const;
    [[nodiscard]] QString selectedText() const;
    [[nodiscard]] TextPoint textPointAt(const QPointF& position) const;
    [[nodiscard]] bool cellIsSelected(int globalLine, int column) const;
    [[nodiscard]] int searchStateForCell(int globalLine, int column) const;
    [[nodiscard]] QColor terminalColor(const VTermColor& color,
                                       const QColor& fallback) const;
    [[nodiscard]] VTermModifier modifiersFor(Qt::KeyboardModifiers modifiers) const;
    [[nodiscard]] static bool pointBefore(const TextPoint& left, const TextPoint& right);

    void sendKey(VTermKey key, VTermModifier modifiers);
    void sendCodepoint(uint codepoint, VTermModifier modifiers);
    void sendBackspace(Qt::KeyboardModifiers modifiers);
    void sendMousePosition(const QPointF& position, Qt::KeyboardModifiers modifiers);

    static void outputCallback(const char* bytes, size_t length, void* user);
    static int oscFallbackCallback(int command, VTermStringFragment fragment, void* user);
    static int damageCallback(VTermRect rect, void* user);
    static int moveRectCallback(VTermRect destination, VTermRect source, void* user);
    static int moveCursorCallback(VTermPos position,
                                  VTermPos oldPosition,
                                  int visible,
                                  void* user);
    static int setTermPropertyCallback(VTermProp property, VTermValue* value, void* user);
    static int bellCallback(void* user);
    static int resizeCallback(int rows, int columns, void* user);
    static int scrollbackPushCallback(int columns,
                                      const VTermScreenCell* cells,
                                      void* user);
    static int scrollbackPopCallback(int columns, VTermScreenCell* cells, void* user);
    static int scrollbackClearCallback(void* user);

    mutable QMutex m_terminalMutex;
    bool m_performanceProbe = false;
    bool m_performanceFixtureComplete = false;
    QByteArray m_performanceTail;
    PtyProcess m_process;
    std::unique_ptr<bitshell::core::PasswordBroker> m_passwordBroker;
    std::unique_ptr<bitshell::core::PasswordBroker> m_pendingPasswordBroker;
    bitshell::core::SessionLogger m_logger;
    bitshell::core::HighlightEngine m_highlightEngine;
    QList<bitshell::core::HighlightRule> m_highlightRules =
        bitshell::core::HighlightEngine::defaultRules();
    QVariantList m_highlightRuleValues;

    VTerm* m_terminal = nullptr;
    VTermScreen* m_screen = nullptr;
    VTermScreenCallbacks m_screenCallbacks{};
    VTermStateFallbacks m_stateFallbacks{};

    std::deque<ScrollbackLine> m_scrollback;
    std::size_t m_scrollbackBytes = 0;
    quint64 m_historyDropped = 0;
    quint64 m_searchHistoryDropped = 0;
    int m_searchedHistoryLines = 0;
    std::size_t m_historySearchMatchCount = 0;
    int m_scrollbackLimit = 10000;
    int m_scrollOffset = 0;
    QString m_backspaceMode = QStringLiteral("del");
    QVariantMap m_colorScheme;
    QVariantMap m_keyBindings;
    bool m_copyOnSelect = false;
    qreal m_zoomWheelRemainder = 0;

    QString m_fontFamily;
    qreal m_fontPixelSize = 13.0;
    QString m_textAntialiasing = QStringLiteral("system");
    QString m_fontHinting = QStringLiteral("default");
    qreal m_cellWidth = 8.0;
    qreal m_cellHeight = 17.0;
    qreal m_ascent = 13.0;
    int m_columns = 80;
    int m_rows = 24;
    qreal m_horizontalPadding = 14.0;
    qreal m_verticalPadding = 10.0;

    QColor m_defaultForeground = QColor(QStringLiteral("#e8eef7"));
    QColor m_defaultBackground = QColor(QStringLiteral("#070b11"));
    QColor m_selectionColor = QColor(QStringLiteral("#24504f"));
    QColor m_cursorColor = QColor(QStringLiteral("#6ee7d5"));

    VTermPos m_cursorPosition{0, 0};
    bool m_cursorVisible = true;
    bool m_cursorBlinkEnabled = true;
    int m_cursorShape = VTERM_PROP_CURSORSHAPE_BLOCK;
    int m_cursorEpoch = 0;
    bool m_cursorGeometryDirty = false;
    bool m_cursorActivity = false;
    bool m_cursorFlushQueued = false;
    int m_mouseMode = VTERM_PROP_MOUSE_NONE;
    QList<QMetaObject::Connection> m_windowConnections;
    QImage m_contentCache; // Active viewport only; capped at 16 MiB and released when hidden.
    bool m_contentDirty = true;
    bool m_processingOutput = false;
    Win32Input m_win32Input;
    QString m_shellIntegrationToken;
    QString m_integratedShellKind;
    QByteArray m_shellMarkerFragment;
    bool m_localPromptActive = false;
    bool m_alternateScreen = false;
    quint64 m_localPromptGeneration = 0;
    quint64 m_localInputRow = 0;
    int m_localInputColumn = -1;
    QVariantMap m_localSshQuery;
    QVariantMap m_currentSshConnection;
    bool m_currentSshConnectionNotificationPending = false;
    bool m_localSshSuggestionsActive = false;
    bool m_localSshSelectionReady = false;
    QString m_dismissedLocalSshCommand;
    QString m_lastLocalInput;
    QString m_pendingLocalSshCommand;
    QString m_pendingLocalSshStatus;
    QVariantMap m_pendingLocalSshConnection;
    QString m_pendingLocalSshShellKind;
    quint64 m_pendingLocalSshGeneration = 0;
    quint64 m_localSshRequestSerial = 0;
    quint64 m_localSshRefreshSerial = 0;
    QSet<int> m_consumedLocalSshKeys;
    QBitArray m_dirtyRows;
    quint64 m_contentPaintCount = 0;
    quint64 m_sceneUpdateCount = 0;

    TextPoint m_selectionAnchor;
    TextPoint m_selectionEnd;
    bool m_selecting = false;
    bool m_terminalMousePressed = false;

    QString m_title;
    QByteArray m_titleFragment;
    QString m_pendingPaste;
    bool m_pendingPasteEnter = false;
    QVariantMap m_pendingX11Profile;
    QVariantMap m_lastSshProfile;
    bool m_managedSsh = false;
    bool m_broadcastArmed = false;
    quint64 m_broadcastGeneration = 0;
    QString m_activeTarget = QStringLiteral("Local");
    QString m_statusText = QStringLiteral("Starting terminal…");
    QString m_lastProgram;
    QStringList m_lastArguments;
    QString m_lastTargetLabel = QStringLiteral("Local");
    QString m_lastStartingStatus = QStringLiteral("Starting local shell…");
    bool m_lastInteractiveShell = false;
    QString m_lastWorkingDirectory;

    bool m_logEnabled = true;
    bool m_syntaxHighlightEnabled = true;
    QString m_searchText;
    std::vector<TextRange> m_searchMatches;
    int m_currentSearchMatch = -1;
    QString m_logFilePath;
    QString m_logError;
    QString m_logRootDirectory;
    QString m_logDirectoryTemplate = QStringLiteral("{session}");
    QString m_logFileNameTemplate = QStringLiteral("{session}_{date}_{time}.log");
    bool m_logTimestamps = false;
    bool m_logAppend = true;
    int m_logRotationMegabytes = 25;
    int m_logRetainedFiles = 5;
};

} // namespace bitshell::terminal
