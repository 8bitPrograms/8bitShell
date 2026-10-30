#include <bitshell/core/SftpController.h>
#include <bitshell/core/LocalFileModel.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/PasswordBroker.h>
#include <bitshell/core/PasswordStore.h>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QProcessEnvironment>
#include <QSet>
#include <QUuid>

#include <algorithm>
#ifndef Q_OS_WIN
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#endif
#include <cerrno>

namespace bitshell::core {

namespace {

constexpr auto kRemoteWorkingDirectoryPrefix = "Remote working directory: ";

bool regularTree(const QString& path, bool setPrivatePermissions) {
    auto check = [setPrivatePermissions](const QFileInfo& info) {
        if (info.isSymLink() || info.isJunction() || (!info.isFile() && !info.isDir())) return false;
        if (!setPrivatePermissions) return true;
        QString error;
        return PrivateFile::secureEntry(info.absoluteFilePath(), error);
    };
    const QFileInfo root(path);
    if (!check(root)) return false;
    if (root.isDir()) {
        QDirIterator entries(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                             QDirIterator::Subdirectories);
        int budget = 50000;
        while (entries.hasNext()) {
            entries.next();
            if (--budget < 0 || !check(entries.fileInfo())) return false;
        }
    }
    return true;
}

} // namespace

SftpController::SftpController(QObject* parent)
    : QAbstractListModel(parent) {
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
#ifndef Q_OS_WIN
    m_process.setChildProcessModifier([] {
        ::umask(0077);
        const rlimit noCore{0, 0};
        (void)::setrlimit(RLIMIT_CORE, &noCore);
    });
#endif
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_processFailureHandled = true;
        m_process.kill();
        m_preparingDrag = false;
        finishWithError(QStringLiteral("Transfer timed out. Local partial downloads stay private; remote partial uploads may remain."));
    });

    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_standardOutput += m_process.readAllStandardOutput();
        if (m_standardOutput.size() > 16 * 1024 * 1024) {
            m_processFailureHandled = true;
            m_process.kill();
            finishWithError(QStringLiteral("Directory listing is too large (16 MB limit)."));
        }
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_standardError += m_process.readAllStandardError();
        if (m_standardError.size() > 65536) m_standardError = m_standardError.right(65536);
    });
    connect(&m_process,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this,
            &SftpController::handleFinished);
    connect(&m_process,
            &QProcess::errorOccurred,
            this,
            &SftpController::handleProcessError);
}

SftpController::~SftpController() {
    m_passwordBroker.reset();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.disconnect(this);
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

int SftpController::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_entries.size());
}

QVariant SftpController::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }
    const auto& entry = m_entries.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case DirectoryRole:
        return entry.directory;
    case SizeRole:
        return entry.size;
    case SizeTextRole:
        return entry.directory ? QStringLiteral("—") : humanSize(entry.size);
    case ModifiedRole:
        return entry.modified;
    case PermissionsRole:
        return entry.permissions;
    default:
        return {};
    }
}

QHash<int, QByteArray> SftpController::roleNames() const {
    return {
        {NameRole, QByteArrayLiteral("name")},
        {DirectoryRole, QByteArrayLiteral("isDirectory")},
        {SizeRole, QByteArrayLiteral("size")},
        {SizeTextRole, QByteArrayLiteral("sizeText")},
        {ModifiedRole, QByteArrayLiteral("modified")},
        {PermissionsRole, QByteArrayLiteral("permissions")},
    };
}

SftpController::State SftpController::state() const {
    return m_state;
}

bool SftpController::connected() const {
    return m_connected;
}

bool SftpController::busy() const {
    return m_state == State::Connecting || m_state == State::Busy;
}

QString SftpController::statusText() const {
    return m_statusText;
}

QString SftpController::currentPath() const {
    return m_currentPath;
}

QString SftpController::target() const {
    return m_target;
}

QString SftpController::lastTransferText() const {
    return m_lastTransferText;
}

QString SftpController::downloadDirectory() const {
    QString directory = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (directory.isEmpty()) {
        directory = QDir::home().filePath(QStringLiteral("Downloads"));
    }
    return directory;
}

