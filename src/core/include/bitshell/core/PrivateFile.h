#pragma once
#include <QFile>

namespace bitshell::core {
// Native private storage. Never follow symlinks (or Windows reparse points).
class PrivateFile {
public:
    static bool prepareParent(const QString& path, QString& error);
    // Set file.fileName() before every call: descriptor adoption may clear the
    // QFile name on Windows. The caller retains the canonical log path.
    static bool openLog(QFile& file, bool append, QString& error);
    static bool read(const QString& path, QByteArray& bytes, QString& error, qint64 limit = 8 * 1024 * 1024);
    static bool write(const QString& path, const QByteArray& bytes, QString& error);
    static bool secureEntry(const QString& path, QString& error);
    static bool publishNoReplace(const QString& source, const QString& destination, bool& collision, QString& error);
};
} // namespace bitshell::core
