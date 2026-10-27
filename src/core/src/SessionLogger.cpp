#include <bitshell/core/SessionLogger.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>

#include <algorithm>

#include <QDir>
#include <QFileInfo>

namespace bitshell::core {

QByteArray AnsiStripper::filter(QByteArrayView input) {
    QByteArray output;
    output.reserve(input.size());

    for (const char raw : input) {
        const auto byte = static_cast<unsigned char>(raw);
        switch (m_state) {
        case State::Ground:
            if (byte == 0x1bU) {
                m_state = State::Escape;
            } else if ((byte >= 0x20U && byte != 0x7fU) || raw == '\n' || raw == '\r' || raw == '\t') {
                output.append(raw);
            }
            break;
        case State::Escape:
            if (raw == '[') {
                m_state = State::Csi;
            } else if (raw == ']') {
                m_state = State::Osc;
            } else if (raw == 'P' || raw == 'X' || raw == '^' || raw == '_') {
                m_state = State::String;
            } else {
                m_state = State::Ground;
            }
            break;
        case State::Csi:
            if (byte >= 0x40U && byte <= 0x7eU) {
                m_state = State::Ground;
            }
            break;
        case State::Osc:
            if (byte == 0x07U) {
                m_state = State::Ground;
            } else if (byte == 0x1bU) {
                m_state = State::OscEscape;
            }
            break;
        case State::OscEscape:
            if (raw == '\\') {
                m_state = State::Ground;
            } else if (byte != 0x1bU) {
                m_state = State::Osc;
            }
            break;
        case State::String:
            if (byte == 0x1bU) m_state = State::StringEscape;
            else if (byte == 0x18U || byte == 0x1aU) m_state = State::Ground;
            break;
        case State::StringEscape:
            if (raw == '\\') m_state = State::Ground;
            else if (byte != 0x1bU) m_state = State::String;
            break;
        }
    }

    return output;
}

void AnsiStripper::reset() {
    m_state = State::Ground;
}

SessionLogger::~SessionLogger() {
    stop();
}

bool SessionLogger::start(const LogOptions& options,
                          const LogContext& context,
                          QString* errorMessage) {
    stop();
    m_options = options;
    m_context = context;
    m_ansiStripper.reset();
    m_atLineStart = true;

    if (options.rootDirectory.trimmed().isEmpty()) {
        setError(errorMessage, QStringLiteral("Log root directory is empty"));
        return false;
    }

    const auto relativeDirectory = QDir::cleanPath(applyTemplate(options.directoryTemplate, context));
    const auto fileName = applyTemplate(options.fileNameTemplate, context);
    if (QDir::isAbsolutePath(relativeDirectory) || relativeDirectory == QStringLiteral("..")
        || relativeDirectory.startsWith(QStringLiteral("../"))) {
        setError(errorMessage, QStringLiteral("Log directory template escapes the log root"));
        return false;
    }
    if (fileName.contains(u'/') || fileName.contains(u'\\') || fileName.isEmpty()
        || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")
        || !SshSecurity::safePath(fileName) || !SshSecurity::safePath(relativeDirectory)) {
        setError(errorMessage, QStringLiteral("Log file template must produce a file name"));
        return false;
    }

    QDir root(options.rootDirectory);
    const auto directoryPath = root.filePath(relativeDirectory);
    m_path = QDir(directoryPath).filePath(fileName);
    m_file.setFileName(m_path);
    QString error;
    if (!PrivateFile::prepareParent(m_path, error)) { setError(errorMessage, error); return false; }
    m_lock = std::make_unique<QLockFile>(m_path + QStringLiteral(".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        m_lock.reset();
        setError(errorMessage, QStringLiteral("Log is already in use. Choose a unique log name for this session."));
        return false;
    }
    if (!PrivateFile::openLog(m_file, options.existingFileMode == ExistingLogMode::Append, error)) {
        m_lock.reset();
        setError(errorMessage, error);
        return false;
    }
    return true;
}

bool SessionLogger::writeOutput(QByteArrayView output, QString* errorMessage) {
    if (!m_file.isOpen()) {
        setError(errorMessage, QStringLiteral("Log is not open"));
        return false;
    }

    QByteArray prepared = m_options.stripAnsi ? m_ansiStripper.filter(output)
                                              : QByteArray(output.data(), output.size());
    if (m_options.timestampMode == LogTimestampMode::EachLine) {
        prepared = applyLineTimestamps(prepared);
    }
    if (!rotateIfNeeded(prepared.size(), errorMessage)) {
        return false;
    }

    if (m_file.write(prepared) != prepared.size()) {
        setError(errorMessage, m_file.errorString());
        return false;
    }
    if (!m_file.flush()) {
        setError(errorMessage, m_file.errorString());
        return false;
    }
    return true;
}

void SessionLogger::stop() {
    if (m_file.isOpen()) {
        m_file.flush();
        m_file.close();
    }
    m_lock.reset();
}

bool SessionLogger::isOpen() const {
    return m_file.isOpen();
}

QString SessionLogger::filePath() const {
    return m_path;
}

QString SessionLogger::safePathToken(QString value) {
    value = value.trimmed();
    for (auto& character : value) {
        if (!(character.isLetterOrNumber() || character == u'-' || character == u'_'
              || character == u'.')) {
            character = u'_';
        }
    }
    if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral("..")) return QStringLiteral("session");
    return value.left(80);
}

QString SessionLogger::applyTemplate(QString value, const LogContext& context) {
    const auto timestamp = context.startedAt.isValid() ? context.startedAt
                                                        : QDateTime::currentDateTime();
    value.replace(QStringLiteral("{session}"), safePathToken(context.session));
    value.replace(QStringLiteral("{host}"), safePathToken(context.host));
    value.replace(QStringLiteral("{user}"), safePathToken(context.user));
    value.replace(QStringLiteral("{date}"), timestamp.toString(QStringLiteral("yyyy-MM-dd")));
    value.replace(QStringLiteral("{time}"), timestamp.toString(QStringLiteral("HH-mm-ss")));
    return value;
}

bool SessionLogger::rotateIfNeeded(qint64 incomingBytes, QString* errorMessage) {
    if (m_options.rotateAfterBytes <= 0
        || m_file.size() + incomingBytes <= m_options.rotateAfterBytes) {
        return true;
    }

    const auto path = m_path;
    m_file.close();

    const int retained = std::clamp(m_options.retainedRotations, 0, 100);
    for (int index = 0; index <= retained; ++index) {
        const QString entry = index == 0 ? path : path + QStringLiteral(".%1").arg(index);
        const QFileInfo info(entry);
        if (info.isSymLink() || (info.exists() && !info.isFile())) {
            setError(errorMessage, QStringLiteral("Unsafe log rotation target; logging stopped.")); return false;
        }
    }
    if (retained > 0) {
        const auto oldest = path + QStringLiteral(".%1").arg(retained);
        if (QFile::exists(oldest) && !QFile::remove(oldest)) {
            setError(errorMessage, QStringLiteral("Cannot remove oldest log rotation; logging stopped.")); return false;
        }
        for (int index = retained - 1; index >= 1; --index) {
            const auto source = path + QStringLiteral(".%1").arg(index);
            const auto destination = path + QStringLiteral(".%1").arg(index + 1);
            if (QFile::exists(source)) {
                if (!QFile::rename(source, destination)) {
                    setError(errorMessage, QStringLiteral("Cannot rename log rotation; logging stopped without truncating the current log.")); return false;
                }
            }
        }
        if (QFile::exists(path)) {
            if (!QFile::rename(path, path + QStringLiteral(".1"))) {
                setError(errorMessage, QStringLiteral("Cannot rotate current log; logging stopped without truncation.")); return false;
            }
        }
    } else {
        if (!QFile::remove(path)) {
            setError(errorMessage, QStringLiteral("Cannot replace current log; logging stopped without truncation.")); return false;
        }
    }

    QString error;
    m_file.setFileName(m_path);
    if (!PrivateFile::openLog(m_file, false, error)) {
        setError(errorMessage, error);
        return false;
    }
    return true;
}

QByteArray SessionLogger::applyLineTimestamps(QByteArrayView output) {
    QByteArray timestamped;
    const QByteArray prefix =
        QDateTime::currentDateTime().toString(QStringLiteral("[yyyy-MM-dd HH:mm:ss] ")).toUtf8();
    timestamped.reserve(output.size() + prefix.size() * 2);

    for (const char byte : output) {
        if (m_atLineStart) {
            timestamped += prefix;
            m_atLineStart = false;
        }
        timestamped += byte;
        if (byte == '\n') {
            m_atLineStart = true;
        }
    }
    return timestamped;
}

void SessionLogger::setError(QString* destination, const QString& message) {
    if (destination != nullptr) {
        *destination = message;
    }
}

} // namespace bitshell::core