int SftpController::count() const {
    return static_cast<int>(m_entries.size());
}

void SftpController::connectToProfile(const QVariantMap& profile) {
    connectToProfileAt(profile, {});
}

void SftpController::connectToProfileAt(const QVariantMap& profile, const QString& initialPath) {
    if (busy()) return;
    const auto validationError = SshSecurity::validateProfile(profile);
    if (!validationError.isEmpty()) { setStatusText(validationError); return; }
    const QString host = profile.value(QStringLiteral("host")).toString().trimmed();
    if (host.isEmpty()) {
        disconnectFromHost();
        setStatusText(QStringLiteral("Select an SSH session to browse remote files"));
        return;
    }

    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }

    const QString requestedPath = initialPath.trimmed();
    if (!requestedPath.isEmpty() && (!requestedPath.startsWith(u'/') || !SshSecurity::safePath(requestedPath))) {
        setStatusText(QStringLiteral("The saved remote folder is invalid; connect to the remote home folder instead."));
        return;
    }
    m_profile = profile;
    m_history.clear(); m_historyPosition = -1; m_historyTarget = -1;
    m_dragKey.clear(); m_dragPath.clear();
    const QString user = profile.value(QStringLiteral("user")).toString().trimmed();
    const QString nextTarget = profile.contains(QStringLiteral("psmp"))
        ? host + QStringLiteral(" via ") + profile.value(QStringLiteral("psmp")).toMap().value(QStringLiteral("route")).toMap().value(QStringLiteral("host")).toString()
        : user.isEmpty() ? host : QStringLiteral("%1@%2").arg(user, host);
    if (m_target != nextTarget) {
        m_target = nextTarget;
        emit targetChanged();
    }
    if (!m_currentPath.isEmpty()) {
        m_currentPath.clear();
        emit currentPathChanged();
    }
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    clearEntries();
    startListing(requestedPath, true);
}

void SftpController::disconnectFromHost() {
    m_passwordBroker.reset();
    m_process.setProcessEnvironment(SshSecurity::environment());
    m_timeout.stop();
    m_processFailureHandled = true;
    m_preparingDrag = false;
    m_history.clear(); m_historyPosition = -1; m_historyTarget = -1;
    m_dragKey.clear(); m_dragPath.clear();
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
    m_operation = Operation::None;
    m_profile.clear();
    clearEntries();
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    if (!m_target.isEmpty()) {
        m_target.clear();
        emit targetChanged();
    }
    if (!m_currentPath.isEmpty()) {
        m_currentPath.clear();
        emit currentPathChanged();
    }
    setState(State::Disconnected);
    setStatusText(QStringLiteral("Select an SSH session to browse remote files"));
}

void SftpController::refresh() {
    if (m_profile.value(QStringLiteral("host")).toString().trimmed().isEmpty() || busy()) {
        return;
    }
    startListing(m_currentPath, false);
}

void SftpController::openEntry(int row) {
    if (row < 0 || row >= static_cast<int>(m_entries.size())
        || !m_entries.at(row).directory || busy()) {
        return;
    }
    changeDirectory(m_entries.at(row).name);
}

void SftpController::goUp() {
    if (busy() || m_currentPath.isEmpty()) {
        return;
    }
    startListing(parentRemotePath(m_currentPath), false);
}

void SftpController::goHome() {
    if (!busy() && !m_profile.isEmpty()) {
        startListing({}, false);
    }
}

void SftpController::changeDirectory(const QString& path) {
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty() || !SshSecurity::safePath(trimmed) || busy()) {
        return;
    }
    const QString requested = trimmed.startsWith(u'/')
                                  ? trimmed
                                  : joinRemotePath(m_currentPath, trimmed);
    startListing(requested, false);
}

void SftpController::download(int row) {
    QDir().mkpath(downloadDirectory());
    downloadTo(row, QUrl::fromLocalFile(downloadDirectory()));
}

