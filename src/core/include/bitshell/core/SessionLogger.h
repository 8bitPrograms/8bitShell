#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QDateTime>
#include <QFile>
#include <QLockFile>
#include <QString>
#include <memory>

namespace bitshell::core {

enum class ExistingLogMode {
    Append,
    Truncate,
};

enum class LogTimestampMode {
    None,
    EachLine,
};

struct LogContext {
    QString session;
    QString host;
    QString user;
    QDateTime startedAt = QDateTime::currentDateTime();
};

struct LogOptions {
    QString rootDirectory;
    QString directoryTemplate = QStringLiteral("{session}");
    QString fileNameTemplate = QStringLiteral("{session}_{date}_{time}.log");
    ExistingLogMode existingFileMode = ExistingLogMode::Append;
    LogTimestampMode timestampMode = LogTimestampMode::None;
    bool stripAnsi = true;
    qint64 rotateAfterBytes = 0;
    int retainedRotations = 3;
};

class AnsiStripper {
public:
    [[nodiscard]] QByteArray filter(QByteArrayView input);
    void reset();

private:
    enum class State {
        Ground,
        Escape,
        Csi,
        Osc,
        OscEscape,
        String,
        StringEscape,
    };

    State m_state = State::Ground;
};

class SessionLogger {
public:
    SessionLogger() = default;
    ~SessionLogger();

    SessionLogger(const SessionLogger&) = delete;
    SessionLogger& operator=(const SessionLogger&) = delete;

    [[nodiscard]] bool start(const LogOptions& options,
                             const LogContext& context,
                             QString* errorMessage = nullptr);
    [[nodiscard]] bool writeOutput(QByteArrayView output, QString* errorMessage = nullptr);
    void stop();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString filePath() const;

private:
    [[nodiscard]] static QString safePathToken(QString value);
    [[nodiscard]] static QString applyTemplate(QString value, const LogContext& context);
    [[nodiscard]] bool rotateIfNeeded(qint64 incomingBytes, QString* errorMessage);
    [[nodiscard]] QByteArray applyLineTimestamps(QByteArrayView output);
    static void setError(QString* destination, const QString& message);

    QFile m_file;
    QString m_path;
    std::unique_ptr<QLockFile> m_lock;
    LogOptions m_options;
    LogContext m_context;
    AnsiStripper m_ansiStripper;
    bool m_atLineStart = true;
};

} // namespace bitshell::core
