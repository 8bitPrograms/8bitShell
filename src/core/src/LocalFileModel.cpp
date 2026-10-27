#include <bitshell/core/LocalFileModel.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <QDir>
#include <QDirIterator>
#include <QDrag>
#include <QGuiApplication>
#include <QLocale>
#include <QMimeData>
#include <QWindow>
#include <QtConcurrent>

namespace bitshell::core {
namespace {
// Never follow a symbolic link when recursively copying a directory.
bool copyEntry(const QString& source, const QString& destination, QString& error, int& budget, int depth = 0) {
    if (--budget < 0 || depth > 64) {
        error = QStringLiteral("Copy stopped: at most 50,000 entries and 64 directory levels are supported. Completed files were kept.");
        return false;
    }
    const QFileInfo info(source);
    if (info.isSymLink() || info.isJunction()) {
        error = QStringLiteral("Symbolic links are not copied: %1").arg(source);
        return false;
    }
    if (info.isDir()) {
        if (!QDir().mkdir(destination, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) {
            error = QStringLiteral("Cannot create %1").arg(destination);
            return false;
        }
#ifdef Q_OS_WIN
        if (!PrivateFile::secureEntry(destination, error)) return false;
#endif
        QDirIterator entries(source, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
        while (entries.hasNext()) {
            entries.next();
            const auto child = entries.fileInfo();
            if (!copyEntry(child.absoluteFilePath(), QDir(destination).filePath(child.fileName()), error, budget, depth + 1))
                return false;
        }
        return true;
    }
    if (!info.isFile() || !QFile::copy(source, destination)) {
        error = QStringLiteral("Cannot copy %1. Any completed files were kept.").arg(source);
        return false;
    }
#ifdef Q_OS_WIN
    if (!PrivateFile::secureEntry(destination, error)) return false;
#endif
    return true;
}
}
LocalFileModel::LocalFileModel(QObject* parent) : QAbstractListModel(parent) {
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(75);
    connect(&m_refreshTimer, &QTimer::timeout, this, &LocalFileModel::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        if (m_active && !m_copying && !m_refreshTimer.isActive()) m_refreshTimer.start();
    });
    connect(&m_copy, &QFutureWatcher<QString>::finished, this, [this] {
        m_copyResult = m_copy.result();
        m_copying = false;
        if (m_active) refresh();
        else m_status = m_copyResult;
        emit changed();
    });
    changeDirectory(QDir::homePath());
}
LocalFileModel::~LocalFileModel() { m_copy.waitForFinished(); }
void LocalFileModel::setActive(bool active) {
    if (m_active == active) return;
    m_active = active;
    m_refreshTimer.stop();
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    if (active && !m_path.isEmpty()) { m_watcher.addPath(m_path); refresh(); }
    emit changed();
}
int LocalFileModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}
QVariant LocalFileModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) return {};
    const auto& info = m_entries.at(index.row());
    if (role == Qt::DisplayRole || role == NameRole) return info.fileName();
    if (role == DirectoryRole) return info.isDir();
    if (role == SizeTextRole) return info.isDir() ? QStringLiteral("—") : QLocale().formattedDataSize(info.size());
    return {};
}
QHash<int,QByteArray> LocalFileModel::roleNames() const {
    return {{NameRole,"name"}, {DirectoryRole,"isDirectory"}, {SizeTextRole,"sizeText"}};
}
void LocalFileModel::changeDirectory(const QString& path) {
    m_copyResult.clear();
    QString next = path;
    if (next == QStringLiteral("~")) next = QDir::homePath();
    else if (next.startsWith(QStringLiteral("~/"))) next = QDir::home().filePath(next.sliced(2));
    if (QDir::isRelativePath(next)) next = QDir(m_path).filePath(next);
    const QFileInfo info(next);
    if (!info.isDir() || !info.isReadable()) {
        m_status = QStringLiteral("Cannot open this directory"); emit changed(); return;
    }
    next = info.canonicalFilePath();
    if (next != m_path) {
        if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
        m_path = next;
        if (m_active) m_watcher.addPath(m_path);
        m_history = m_history.mid(0, m_position + 1);
        m_history.append(m_path);
        m_position = static_cast<int>(m_history.size()) - 1;
    }
    if (m_active) {
        // A tab switch can restore its home directory and immediately replace
        // it with that tab's saved path. Coalesce those changes so Qt Quick
        // never has to cancel half-created delegates from an obsolete listing.
        m_status = QStringLiteral("Loading…");
        m_refreshTimer.start();
    }
    emit changed();
}
void LocalFileModel::refresh() {
    m_refreshTimer.stop();
    if (!m_active) return;
    beginResetModel();
    m_entries = QDir(m_path).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                          QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    endResetModel();
    if (!busy()) m_status = m_copyResult.isEmpty()
        ? QStringLiteral("%1 items · drag files to copy").arg(m_entries.size()) : m_copyResult;
    emit changed();
}
void LocalFileModel::goUp() { changeDirectory(QDir(m_path).filePath(QStringLiteral(".."))); }
void LocalFileModel::goHome() { changeDirectory(QDir::homePath()); }
void LocalFileModel::goBack() {
    if (!canGoBack()) return;
    const auto history = m_history; const int position = m_position - 1;
    changeDirectory(history.at(position)); m_history = history; m_position = position; emit changed();
}
void LocalFileModel::goForward() {
    if (!canGoForward()) return;
    const auto history = m_history; const int position = m_position + 1;
    changeDirectory(history.at(position)); m_history = history; m_position = position; emit changed();
}
void LocalFileModel::openEntry(int row) {
    if (row >= 0 && row < m_entries.size() && m_entries.at(row).isDir())
        changeDirectory(m_entries.at(row).absoluteFilePath());
}
QUrl LocalFileModel::entryUrl(int row) const {
    return row >= 0 && row < m_entries.size() ? QUrl::fromLocalFile(m_entries.at(row).absoluteFilePath()) : QUrl{};
}
QString LocalFileModel::availablePath(const QString& directory, const QString& name) {
    const QString safeName = QFileInfo(name).fileName();
#ifdef Q_OS_WIN
    if (!Platform::windowsFileName(safeName)) return {};
#endif
    if (safeName.isEmpty() || safeName == QStringLiteral(".") || safeName == QStringLiteral("..")) return {};
    QString candidate = QDir(directory).filePath(safeName);
    int suffix = 1;
    while (QFileInfo::exists(candidate) || QFileInfo(candidate).isSymLink()) {
        if (suffix > 100) return {};
        candidate = QDir(directory).filePath(QStringLiteral("%1 (%2)").arg(safeName).arg(suffix++));
    }
    return candidate;
}
void LocalFileModel::copyFiles(const QList<QUrl>& urls) {
    if (busy() || urls.isEmpty()) return;
    const QString destination = m_path;
    m_copyResult.clear();
    m_status = QStringLiteral("Copying %1 item(s)…").arg(urls.size());
    m_copying = true;
    m_refreshTimer.stop();
    m_copy.setFuture(QtConcurrent::run([urls, destination] {
        int count = 0;
        int budget = 50000;
        for (const auto& url : urls) {
            if (!url.isLocalFile()) return QStringLiteral("Only local file URLs can be copied here.");
            const QFileInfo source(url.toLocalFile());
            const QString canonical = source.canonicalFilePath();
            if (source.isDir() && (destination == canonical || destination.startsWith(canonical + u'/')))
                return QStringLiteral("Cannot copy a folder into itself.");
            QString error;
            const QString target = LocalFileModel::availablePath(destination, source.fileName());
            if (target.isEmpty() || !copyEntry(source.absoluteFilePath(), target, error, budget))
                return error.isEmpty() ? QStringLiteral("Invalid destination") : error;
            ++count;
        }
        return QStringLiteral("Copied %1 item(s) to %2").arg(count).arg(destination);
    }));
    emit changed();
}
void LocalFileModel::dragUrls(const QList<QUrl>& urls) {
    if (urls.isEmpty() || !QGuiApplication::focusWindow()) return;
    auto* drag = new QDrag(QGuiApplication::focusWindow());
    auto* mime = new QMimeData;
    mime->setUrls(urls);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction, Qt::CopyAction);
    drag->deleteLater();
}
void LocalFileModel::startDrag(int row) {
    const QUrl url = entryUrl(row);
    if (url.isLocalFile()) dragUrls({url});
}
} // namespace bitshell::core