void SftpController::downloadTo(int row, const QUrl& directory) {
    if (row < 0 || row >= m_entries.size() || busy() || !m_connected || !directory.isLocalFile()) return;
    const auto& entry = m_entries.at(row);
    const QString destination = directory.toLocalFile();
#ifdef Q_OS_WIN
    if (!Platform::windowsFileName(entry.name)) {
        setLastTransferText(QStringLiteral("This remote name cannot be safely represented on Windows (device name, path separator, or alternate data stream).")); return;
    }
#endif
    if (!QFileInfo(destination).isDir() || !QFileInfo(destination).isWritable()
        || !SshSecurity::safePath(destination) || !SshSecurity::safePath(entry.name)
        || entry.name.contains(u'/') || (!entry.permissions.startsWith(u'-') && !entry.directory)) {
        setLastTransferText(QStringLiteral("Choose a writable local folder. Names with line breaks are unsupported."));
        return;
    }
    m_downloadStaging = std::make_unique<QTemporaryDir>(QDir(destination).filePath(QStringLiteral(".bitshell-download-XXXXXX")));
    if (!m_downloadStaging->isValid()) { setLastTransferText(QStringLiteral("Cannot create a private download directory.")); return; }
    QString privateError;
    if (!PrivateFile::secureEntry(m_downloadStaging->path(), privateError)) { setLastTransferText(privateError); m_downloadStaging.reset(); return; }
    const QString localPath = m_downloadStaging->filePath(entry.name);
    m_downloadDestination = QFileInfo(destination).canonicalFilePath();
    QStringList commands;
    const QString cdCommand = workingDirectoryCommand();
    if (!cdCommand.isEmpty()) commands << cdCommand;
    commands << QStringLiteral("get %1%2 %3")
        .arg(entry.directory ? QStringLiteral("-R ") : QString{}, quoteGlobArgument(QStringLiteral("./") + entry.name),
             quoteSftpArgument(localPath));
    m_transferName = entry.name;
    m_transferLocalPath = localPath;
    startBatch(Operation::Download, commands, QStringLiteral("Downloading %1…").arg(entry.name));
}

void SftpController::upload(const QUrl& localFile) { uploadFiles({localFile}); }

void SftpController::uploadFiles(const QList<QUrl>& localFiles) {
    if (localFiles.isEmpty() || busy() || !m_connected) return;
    QStringList commands, names;
    if (!workingDirectoryCommand().isEmpty()) commands << workingDirectoryCommand();
    QSet<QString> occupied;
    for (const auto& entry : m_entries) occupied.insert(entry.name);
    for (const QUrl& url : localFiles) {
        const QFileInfo info(url.toLocalFile());
        if (!url.isLocalFile() || !info.exists() || info.isSymLink() || (!info.isFile() && !info.isDir())
            || !SshSecurity::safePath(info.absoluteFilePath()) || !regularTree(info.absoluteFilePath(), false)) {
            setLastTransferText(QStringLiteral("Upload blocked: only regular files/folders, no symbolic links, special files or control characters; at most 50,000 entries."));
            return;
        }
        QString name = info.fileName();
        int suffix = 1;
        while (occupied.contains(name)) name = QStringLiteral("%1 (%2)").arg(info.fileName()).arg(suffix++);
        occupied.insert(name);
        commands << QStringLiteral("put %1%2 %3")
            .arg(info.isDir() ? QStringLiteral("-R ") : QString{}, quoteGlobArgument(info.absoluteFilePath()),
                 quoteSftpArgument(QStringLiteral("./") + name));
        names << name;
    }
    m_transferName = names.join(QStringLiteral(", "));
    startBatch(Operation::Upload, commands, QStringLiteral("Uploading %1 item(s)…").arg(localFiles.size()));
}

