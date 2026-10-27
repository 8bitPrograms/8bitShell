#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <fcntl.h>
#include <sys/stat.h>
#ifdef Q_OS_LINUX
#include <sys/syscall.h>
#endif
#include <unistd.h>
#include <cerrno>
#include <cstring>

namespace bitshell::core {
namespace {
int parentDescriptor(const QString& path, bool create, QString& error) {
    if (path.isEmpty() || !SshSecurity::safePath(path)) { error = QStringLiteral("Invalid file path."); return -1; }
    const auto parent = QFileInfo(path).absolutePath();
    int fd = ::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    for (const auto& component : QDir::cleanPath(parent).split(u'/', Qt::SkipEmptyParts)) {
        if (fd < 0) break;
        const auto name = QFile::encodeName(component);
        if (create && ::mkdirat(fd, name.constData(), 0700) != 0 && errno != EEXIST) {
            ::close(fd); fd = -1; break;
        }
        const int next = ::openat(fd, name.constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        ::close(fd); fd = next;
    }
    if (fd < 0) error = QStringLiteral("Cannot access a regular directory without symbolic links: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
    return fd;
}
}
bool PrivateFile::prepareParent(const QString& path, QString& error) {
    const int fd = parentDescriptor(path, true, error);
    if (fd < 0) return false;
    ::close(fd); return true;
}
bool PrivateFile::openLog(QFile& file, bool append, QString& error) {
    const int parent = parentDescriptor(file.fileName(), true, error);
    if (parent < 0) return false;
    const auto name = QFile::encodeName(QFileInfo(file.fileName()).fileName());
    const int fd = ::openat(parent, name.constData(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC
                           | (append ? O_APPEND : 0), 0600);
    ::close(parent);
    if (fd < 0) { error = QStringLiteral("Cannot safely open the log file."); return false; }
    struct stat status{};
    // Validate before truncation or chmod: reject hard links and special files.
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1
        || status.st_uid != ::geteuid() || ::fchmod(fd, 0600) != 0 || (!append && ::ftruncate(fd, 0) != 0)) {
        ::close(fd); error = QStringLiteral("Log must be a regular, single-link file owned by the current user."); return false;
    }
    if (!file.open(fd, QIODevice::WriteOnly | (append ? QIODevice::Append : QIODevice::OpenMode{}), QFileDevice::AutoCloseHandle)) {
        ::close(fd); error = file.errorString(); return false;
    }
    return true;
}
bool PrivateFile::read(const QString& path, QByteArray& bytes, QString& error, qint64 limit) {
    const int parent = parentDescriptor(path, false, error);
    if (parent < 0) return false;
    const auto name = QFile::encodeName(QFileInfo(path).fileName());
    const int fd = ::openat(parent, name.constData(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    ::close(parent);
    if (fd < 0) { error = QStringLiteral("Cannot read a regular file without symbolic links."); return false; }
    struct stat status{};
    if (::fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_size > limit) {
        ::close(fd); error = QStringLiteral("Input must be a regular file no larger than %1 MiB.").arg(limit / 1024 / 1024); return false;
    }
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { ::close(fd); error = file.errorString(); return false; }
    bytes = file.read(limit + 1);
    if (bytes.size() > limit || file.error() != QFileDevice::NoError) {
        bytes.clear(); error = QStringLiteral("Input changed size or could not be read safely."); return false;
    }
    return true;
}
bool PrivateFile::write(const QString& path, const QByteArray& bytes, QString& error) {
    const QFileInfo info(path);
    if (path.isEmpty() || info.isSymLink() || (info.exists() && !info.isFile())) {
        error = QStringLiteral("Choose a local regular file, not a symbolic link."); return false;
    }
    if (bytes.size() > 8 * 1024 * 1024) { error = QStringLiteral("File exceeds the 8 MiB limit."); return false; }
    if (!prepareParent(path, error)) return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        error = file.errorString(); return false;
    }
    error.clear(); return true;
}
bool PrivateFile::secureEntry(const QString& path, QString& error) {
    const QFileInfo info(path);
    if (info.isSymLink() || (!info.isFile() && !info.isDir())) { error = QStringLiteral("Not a regular file or directory."); return false; }
    struct stat status{};
    if (::lstat(QFile::encodeName(path).constData(), &status) != 0
        || status.st_uid != ::geteuid()
        || (info.isFile() && (!S_ISREG(status.st_mode) || status.st_nlink != 1))
        || (info.isDir() && !S_ISDIR(status.st_mode))) {
        error = QStringLiteral("Private entries must be owned by the current user and files must have one hard link.");
        return false;
    }
    auto permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (info.isDir()) permissions |= QFileDevice::ExeOwner;
    if (!QFile::setPermissions(path, permissions)) { error = QStringLiteral("Cannot restrict file permissions."); return false; }
    return true;
}
bool PrivateFile::publishNoReplace(const QString& source, const QString& destination, bool& collision, QString& error) {
    collision = false;
#if defined(Q_OS_LINUX) && defined(SYS_renameat2)
    if (::syscall(SYS_renameat2, AT_FDCWD, QFile::encodeName(source).constData(), AT_FDCWD,
                  QFile::encodeName(destination).constData(), 1U /* RENAME_NOREPLACE */) == 0) return true;
#else
    // link() publishes a regular staged file atomically without replacing a destination.
    if (::link(QFile::encodeName(source).constData(), QFile::encodeName(destination).constData()) == 0) {
        (void)::unlink(QFile::encodeName(source).constData());
        return true;
    }
#endif
    collision = errno == EEXIST;
    error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
}
} // namespace bitshell::core