void SftpController::goBack() {
    if (!busy() && canGoBack()) {
        m_historyTarget = m_historyPosition - 1;
        startListing(m_history.at(m_historyTarget), false);
    }
}
void SftpController::goForward() {
    if (!busy() && canGoForward()) {
        m_historyTarget = m_historyPosition + 1;
        startListing(m_history.at(m_historyTarget), false);
    }
}
void SftpController::startDrag(int row) {
    if (busy() || !m_connected || row < 0 || row >= m_entries.size()) return;
    const auto& entry = m_entries.at(row);
    const QString key = m_currentPath + u'/' + entry.name + u'|' + QString::number(entry.size) + u'|' + entry.modified;
    if (m_dragKey == key && QFileInfo::exists(m_dragPath)) {
        LocalFileModel::dragUrls({QUrl::fromLocalFile(m_dragPath)});
        return;
    }
    m_dragStaging = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/bitshell-drag-XXXXXX"));
    if (!m_dragStaging->isValid()) { setLastTransferText(QStringLiteral("Cannot prepare the private drag cache.")); return; }
    QString privacyError;
    if (!PrivateFile::secureEntry(m_dragStaging->path(), privacyError)) {
        m_dragStaging.reset(); setLastTransferText(privacyError); return;
    }
    const QString staging = m_dragStaging->path();
    m_preparingDrag = true; m_pendingDragKey = key;
    downloadTo(row, QUrl::fromLocalFile(staging));
    if (!busy()) { m_preparingDrag = false; return; }
    setStatusText(QStringLiteral("Preparing drag… keep the mouse held, or drag again when ready."));
}

void SftpController::startListing(const QString& requestedPath, bool connecting) {
    m_requestedPath = requestedPath;
    QStringList commands;
    if (!requestedPath.isEmpty()) {
        commands << QStringLiteral("cd %1").arg(quoteSftpArgument(requestedPath));
    }
    commands << QStringLiteral("pwd") << QStringLiteral("ls -la");
    setState(connecting ? State::Connecting : State::Busy);
    startBatch(Operation::List,
               commands,
               connecting ? QStringLiteral("Connecting to %1…").arg(m_target)
                          : QStringLiteral("Loading %1…")
                                .arg(requestedPath.isEmpty() ? QStringLiteral("home")
                                                             : requestedPath));
}

void SftpController::startBatch(Operation operation,
                                const QStringList& commands,
                                const QString& activityText) {
    if (m_process.state() != QProcess::NotRunning) {
        return;
    }

    const auto command = SshSecurity::command(m_profile, SshSecurity::Transport::Sftp);
    if (!command) { finishWithError(command.error()); return; }
    if (commands.size() > 50000) { finishWithError(QStringLiteral("Too many transfer commands.")); return; }
    for (const auto& line : commands) {
        if (!SshSecurity::safePath(line)) {
            finishWithError(QStringLiteral("Unsupported control character or overlong SFTP command.")); return;
        }
    }

    m_passwordBroker.reset();
    auto environment = SshSecurity::environment();
    if (m_profile.value(QStringLiteral("authenticationMode")).toString() == QStringLiteral("password")) {
        auto password = PasswordStore::read(m_profile);
        if (!password) { finishWithError(password.error()); return; }
        if (password->isEmpty()) {
            finishWithError(QStringLiteral("Save a password on this device in the SSH session before using password-authenticated file transfers."));
            return;
        }
        m_passwordBroker = std::make_unique<PasswordBroker>();
        const auto additions = m_passwordBroker->start(m_profile, *password);
        password->fill('\0');
        if (!additions) { finishWithError(additions.error()); return; }
        for (const auto& key : additions->keys()) environment.insert(key, additions->value(key));
    }
#ifdef Q_OS_WIN
    // Production SSH keeps a trusted PATH. The Windows test double is this Qt
    // executable, and the loader will not find Qt beside it once PATH is replaced.
    if (!m_testTransport.isEmpty()) {
        const auto path = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
        if (!path.isEmpty()) environment.insert(QStringLiteral("PATH"), path);
    }
#endif

    m_operation = operation;
    m_standardOutput.clear();
    m_standardError.clear();
    m_pendingBatch.clear();
    m_processFailureHandled = false;
    setStatusText(activityText);
    if (m_state != State::Connecting) {
        setState(State::Busy);
    }

    m_process.setProgram(m_testTransport.isEmpty() ? command->program : m_testTransport);
    m_process.setArguments(command->arguments);
    m_process.setProcessEnvironment(environment);
    for (const auto& line : commands) {
        m_pendingBatch += line.toUtf8();
        m_pendingBatch += '\n';
    }
    m_pendingBatch += "quit\n";
    m_process.start();
    m_timeout.start(operation == Operation::List ? 30000 : 30 * 60 * 1000);
    m_process.write(m_pendingBatch);
    m_process.closeWriteChannel();
    m_pendingBatch.clear();
}

void SftpController::handleFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    m_passwordBroker.reset();
    m_process.setProcessEnvironment(SshSecurity::environment());
    m_timeout.stop();
    m_standardOutput += m_process.readAllStandardOutput();
    m_standardError += m_process.readAllStandardError();
    m_pendingBatch.clear();
    if (m_processFailureHandled) {
        return;
    }

    const Operation completedOperation = m_operation;
    m_operation = Operation::None;
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        m_preparingDrag = false;
        finishWithError(QString::fromUtf8(m_standardError + m_standardOutput));
        return;
    }

    if (completedOperation == Operation::List) {
        applyListing(m_standardOutput);
        return;
    }

    setState(State::Ready);
    if (!m_connected) {
        m_connected = true;
        emit connectedChanged();
    }
    if (completedOperation == Operation::Download) {
        if (!m_downloadStaging || !regularTree(m_transferLocalPath, true)) {
            finishWithError(QStringLiteral("Downloaded data failed validation. Nothing was published to the destination.")); return;
        }
        // Atomic no-replace publication, including when a destination symlink
        // or file appears after the user initiated the download.
        bool published = false;
        for (int attempt = 0; attempt < 100; ++attempt) {
            const QString destination = LocalFileModel::availablePath(m_downloadDestination, m_transferName);
            if (destination.isEmpty()) break;
            bool collision = false; QString publicationError;
            if (PrivateFile::publishNoReplace(m_transferLocalPath, destination, collision, publicationError)) {
                m_transferLocalPath = destination; published = true; break;
            }
            if (!collision) break;
        }
        if (!published) {
            finishWithError(QStringLiteral("Cannot publish the download without overwriting an existing file. Nothing was replaced.")); return;
        }
        m_downloadStaging.reset();
        setLastTransferText(QStringLiteral("Downloaded %1 to %2")
                                .arg(m_transferName, m_transferLocalPath));
        setStatusText(QStringLiteral("Download complete"));
        if (m_preparingDrag) {
            m_preparingDrag = false;
            m_dragKey = m_pendingDragKey; m_dragPath = m_transferLocalPath;
            setStatusText(QStringLiteral("Drag ready · drag this item to your desktop or a folder"));
            QTimer::singleShot(0, this, [this] {
                if (QGuiApplication::mouseButtons().testFlag(Qt::LeftButton))
                    LocalFileModel::dragUrls({QUrl::fromLocalFile(m_dragPath)});
            });
        }
        emit transferCompleted();
    } else if (completedOperation == Operation::Upload) {
        setLastTransferText(QStringLiteral("Uploaded %1").arg(m_transferName));
        startListing(m_currentPath, false);
        emit transferCompleted();
    }
}

void SftpController::handleProcessError(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart || m_processFailureHandled) {
        return;
    }
    m_processFailureHandled = true;
    m_operation = Operation::None;
    m_pendingBatch.clear();
    finishWithError(m_process.errorString());
}

void SftpController::finishWithError(const QString& output) {
    m_passwordBroker.reset();
    m_process.setProcessEnvironment(SshSecurity::environment());
    m_timeout.stop();
    m_historyTarget = -1;
    if (m_connected) {
        m_connected = false;
        emit connectedChanged();
    }
    setState(State::Error);
    setStatusText(friendlyError(output));
}

void SftpController::applyListing(const QByteArray& output) {
    static const QRegularExpression listingPattern(
        QStringLiteral("^([bcdlps-][rwxStTs-]{9})\\s+(?:\\d+|\\?)\\s+\\S+\\s+\\S+\\s+"
                       "(\\d+)\\s+(\\S+\\s+\\d+\\s+\\S+)\\s+(.+)$"));

    QVector<Entry> entries;
    QString resolvedPath = m_requestedPath;
    const QString decoded = QString::fromUtf8(output);
    const auto lines = decoded.split(u'\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.startsWith(QLatin1StringView(kRemoteWorkingDirectoryPrefix))) {
            resolvedPath = line.sliced(
                static_cast<qsizetype>(std::char_traits<char>::length(
                    kRemoteWorkingDirectoryPrefix)));
            if (!resolvedPath.startsWith(u'/') || !SshSecurity::safePath(resolvedPath)) {
                finishWithError(QStringLiteral("The server returned an unsafe working directory.")); return;
            }
            continue;
        }

        const auto match = listingPattern.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        QString name = match.captured(4);
        if (name == QStringLiteral(".") || name == QStringLiteral("..")) {
            continue;
        }
        const QString permissions = match.captured(1);
        if (permissions.startsWith(u'l')) {
            const qsizetype arrow = name.indexOf(QStringLiteral(" -> "));
            if (arrow > 0) {
                name.truncate(arrow);
            }
        }
        if (name.isEmpty() || name.contains(u'/') || !SshSecurity::safePath(name) || entries.size() >= 20000) {
            finishWithError(QStringLiteral("Unsafe or oversized directory listing. No files were transferred.")); return;
        }
        bool sizeValid = false;
        const qint64 size = match.captured(2).toLongLong(&sizeValid);
        entries.push_back(Entry{
            .name = name,
            .directory = permissions.startsWith(u'd'),
            .size = sizeValid ? size : 0,
            .modified = match.captured(3),
            .permissions = permissions,
        });
    }

    std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) {
        if (left.directory != right.directory) {
            return left.directory;
        }
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });

    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
    emit countChanged();
    if (m_historyTarget >= 0) {
        m_historyPosition = m_historyTarget;
        m_historyTarget = -1;
    } else if (m_historyPosition < 0 || m_history.value(m_historyPosition) != resolvedPath) {
        m_history = m_history.mid(0, m_historyPosition + 1);
        m_history.append(resolvedPath);
        m_historyPosition = static_cast<int>(m_history.size()) - 1;
    }
    if (m_currentPath != resolvedPath) {
        m_currentPath = resolvedPath;
        emit currentPathChanged();
    }
    if (!m_connected) {
        m_connected = true;
        emit connectedChanged();
    }
    setState(State::Ready);
    setStatusText(QStringLiteral("%1 item%2")
                      .arg(m_entries.size())
                      .arg(m_entries.size() == 1 ? QString{} : QStringLiteral("s")));
}

void SftpController::setState(State state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit stateChanged();
}

void SftpController::setStatusText(const QString& text) {
    if (m_statusText == text) {
        return;
    }
    m_statusText = text;
    emit statusTextChanged();
}

void SftpController::setLastTransferText(const QString& text) {
    if (m_lastTransferText == text) {
        return;
    }
    m_lastTransferText = text;
    emit lastTransferTextChanged();
}

void SftpController::clearEntries() {
    if (m_entries.isEmpty()) {
        return;
    }
    beginResetModel();
    m_entries.clear();
    endResetModel();
    emit countChanged();
}

QString SftpController::workingDirectoryCommand() const {
    return m_currentPath.isEmpty()
               ? QString{}
               : QStringLiteral("cd %1").arg(quoteSftpArgument(m_currentPath));
}

QString SftpController::availableDownloadPath(const QString& fileName) const {
    QDir directory(downloadDirectory());
    (void)directory.mkpath(QStringLiteral("."));
    const QString safeName = QFileInfo(fileName).fileName().isEmpty()
                                 ? QStringLiteral("download")
                                 : QFileInfo(fileName).fileName();
    QString candidate = directory.filePath(safeName);
    if (!QFileInfo::exists(candidate)) {
        return candidate;
    }

    const QFileInfo info(safeName);
    const QString stem = info.completeBaseName().isEmpty() ? safeName : info.completeBaseName();
    const QString suffix = info.completeSuffix();
    for (int copy = 1; copy < 10000; ++copy) {
        const QString numbered = suffix.isEmpty()
                                     ? QStringLiteral("%1 (%2)").arg(stem).arg(copy)
                                     : QStringLiteral("%1 (%2).%3").arg(stem).arg(copy).arg(suffix);
        candidate = directory.filePath(numbered);
        if (!QFileInfo::exists(candidate)) {
            return candidate;
        }
    }
    return directory.filePath(QStringLiteral("%1.download").arg(safeName));
}

QString SftpController::quoteSftpArgument(const QString& value) {
    QString escaped = value;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'\"', QStringLiteral("\\\""));
    escaped.remove(u'\r');
    escaped.remove(u'\n');
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString SftpController::quoteGlobArgument(const QString& value) {
    // OpenSSH makeargv() already escapes glob characters inside quotes.
    // Adding a second glob escape would request a different filename.
    return quoteSftpArgument(value);
}

QString SftpController::joinRemotePath(const QString& base, const QString& child) {
    if (base.isEmpty() || base == QStringLiteral(".")) {
        return child;
    }
    if (base == QStringLiteral("/")) {
        return QStringLiteral("/%1").arg(child);
    }
    return QStringLiteral("%1/%2").arg(base, child);
}

QString SftpController::parentRemotePath(const QString& path) {
    if (path.isEmpty() || path == QStringLiteral("/")) {
        return path;
    }
    QString normalized = path;
    while (normalized.size() > 1 && normalized.endsWith(u'/')) {
        normalized.chop(1);
    }
    const qsizetype slash = normalized.lastIndexOf(u'/');
    if (slash < 0) {
        return QStringLiteral(".");
    }
    return slash == 0 ? QStringLiteral("/") : normalized.first(slash);
}

QString SftpController::humanSize(qint64 bytes) {
    static const QStringList units{
        QStringLiteral("B"),
        QStringLiteral("KB"),
        QStringLiteral("MB"),
        QStringLiteral("GB"),
        QStringLiteral("TB"),
    };
    qreal value = static_cast<qreal>(std::max<qint64>(0, bytes));
    int unit = 0;
    while (value >= 1024.0 && unit < units.size() - 1) {
        value /= 1024.0;
        ++unit;
    }
    const int precision = unit == 0 || value >= 10.0 ? 0 : 1;
    return QStringLiteral("%1 %2")
        .arg(QLocale::c().toString(value, 'f', precision), units.at(unit));
}

QString SftpController::friendlyError(const QString& output) {
    const QString message = output.trimmed();
    if (message.contains(QStringLiteral("Host key verification failed"),
                         Qt::CaseInsensitive)) {
        return QStringLiteral("Host key verification failed. Ask your administrator to provision the verified host key or host CA in known_hosts. Never bypass a changed-key warning.");
    }
    if (message.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)) {
        return QStringLiteral("SFTP authentication failed. Check the session's SSH key or agent, or update its saved password in Edit session.");
    }
    if (message.contains(QStringLiteral("Could not resolve hostname"),
                         Qt::CaseInsensitive)) {
        return QStringLiteral("The SFTP host name could not be resolved.");
    }
    if (message.contains(QStringLiteral("Connection refused"), Qt::CaseInsensitive)) {
        return QStringLiteral("The remote host refused the SFTP connection.");
    }
    if (message.contains(QStringLiteral("No such file"), Qt::CaseInsensitive)) {
        return QStringLiteral("That remote path no longer exists. Refresh the folder.");
    }
    if (message.isEmpty()) {
        return QStringLiteral("The SFTP operation failed without an error message.");
    }
    const QString firstLine = message.section(u'\n', 0, 0).trimmed();
    return firstLine.size() > 220 ? firstLine.first(217) + QStringLiteral("…") : firstLine;
}

} // namespace bitshell::core
