#include <bitshell/core/SessionStore.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/ConnectionRouting.h>

#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QUuid>
#include <QXmlStreamReader>

#include <algorithm>
#include <functional>

namespace bitshell::core {

namespace {

bool validCatalogFolder(const QString& path) {
    return path.size() <= 1024 && path.count(u'/') + path.count(u'\\') < 64
        && SshSecurity::safePath(path);
}

QString parentFolder(const QString& path) {
    const qsizetype separator = path.lastIndexOf(u'/');
    return separator < 0 ? QString{} : path.left(separator);
}

QString folderDisplayName(const QString& path) {
    const qsizetype separator = path.lastIndexOf(u'/');
    return separator < 0 ? path : path.sliced(separator + 1);
}

bool isPathOrDescendant(const QString& candidate, const QString& parent) {
    return candidate == parent || candidate.startsWith(parent + u'/');
}

bool naturalLess(const QString& left, const QString& right) {
    static thread_local QCollator collator = [] {
        QCollator value;
        value.setCaseSensitivity(Qt::CaseInsensitive);
        value.setNumericMode(true);
        return value;
    }();
    return collator.compare(left, right) < 0;
}

QString localFilePath(const QString& source) {
    const QUrl url(source);
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return source;
}

} // namespace

SessionStore::SessionStore(QObject* parent)
    : QAbstractListModel(parent)
    , m_yaml(QDir(Platform::appConfigDirectory())
                 .filePath(QStringLiteral("sessions.yaml")), QStringLiteral("8bitshell/sessions"), 2) {
    const QString configRoot = Platform::appConfigDirectory();
    const auto invalidateIndex = [this] { m_treeIndexRevision = -1; };
    connect(this, &QAbstractItemModel::modelReset, this, invalidateIndex);
    connect(this, &QAbstractItemModel::rowsInserted, this, invalidateIndex);
    connect(this, &QAbstractItemModel::rowsRemoved, this, invalidateIndex);
    connect(this, &QAbstractItemModel::dataChanged, this, invalidateIndex);
    connect(this, &SessionStore::folderCountChanged, this, invalidateIndex);
    m_storagePath = m_yaml.fileName();
    const QString legacyPath =
        QDir(Platform::iniSettingsDirectory())
            .filePath(QStringLiteral("sessions.json"));
    const QString oldPath = QDir(configRoot).filePath(QStringLiteral("sessions.json"));
    if (!QFileInfo::exists(m_storagePath) && !QFileInfo::exists(oldPath) && QFileInfo::exists(legacyPath)) {
        QByteArray bytes;
        QString error;
        if (!PrivateFile::read(legacyPath, bytes, error)
            || !PrivateFile::write(oldPath, bytes, error)) {
            m_yaml.reject(error);
            setLastError(error);
            return;
        }
    }
    (void)reload();
}

int SessionStore::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_sessions.size());
}

QVariant SessionStore::data(const QModelIndex& index, int role) const {
    if (!isValidRow(index.row())) {
        return {};
    }
    const auto& session = m_sessions.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return session.name;
    case IdRole:
        return session.id;
    case HostRole:
        return session.host;
    case UserRole:
        return session.user;
    case PortRole:
        return session.port;
    case GroupRole:
        return session.group;
    case TagsRole:
        return session.tags;
    case IdentityKeyRole:
        return session.identityKey;
    case IdentityFileRole:
        return session.identityFile;
    case ProxyJumpRole:
        return session.proxyJump;
    case ForwardX11Role:
        return session.forwardX11;
    case AgentForwardingRole:
        return session.agentForwarding;
    case CompressionRole:
        return session.compression;
    case BackspaceModeRole:
        return session.backspaceMode;
    default:
        return {};
    }
}

QHash<int, QByteArray> SessionStore::roleNames() const {
    return {
        {IdRole, QByteArrayLiteral("sessionId")},
        {NameRole, QByteArrayLiteral("name")},
        {HostRole, QByteArrayLiteral("host")},
        {UserRole, QByteArrayLiteral("user")},
        {PortRole, QByteArrayLiteral("port")},
        {GroupRole, QByteArrayLiteral("group")},
        {TagsRole, QByteArrayLiteral("tags")},
        {IdentityKeyRole, QByteArrayLiteral("identityKey")},
        {IdentityFileRole, QByteArrayLiteral("identityFile")},
        {ProxyJumpRole, QByteArrayLiteral("proxyJump")},
        {ForwardX11Role, QByteArrayLiteral("forwardX11")},
        {AgentForwardingRole, QByteArrayLiteral("agentForwarding")},
        {CompressionRole, QByteArrayLiteral("compression")},
        {BackspaceModeRole, QByteArrayLiteral("backspaceMode")},
    };
}

int SessionStore::count() const {
    return static_cast<int>(m_sessions.size());
}

int SessionStore::folderCount() const {
    return static_cast<int>(m_folders.size());
}

int SessionStore::revision() const {
    return m_revision;
}

QString SessionStore::storagePath() const {
    return m_storagePath;
}

QString SessionStore::lastError() const {
    return m_lastError;
}

QString SessionStore::lastImportSummary() const {
    return m_lastImportSummary;
}

QVariantMap SessionStore::get(int row) const {
    return isValidRow(row) ? toMap(m_sessions.at(row)) : QVariantMap{};
}

QVariantMap SessionStore::getById(const QString& id) const {
    for (const auto& session : m_sessions) if (session.id == id) return toMap(session);
    return {};
}

int SessionStore::find(const QString& query) const {
    QString needle = query.trimmed();
    if (needle.startsWith(QStringLiteral("ssh ")) && !needle.sliced(4).trimmed().contains(u' ')) needle = needle.sliced(4).trimmed();
    if (needle.isEmpty()) {
        return -1;
    }
    for (int row = 0; row < static_cast<int>(m_sessions.size()); ++row) {
        const auto& session = m_sessions.at(row);
        const QString sshTarget = session.user.isEmpty()
                                      ? session.host
                                      : QStringLiteral("%1@%2").arg(session.user, session.host);
        if (session.id.compare(needle, Qt::CaseInsensitive) == 0
            || session.name.compare(needle, Qt::CaseInsensitive) == 0
            || session.host.compare(needle, Qt::CaseInsensitive) == 0
            || sshTarget.compare(needle, Qt::CaseInsensitive) == 0) {
            return row;
        }
    }
    return -1;
}

void SessionStore::prepareTreeIndex() const {
    if (m_treeIndexRevision == m_revision) return;
    QSet<QString> folders;
    for (const auto& folder : m_folders) {
        const QString normalized = normalizeFolderPath(folder);
        if (!normalized.isEmpty()) {
            folders.insert(normalized);
        }
    }
    for (const auto& session : m_sessions) {
        QString current = normalizeFolderPath(session.group);
        while (!current.isEmpty()) {
            folders.insert(current);
            current = parentFolder(current);
        }
    }

    QHash<QString, QStringList> children;
    for (const auto& folder : folders) {
        children[parentFolder(folder)].push_back(folder);
    }
    for (auto iterator = children.begin(); iterator != children.end(); ++iterator) {
        std::sort(iterator.value().begin(), iterator.value().end(),
                  [](const QString& left, const QString& right) {
                      return naturalLess(folderDisplayName(left), folderDisplayName(right));
                  });
    }

    QHash<QString, QVector<int>> sessionsByFolder;
    QHash<QString, int> descendantSessionCounts;
    for (int row = 0; row < static_cast<int>(m_sessions.size()); ++row) {
        const QString folder = normalizeFolderPath(m_sessions.at(row).group);
        sessionsByFolder[folder].push_back(row);
        QString current = folder;
        while (!current.isEmpty()) {
            ++descendantSessionCounts[current];
            current = parentFolder(current);
        }
    }
    for (auto iterator = sessionsByFolder.begin(); iterator != sessionsByFolder.end();
         ++iterator) {
        std::sort(iterator.value().begin(), iterator.value().end(),
                  [this](int left, int right) {
                      return naturalLess(m_sessions.at(left).name, m_sessions.at(right).name);
                  });
    }

    m_treeFolders = std::move(folders);
    m_treeChildren = std::move(children);
    m_treeSessions = std::move(sessionsByFolder);
    m_treeCounts = std::move(descendantSessionCounts);
    m_treeSearchText.clear();
    m_treeSearchText.reserve(m_sessions.size());
    for (const auto& session : m_sessions) {
        m_treeSearchText.append(QStringList{session.id, session.name, session.host, session.user,
            session.group, session.tags.join(u' ')}.join(u' ').toCaseFolded());
    }
    m_treeIndexRevision = m_revision;
}

QVariantMap SessionStore::search(const QString& query, int limit) const {
    if (query.size() > 256) return {{QStringLiteral("results"), QVariantList{}}, {QStringLiteral("total"), 0}};
    QString needle = query.simplified().toCaseFolded();
    if (needle.startsWith(QStringLiteral("ssh "))) needle = needle.sliced(4).trimmed();
    if (needle.isEmpty()) return {{QStringLiteral("results"), QVariantList{}}, {QStringLiteral("total"), 0}};
    prepareTreeIndex();
    const auto terms = needle.split(u' ', Qt::SkipEmptyParts);
    struct Match { int row; int rank; QString foldedName; };
    QVector<Match> matches;
    matches.reserve(m_sessions.size());
    for (int row = 0; row < m_sessions.size(); ++row) {
        const auto& session = m_sessions.at(row);
        const auto target = session.user.isEmpty() ? session.host : session.user + u'@' + session.host;
        const auto searchable = m_treeSearchText.at(row) + u' ' + target.toCaseFolded();
        if (!std::all_of(terms.cbegin(), terms.cend(), [&](const QString& term) { return searchable.contains(term); })) continue;
        const auto name = session.name.toCaseFolded(), host = session.host.toCaseFolded();
        const int rank = needle == name || needle == host || needle == target.toCaseFolded() ? 0
            : name.startsWith(needle) || host.startsWith(needle) ? 1 : 2;
        matches.push_back({row, rank, name});
    }
    const auto resultCount = std::min(matches.size(), qsizetype(std::clamp(limit, 1, 200)));
    // Only the visible result window needs sorting, even for a broad fleet-wide query.
    std::partial_sort(matches.begin(), matches.begin() + resultCount, matches.end(), [&](const Match& a, const Match& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.foldedName == b.foldedName ? m_sessions.at(a.row).id < m_sessions.at(b.row).id
                                          : naturalLess(a.foldedName, b.foldedName);
    });
    QVariantList results;
    results.reserve(resultCount);
    for (int index = 0; index < resultCount; ++index)
        results << toMap(m_sessions.at(matches[index].row));
    return {{QStringLiteral("results"), results}, {QStringLiteral("total"), matches.size()}};
}

QVariantList SessionStore::treeRows(const QString& filter,
                                    const QStringList& expandedFolders) const {
    prepareTreeIndex();
    const auto& folders = m_treeFolders;
    const auto& children = m_treeChildren;
    const auto& sessionsByFolder = m_treeSessions;
    const auto& descendantSessionCounts = m_treeCounts;

    QSet<QString> expanded;
    for (const auto& folder : expandedFolders) {
        expanded.insert(normalizeFolderPath(folder));
    }

    const QString needle = filter.trimmed().toCaseFolded();
    const auto terms = needle.simplified().split(u' ', Qt::SkipEmptyParts);
    QVector<bool> matchingSessions(m_sessions.size(), needle.isEmpty());
    QSet<QString> visibleFolders;
    if (!needle.isEmpty()) {
        for (int row = 0; row < static_cast<int>(m_sessions.size()); ++row) {
            const auto& session = m_sessions.at(row);
            if (!std::all_of(terms.cbegin(), terms.cend(), [&](const QString& term) { return m_treeSearchText.at(row).contains(term); })) {
                continue;
            }
            matchingSessions[row] = true;
            QString current = normalizeFolderPath(session.group);
            while (!current.isEmpty()) {
                visibleFolders.insert(current);
                current = parentFolder(current);
            }
        }
        for (const auto& folder : folders) {
            if (!folder.toCaseFolded().contains(needle)) {
                continue;
            }
            // Descendant paths already contain the matching ancestor text. Their
            // own pass (and each session's indexed full path) includes them without
            // scanning every folder and session again for each matched folder.
            QString current = folder;
            while (!current.isEmpty()) {
                visibleFolders.insert(current);
                current = parentFolder(current);
            }
        }
    }

    QVariantList result;
    const auto appendSessions = [this,
                                 &result,
                                 &sessionsByFolder,
                                 &matchingSessions,
                                 &needle](const QString& folder, int depth) {
        for (const int row : sessionsByFolder.value(folder)) {
            if (!needle.isEmpty() && !matchingSessions.at(row)) {
                continue;
            }
            const auto& session = m_sessions.at(row);
            QVariantMap item = toMap(session);
            item.insert(QStringLiteral("kind"), QStringLiteral("session"));
            item.insert(QStringLiteral("key"), QStringLiteral("session:%1").arg(session.id));
            item.insert(QStringLiteral("depth"), depth);
            item.insert(QStringLiteral("sourceIndex"), row);
            result.push_back(item);
        }
    };

    std::function<void(const QString&, int)> appendFolderChildren;
    appendFolderChildren = [this,
                            &appendFolderChildren,
                            &appendSessions,
                            &children,
                            &descendantSessionCounts,
                            &expanded,
                            &needle,
                            &result,
                            &sessionsByFolder,
                            &visibleFolders](const QString& parent, int depth) {
        for (const auto& folder : children.value(parent)) {
            if (!needle.isEmpty() && !visibleFolders.contains(folder)) {
                continue;
            }
            const bool isExpanded = !needle.isEmpty() || expanded.contains(folder);
            result.push_back(QVariantMap{
                {QStringLiteral("kind"), QStringLiteral("folder")},
                {QStringLiteral("key"), QStringLiteral("folder:%1").arg(folder)},
                {QStringLiteral("name"), folderDisplayName(folder)},
                {QStringLiteral("path"), folder},
                {QStringLiteral("depth"), depth},
                {QStringLiteral("expanded"), isExpanded},
                {QStringLiteral("directCount"), sessionsByFolder.value(folder).size()},
                {QStringLiteral("totalCount"), descendantSessionCounts.value(folder)},
                {QStringLiteral("childFolderCount"), children.value(folder).size()},
            });
            if (!isExpanded) {
                continue;
            }
            appendFolderChildren(folder, depth + 1);
            appendSessions(folder, depth + 1);
        }
    };

    appendFolderChildren({}, 0);
    appendSessions({}, 0);
    return result;
}

QStringList SessionStore::folderPaths() const {
    QStringList folders = m_folders;
    std::sort(folders.begin(), folders.end(), [](const QString& left, const QString& right) {
        return naturalLess(left, right);
    });
    return folders;
}

int SessionStore::addSession(const QVariantMap& values) {
    Session session = fromMap(values);
    if (session.host.isEmpty()) {
        setLastError(QStringLiteral("Host is required"));
        return -1;
    }
    if (session.name.isEmpty()) {
        session.name = session.host;
    }
    session.group = normalizeFolderPath(session.group);

    const QStringList previousFolders = m_folders;
    ensureFolderAncestors(session.group);
    const int row = static_cast<int>(m_sessions.size());
    beginInsertRows({}, row, row);
    m_sessions.push_back(std::move(session));
    endInsertRows();
    if (!save()) {
        beginRemoveRows({}, row, row);
        m_sessions.removeLast();
        endRemoveRows();
        m_folders = previousFolders;
        return -1;
    }
    setLastError({});
    emit countChanged();
    if (m_folders != previousFolders) {
        emit folderCountChanged();
    }
    advanceRevision();
    return row;
}

bool SessionStore::updateSession(int row, const QVariantMap& values) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Session does not exist"));
        return false;
    }
    Session updated = fromMap(values, &m_sessions.at(row));
    if (updated.host.isEmpty()) {
        setLastError(QStringLiteral("Host is required"));
        return false;
    }
    if (updated.name.isEmpty()) {
        updated.name = updated.host;
    }
    updated.group = normalizeFolderPath(updated.group);

    const Session previous = m_sessions.at(row);
    const QStringList previousFolders = m_folders;
    ensureFolderAncestors(updated.group);
    m_sessions[row] = std::move(updated);
    emit dataChanged(index(row), index(row));
    if (!save()) {
        m_sessions[row] = previous;
        m_folders = previousFolders;
        emit dataChanged(index(row), index(row));
        return false;
    }
    setLastError({});
    if (m_folders != previousFolders) {
        emit folderCountChanged();
    }
    advanceRevision();
    return true;
}

bool SessionStore::removeSession(int row) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Session does not exist"));
        return false;
    }
    const Session removed = m_sessions.at(row);
    beginRemoveRows({}, row, row);
    m_sessions.removeAt(row);
    endRemoveRows();
    if (!save()) {
        beginInsertRows({}, row, row);
        m_sessions.insert(row, removed);
        endInsertRows();
        return false;
    }
    setLastError({});
    emit countChanged();
    advanceRevision();
    return true;
}

bool SessionStore::collectDeletion(const QStringList& sessionIds, const QStringList& folders,
                                   bool all, QSet<QString>& removedIds,
                                   QSet<QString>& removedFolders, QString& error) const {
    if (sessionIds.size() > 10000 || folders.size() > 10000
        || (all && (!sessionIds.isEmpty() || !folders.isEmpty()))) {
        error = QStringLiteral("Invalid deletion selection.");
        return false;
    }
    QSet<QString> existingIds;
    for (const auto& session : m_sessions) existingIds.insert(session.id);
    const QSet<QString> existingFolders(m_folders.cbegin(), m_folders.cend());
    QSet<QString> selectedFolders;
    for (const auto& id : sessionIds) {
        if (!existingIds.contains(id)) {
            error = QStringLiteral("A selected session no longer exists. Review the selection again.");
            return false;
        }
        removedIds.insert(id);
    }
    for (const auto& path : folders) {
        if (path.isEmpty() || path != normalizeFolderPath(path) || !existingFolders.contains(path)) {
            error = QStringLiteral("A selected folder no longer exists. Review the selection again.");
            return false;
        }
        selectedFolders.insert(path);
    }
    if (all) {
        removedIds = existingIds;
        removedFolders = existingFolders;
    } else {
        // Walk bounded ancestors: selecting a thousand folders must not cause
        // a quadratic rescan of every session or one disk write per deletion.
        for (const auto& folder : m_folders) {
            QString parent = folder;
            while (!parent.isEmpty()) {
                if (selectedFolders.contains(parent)) {
                    removedFolders.insert(folder);
                    break;
                }
                parent = parentFolder(parent);
            }
        }
        for (const auto& session : m_sessions)
            if (removedFolders.contains(session.group)) removedIds.insert(session.id);
    }
    if (removedIds.isEmpty() && removedFolders.isEmpty()) {
        error = QStringLiteral("Select at least one saved session or folder.");
        return false;
    }
    return true;
}

QVariantMap SessionStore::deletionPreview(const QStringList& sessionIds,
                                         const QStringList& folders, bool all) const {
    QSet<QString> removedIds, removedFolders;
    QString error;
    const bool valid = collectDeletion(sessionIds, folders, all, removedIds, removedFolders, error);
    return {{QStringLiteral("valid"), valid}, {QStringLiteral("error"), error},
        {QStringLiteral("sessions"), removedIds.size()}, {QStringLiteral("folders"), removedFolders.size()},
        {QStringLiteral("revision"), m_revision}};
}

bool SessionStore::deleteEntries(const QStringList& sessionIds, const QStringList& folders,
                                 bool all, int expectedRevision) {
    if (expectedRevision != m_revision) {
        setLastError(QStringLiteral("The catalog changed while confirming deletion. Review the selection again."));
        return false;
    }
    QSet<QString> removedIds, removedFolders;
    QString error;
    if (!collectDeletion(sessionIds, folders, all, removedIds, removedFolders, error)) {
        setLastError(error);
        return false;
    }
    auto nextSessions = m_sessions;
    nextSessions.removeIf([&](const Session& session) { return removedIds.contains(session.id); });
    auto nextFolders = m_folders;
    nextFolders.removeIf([&](const QString& path) { return removedFolders.contains(path); });
    auto nextConnections = m_folderConnections;
    for (const auto& folder : removedFolders) nextConnections.remove(folder);

    auto document = yamlDocument();
    QVariantList entries;
    for (const auto& entry : document.value(QStringLiteral("sessions")).toList())
        if (!removedIds.contains(entry.toMap().value(QStringLiteral("id")).toString())) entries.append(entry);
    document[QStringLiteral("sessions")] = entries;
    document[QStringLiteral("folders")] = nextFolders;
    document[QStringLiteral("folderConnections")] = nextConnections;
    if (!validateYaml(document, error)) { setLastError(error); return false; }
    // Save once, atomically and with YamlFile's external-edit conflict check.
    // No model signal or in-memory deletion occurs until persistence succeeds.
    if (!m_yaml.save(document)) { setLastError(m_yaml.error()); return false; }

    beginResetModel();
    m_sessions = std::move(nextSessions);
    m_folders = std::move(nextFolders);
    m_folderConnections = std::move(nextConnections);
    endResetModel();
    setLastError({});
    setLastImportSummary({});
    if (!removedIds.isEmpty()) emit countChanged();
    if (!removedFolders.isEmpty()) emit folderCountChanged();
    advanceRevision();
    QStringList roots;
    for (const auto& folder : removedFolders)
        if (!removedFolders.contains(parentFolder(folder))) roots.append(folder);
    if (!roots.isEmpty()) emit foldersRemoved(roots);
    return true;
}

int SessionStore::duplicateSession(int row) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Session does not exist"));
        return -1;
    }
    QVariantMap copy = toMap(m_sessions.at(row));
    copy.remove(QStringLiteral("id"));
    copy[QStringLiteral("name")] =
        QStringLiteral("%1 copy").arg(copy.value(QStringLiteral("name")).toString());
    return addSession(copy);
}

bool SessionStore::moveSession(int row, const QString& folderPath) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Session does not exist"));
        return false;
    }

    QString destination = normalizeFolderPath(folderPath);
    if (!destination.isEmpty()) {
        const auto destinationFolder = std::find_if(
            m_folders.cbegin(), m_folders.cend(), [&destination](const QString& candidate) {
                return candidate.compare(destination, Qt::CaseInsensitive) == 0;
            });
        if (destinationFolder == m_folders.cend()) {
            setLastError(QStringLiteral("Destination folder does not exist"));
            return false;
        }
        destination = *destinationFolder;
    }

    const QString previousGroup = m_sessions.at(row).group;
    if (previousGroup == destination) {
        setLastError({});
        return true;
    }

    m_sessions[row].group = destination;
    emit dataChanged(index(row), index(row), {GroupRole});
    if (!save()) {
        m_sessions[row].group = previousGroup;
        emit dataChanged(index(row), index(row), {GroupRole});
        return false;
    }

    setLastError({});
    advanceRevision();
    return true;
}

bool SessionStore::addFolder(const QString& parentPath, const QString& name) {
    const QString parent = normalizeFolderPath(parentPath);
    const QString leaf = name.trimmed();
    if (leaf.isEmpty() || leaf == QStringLiteral(".") || leaf == QStringLiteral("..")
        || leaf.contains(u'/') || leaf.contains(u'\\')) {
        setLastError(QStringLiteral(
            "Folder name cannot be empty, a dot path, or contain slashes"));
        return false;
    }
    const QString path = parent.isEmpty() ? leaf : QStringLiteral("%1/%2").arg(parent, leaf);
    if (!validCatalogFolder(path)) { setLastError(QStringLiteral("Folder paths allow at most 1024 characters and 64 levels.")); return false; }
    if (m_folders.contains(path, Qt::CaseInsensitive)) {
        setLastError(QStringLiteral("A folder with this name already exists"));
        return false;
    }

    const QStringList previousFolders = m_folders;
    ensureFolderAncestors(path);
    if (!save()) {
        m_folders = previousFolders;
        return false;
    }
    setLastError({});
    emit folderCountChanged();
    advanceRevision();
    return true;
}

bool SessionStore::renameFolder(const QString& path, const QString& newName) {
    const QString oldPath = normalizeFolderPath(path);
    const QString leaf = newName.trimmed();
    if (oldPath.isEmpty() || leaf.isEmpty() || leaf == QStringLiteral(".")
        || leaf == QStringLiteral("..") || leaf.contains(u'/') || leaf.contains(u'\\')) {
        setLastError(QStringLiteral("Enter a folder name without slashes or dot paths"));
        return false;
    }
    const QString parent = parentFolder(oldPath);
    const QString newPath = parent.isEmpty() ? leaf : QStringLiteral("%1/%2").arg(parent, leaf);
    if (newPath.compare(oldPath, Qt::CaseInsensitive) == 0) {
        setLastError({});
        return true;
    }
    const bool collision = std::any_of(m_folders.cbegin(), m_folders.cend(),
                                       [&newPath, &oldPath](const QString& candidate) {
                                           return !isPathOrDescendant(candidate, oldPath)
                                                  && candidate.compare(newPath,
                                                                       Qt::CaseInsensitive) == 0;
                                       });
    if (collision) {
        setLastError(QStringLiteral("A folder with this name already exists"));
        return false;
    }

    const QStringList previousFolders = m_folders;
    const QVector<Session> previousSessions = m_sessions;
    const QVariantMap previousConnections = m_folderConnections;
    const auto replacePrefix = [&oldPath, &newPath](const QString& candidate) {
        return candidate == oldPath
                   ? newPath
                   : QStringLiteral("%1%2").arg(newPath, candidate.sliced(oldPath.size()));
    };
    for (const auto& key : previousConnections.keys()) if (isPathOrDescendant(key, oldPath)) {
        m_folderConnections.remove(key); m_folderConnections[replacePrefix(key)] = previousConnections.value(key);
    }
    for (auto& folder : m_folders) {
        if (isPathOrDescendant(folder, oldPath)) {
            folder = replacePrefix(folder);
        }
    }
    for (auto& session : m_sessions) {
        if (isPathOrDescendant(session.group, oldPath)) {
            session.group = replacePrefix(session.group);
        }
    }
    m_folders.removeDuplicates();
    if (!save()) {
        m_folders = previousFolders;
        m_sessions = previousSessions;
        m_folderConnections = previousConnections;
        return false;
    }
    if (!m_sessions.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_sessions.size()) - 1),
                         {GroupRole});
    }
    setLastError({});
    advanceRevision();
    emit folderMoved(oldPath, newPath);
    return true;
}

bool SessionStore::moveFolder(const QString& path,
                              const QString& destinationParentPath) {
    const QString requestedSource = normalizeFolderPath(path);
    const auto sourceFolder = std::find_if(
        m_folders.cbegin(), m_folders.cend(), [&requestedSource](const QString& candidate) {
            return candidate.compare(requestedSource, Qt::CaseInsensitive) == 0;
        });
    if (requestedSource.isEmpty() || sourceFolder == m_folders.cend()) {
        setLastError(QStringLiteral("Folder does not exist"));
        return false;
    }
    const QString source = *sourceFolder;

    QString destination = normalizeFolderPath(destinationParentPath);
    if (!destination.isEmpty()) {
        const auto destinationFolder = std::find_if(
            m_folders.cbegin(), m_folders.cend(), [&destination](const QString& candidate) {
                return candidate.compare(destination, Qt::CaseInsensitive) == 0;
            });
        if (destinationFolder == m_folders.cend()) {
            setLastError(QStringLiteral("Destination folder does not exist"));
            return false;
        }
        destination = *destinationFolder;
    }

    const QString foldedSource = source.toCaseFolded();
    const QString foldedDestination = destination.toCaseFolded();
    if (foldedDestination == foldedSource
        || foldedDestination.startsWith(foldedSource + u'/')) {
        setLastError(QStringLiteral("A folder cannot be moved inside itself"));
        return false;
    }

    const QString leaf = folderDisplayName(source);
    const QString newPath = destination.isEmpty()
                                ? leaf
                                : QStringLiteral("%1/%2").arg(destination, leaf);
    if (newPath == source) {
        setLastError({});
        return true;
    }

    const bool collision = std::any_of(
        m_folders.cbegin(), m_folders.cend(), [&newPath, &source](const QString& candidate) {
            return !isPathOrDescendant(candidate, source)
                   && candidate.compare(newPath, Qt::CaseInsensitive) == 0;
        });
    if (collision) {
        setLastError(QStringLiteral("A folder with this name already exists at the destination"));
        return false;
    }

    const QStringList previousFolders = m_folders;
    const QVector<Session> previousSessions = m_sessions;
    const QVariantMap previousConnections = m_folderConnections;
    const auto replacePrefix = [&source, &newPath](const QString& candidate) {
        return candidate == source
                   ? newPath
                   : QStringLiteral("%1%2").arg(newPath,
                                                 candidate.sliced(source.size()));
    };
    for (const auto& key : previousConnections.keys()) if (isPathOrDescendant(key, source)) {
        m_folderConnections.remove(key); m_folderConnections[replacePrefix(key)] = previousConnections.value(key);
    }
    for (auto& folder : m_folders) {
        if (isPathOrDescendant(folder, source)) {
            folder = replacePrefix(folder);
        }
    }
    for (auto& session : m_sessions) {
        if (isPathOrDescendant(session.group, source)) {
            session.group = replacePrefix(session.group);
        }
    }
    m_folders.removeDuplicates();

    if (!save()) {
        m_folders = previousFolders;
        m_sessions = previousSessions;
        m_folderConnections = previousConnections;
        return false;
    }
    if (!m_sessions.isEmpty()) {
        emit dataChanged(index(0), index(static_cast<int>(m_sessions.size()) - 1),
                         {GroupRole});
    }
    setLastError({});
    advanceRevision();
    emit folderMoved(source, newPath);
    return true;
}

bool SessionStore::removeFolder(const QString& path) {
    const QString normalized = normalizeFolderPath(path);
    if (normalized.isEmpty() || !m_folders.contains(normalized)) {
        setLastError(QStringLiteral("Folder does not exist"));
        return false;
    }
    const bool containsSessions = std::any_of(
        m_sessions.cbegin(), m_sessions.cend(), [&normalized](const Session& session) {
            return isPathOrDescendant(session.group, normalized);
        });
    if (containsSessions) {
        setLastError(QStringLiteral("Move or delete the sessions in this folder first"));
        return false;
    }

    const QStringList previousFolders = m_folders;
    const QVariantMap previousConnections = m_folderConnections;
    for (const auto& key : previousConnections.keys()) if (isPathOrDescendant(key, normalized)) m_folderConnections.remove(key);
    m_folders.removeIf([&normalized](const QString& candidate) {
        return isPathOrDescendant(candidate, normalized);
    });
    if (!save()) {
        m_folders = previousFolders;
        m_folderConnections = previousConnections;
        return false;
    }
    setLastError({});
    emit folderCountChanged();
    advanceRevision();
    emit folderRemoved(normalized);
    return true;
}

int SessionStore::importOpenSshConfig() {
    setLastImportSummary({});
    QFile file(QDir::home().filePath(QStringLiteral(".ssh/config")));
    if (!file.exists()) {
        setLastError(QStringLiteral("~/.ssh/config was not found"));
        return 0;
    }
    QByteArray configBytes;
    QString readError;
    if (!PrivateFile::read(file.fileName(), configBytes, readError)) { setLastError(readError); return 0; }

    struct HostBlock {
        QStringList aliases;
        QString hostName;
        QString user;
        int port = 22;
        QString identityFile;
        QString proxyJump;
        bool forwardX11 = false;
    } block;

    QVector<Session> imported;
    const auto flushBlock = [&block, &imported] {
        for (const auto& alias : block.aliases) {
            if (alias.contains(u'*') || alias.contains(u'?') || alias.startsWith(u'!')) {
                continue;
            }
            Session session;
            session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            session.name = alias;
            session.host = block.hostName.isEmpty() ? alias : block.hostName;
            session.user = block.user;
            session.port = block.port;
            session.group = QStringLiteral("SSH Config");
            session.identityFile = block.identityFile;
            session.proxyJump = block.proxyJump;
            session.forwardX11 = block.forwardX11;
            imported.push_back(std::move(session));
        }
        block = {};
    };

    QTextStream stream(configBytes);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        const qsizetype comment = line.indexOf(u'#');
        if (comment >= 0) {
            line.truncate(comment);
            line = line.trimmed();
        }
        if (line.isEmpty()) {
            continue;
        }
        const QStringList parts = QProcess::splitCommand(line);
        if (parts.size() < 2) {
            continue;
        }
        const QString key = parts.constFirst().toLower();
        const QString value = parts.sliced(1).join(u' ');
        if (key == QStringLiteral("host")) {
            flushBlock();
            block.aliases = parts.sliced(1);
        } else if (!block.aliases.isEmpty() && key == QStringLiteral("hostname")) {
            block.hostName = value;
        } else if (!block.aliases.isEmpty() && key == QStringLiteral("user")) {
            block.user = value;
        } else if (!block.aliases.isEmpty() && key == QStringLiteral("port")) {
            bool valid = false;
            const int port = value.toInt(&valid);
            if (valid && port > 0 && port <= 65535) {
                block.port = port;
            }
        } else if (!block.aliases.isEmpty() && key == QStringLiteral("identityfile")) {
            block.identityFile = value;
        } else if (!block.aliases.isEmpty() && key == QStringLiteral("proxyjump")) {
            block.proxyJump = value;
        } else if (!block.aliases.isEmpty() && key == QStringLiteral("forwardx11")) {
            block.forwardX11 = value.compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
        }
    }
    flushBlock();

    const int originalSize = static_cast<int>(m_sessions.size());
    const QStringList originalFolders = m_folders;
    int added = 0;
    for (auto& candidate : imported) {
        const bool duplicate = std::any_of(m_sessions.cbegin(),
                                           m_sessions.cend(),
                                           [&candidate](const Session& existing) {
                                               return existing.name == candidate.name
                                                      && existing.host == candidate.host;
                                           });
        if (duplicate) {
            continue;
        }
        const int row = static_cast<int>(m_sessions.size());
        beginInsertRows({}, row, row);
        ensureFolderAncestors(candidate.group);
        m_sessions.push_back(std::move(candidate));
        endInsertRows();
        ++added;
    }
    if (added > 0 && !save()) {
        beginRemoveRows({}, originalSize, static_cast<int>(m_sessions.size()) - 1);
        m_sessions.resize(originalSize);
        endRemoveRows();
        m_folders = originalFolders;
        return 0;
    }
    setLastError({});
    if (added > 0) {
        emit countChanged();
        if (m_folders != originalFolders) {
            emit folderCountChanged();
        }
        advanceRevision();
    }
    setLastImportSummary(added > 0
                             ? QStringLiteral("Imported %1 OpenSSH session(s)").arg(added)
                             : QStringLiteral("No new OpenSSH hosts found"));
    return added;
}

int SessionStore::importMRemoteNg(const QString& source) {
    setLastImportSummary({});
    const QString path = localFilePath(source);
    QFile file(path);
    if (!file.exists()) {
        setLastError(QStringLiteral("mRemoteNG file was not found"));
        return 0;
    }
    constexpr qint64 maximumImportBytes = 128LL * 1024LL * 1024LL;
    if (file.size() > maximumImportBytes) {
        setLastError(QStringLiteral("mRemoteNG file is larger than the 128 MB safety limit"));
        return 0;
    }
    QByteArray xmlBytes;
    QString readError;
    if (!PrivateFile::read(path, xmlBytes, readError, maximumImportBytes)) {
        setLastError(readError); return 0;
    }

    QVector<Session> candidates;
    QStringList importedFolders;
    QStringList folderStack;
    QVector<bool> nodeIsContainer;
    int skippedUnsupported = 0;
    int skippedInvalid = 0;
    QXmlStreamReader xml(xmlBytes);
    xml.setEntityExpansionLimit(1024);
    int depth = 0, elements = 0;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isDTD() || xml.isEntityReference()) {
            setLastError(QStringLiteral("DTD and entity references are not accepted in session imports.")); return 0;
        }
        if (xml.isStartElement() && (++depth > 64 || ++elements > 200000)) {
            setLastError(QStringLiteral("Session XML exceeds the nesting or element limit.")); return 0;
        }
        if (xml.isEndElement()) --depth;
        if (xml.isStartElement() && xml.name() == QStringLiteral("Connections")) {
            const QString fullyEncrypted =
                xml.attributes().value(QStringLiteral("FullFileEncryption")).toString();
            if (fullyEncrypted.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
                setLastError(QStringLiteral(
                    "This mRemoteNG export is fully encrypted; export an XML copy without "
                    "full-file encryption. Password fields will still be ignored."));
                return 0;
            }
        }
        if (xml.isStartElement() && xml.name() == QStringLiteral("Node")) {
            const auto attributes = xml.attributes();
            const QString type = attributes.value(QStringLiteral("Type")).toString();
            const bool container = type.compare(QStringLiteral("Container"),
                                                Qt::CaseInsensitive) == 0;
            nodeIsContainer.push_back(container);
            if (container) {
                const QString name = attributes.value(QStringLiteral("Name")).toString().trimmed();
                if (!name.isEmpty()) {
                    folderStack.push_back(name);
                } else {
                    folderStack.push_back(QStringLiteral("Unnamed folder"));
                }
                const QString folder = normalizeFolderPath(folderStack.join(u'/'));
                if (!folder.isEmpty() && !importedFolders.contains(folder)) {
                    importedFolders.push_back(folder);
                }
                continue;
            }
            if (type.compare(QStringLiteral("Connection"), Qt::CaseInsensitive) != 0) {
                continue;
            }
            const QString protocol = attributes.value(QStringLiteral("Protocol")).toString();
            if (protocol.compare(QStringLiteral("SSH2"), Qt::CaseInsensitive) != 0
                && protocol.compare(QStringLiteral("SSH"), Qt::CaseInsensitive) != 0) {
                ++skippedUnsupported;
                continue;
            }

            Session session;
            session.id = attributes.value(QStringLiteral("Id")).toString().trimmed();
            if (session.id.isEmpty()) {
                session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
            session.name = attributes.value(QStringLiteral("Name")).toString().trimmed();
            session.host = attributes.value(QStringLiteral("Hostname")).toString().trimmed();
            session.user = attributes.value(QStringLiteral("Username")).toString().trimmed();
            session.group = normalizeFolderPath(folderStack.join(u'/'));
            bool portValid = false;
            const int port = attributes.value(QStringLiteral("Port")).toInt(&portValid);
            session.port = portValid && port > 0 && port <= 65535 ? port : 22;
            session.tags = {QStringLiteral("mremoteng")};
            if (session.name.isEmpty()) {
                session.name = session.host;
            }
            if (session.host.isEmpty()) {
                ++skippedInvalid;
                continue;
            }
            candidates.push_back(std::move(session));
        } else if (xml.isEndElement() && xml.name() == QStringLiteral("Node")
                   && !nodeIsContainer.isEmpty()) {
            const bool container = nodeIsContainer.takeLast();
            if (container && !folderStack.isEmpty()) {
                folderStack.removeLast();
            }
        }
    }
    if (xml.hasError()) {
        setLastError(QStringLiteral("Could not parse mRemoteNG XML at line %1: %2")
                         .arg(xml.lineNumber())
                         .arg(xml.errorString()));
        return 0;
    }

    const int originalSize = static_cast<int>(m_sessions.size());
    const QStringList originalFolders = m_folders;
    for (const auto& folder : importedFolders) {
        ensureFolderAncestors(folder);
    }
    int added = 0;
    int skippedDuplicates = 0;
    for (auto& candidate : candidates) {
        const bool duplicate = std::any_of(
            m_sessions.cbegin(), m_sessions.cend(), [&candidate](const Session& existing) {
                return existing.id.compare(candidate.id, Qt::CaseInsensitive) == 0
                       || (existing.name.compare(candidate.name, Qt::CaseInsensitive) == 0
                           && existing.host.compare(candidate.host, Qt::CaseInsensitive) == 0
                           && existing.user.compare(candidate.user, Qt::CaseInsensitive) == 0
                           && existing.group.compare(candidate.group, Qt::CaseInsensitive) == 0);
            });
        if (duplicate) {
            ++skippedDuplicates;
            continue;
        }
        const int row = static_cast<int>(m_sessions.size());
        beginInsertRows({}, row, row);
        ensureFolderAncestors(candidate.group);
        m_sessions.push_back(std::move(candidate));
        endInsertRows();
        ++added;
    }
    const int foldersAdded = static_cast<int>(m_folders.size() - originalFolders.size());
    if ((added > 0 || foldersAdded > 0) && !save()) {
        if (added > 0) {
            beginRemoveRows({}, originalSize, static_cast<int>(m_sessions.size()) - 1);
            m_sessions.resize(originalSize);
            endRemoveRows();
        }
        m_folders = originalFolders;
        return 0;
    }

    setLastError({});
    if (added > 0 || foldersAdded > 0) {
        if (added > 0) {
            emit countChanged();
        }
        if (foldersAdded > 0) {
            emit folderCountChanged();
        }
        advanceRevision();
    }
    setLastImportSummary(
        QStringLiteral("Imported %1 SSH session(s) and %2 new folder(s); skipped %3 duplicate(s), "
                       "%4 unsupported connection(s), and %5 invalid item(s)")
            .arg(added)
            .arg(foldersAdded)
            .arg(skippedDuplicates)
            .arg(skippedUnsupported)
            .arg(skippedInvalid));
    return added;
}

bool SessionStore::reload() {
    QVariantMap document;
    if (!m_yaml.load(document)) { setLastError(m_yaml.error()); return false; }
    const bool migrate = !QFileInfo::exists(m_storagePath);
    if (migrate) {
        QFile legacy(QDir(QFileInfo(m_storagePath).absolutePath()).filePath(QStringLiteral("sessions.json")));
        if (legacy.exists()) {
            QJsonParseError parseError;
            QByteArray bytes;
            QString readError;
            const bool opened = PrivateFile::read(legacy.fileName(), bytes, readError);
            const auto json = QJsonDocument::fromJson(bytes, &parseError);
            const auto version = json.object().value(QStringLiteral("version")).toInt();
            if (!opened || !json.isObject() || parseError.error != QJsonParseError::NoError || version < 1 || version > 3) {
                m_yaml.reject(QStringLiteral("Cannot migrate sessions.json (invalid file or version). Original preserved."));
                setLastError(m_yaml.error()); return false;
            }
            document = json.object().toVariantMap();
            QVariantList converted;
            for (const auto& entry : document.value(QStringLiteral("sessions")).toList()) {
                auto session = entry.toMap();
                if (session.value(QStringLiteral("id")).toString().trimmed().isEmpty())
                    session[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
                session[QStringLiteral("folder")] = session.take(QStringLiteral("group"));
                if (session.value(QStringLiteral("folder")).isNull()) session[QStringLiteral("folder")] = QString{};
                converted << session;
            }
            document[QStringLiteral("sessions")] = converted;
        } else {
            document = {{QStringLiteral("sessions"), QVariantList{}}, {QStringLiteral("folders"), QStringList{}}};
        }
    }
    QString error;
    if (!validateYaml(document, error)) {
        m_yaml.reject(error); setLastError(error); return false;
    }
    QVector<Session> loaded;
    QStringList loadedFolders;
    for (const auto& value : document.value(QStringLiteral("folders")).toList()) {
        const QString folder = normalizeFolderPath(value.toString());
        if (!folder.isEmpty() && !loadedFolders.contains(folder)) {
            loadedFolders.push_back(folder);
        }
    }
    const auto sessions = document.value(QStringLiteral("sessions")).toList();
    loaded.reserve(sessions.size());
    for (const auto& value : sessions) {
        auto fields = value.toMap();
        fields[QStringLiteral("group")] = fields.take(QStringLiteral("folder"));
        Session session = fromMap(fields);
        if (!session.host.isEmpty()) {
            session.group = normalizeFolderPath(session.group);
            loaded.push_back(std::move(session));
        }
    }

    if (migrate && !m_yaml.save(document)) { setLastError(m_yaml.error()); return false; }

    beginResetModel();
    m_sessions = std::move(loaded);
    m_routes = document.value(QStringLiteral("routes")).toMap();
    m_folderConnections = document.value(QStringLiteral("folderConnections")).toMap();
    m_folders.clear();
    for (const auto& folder : loadedFolders) ensureFolderAncestors(folder);
    for (const auto& session : m_sessions) {
        ensureFolderAncestors(session.group);
    }
    endResetModel();
    setLastError({});
    emit countChanged();
    emit folderCountChanged();
    advanceRevision();
    return true;
}

SessionStore::Session SessionStore::fromMap(const QVariantMap& values,
                                            const Session* existing) {
    Session session = existing == nullptr ? Session{} : *existing;
    const auto stringValue = [&values](const QString& key, const QString& fallback) {
        return values.contains(key) ? values.value(key).toString().trimmed() : fallback;
    };
    session.id = stringValue(QStringLiteral("id"), session.id);
    if (session.id.isEmpty()) {
        session.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    session.name = stringValue(QStringLiteral("name"), session.name);
    session.host = stringValue(QStringLiteral("host"), session.host);
    session.user = stringValue(QStringLiteral("user"), session.user);
    session.group = normalizeFolderPath(stringValue(QStringLiteral("group"), session.group));
    session.identityKey = stringValue(QStringLiteral("identityKey"), session.identityKey);
    session.identityFile = stringValue(QStringLiteral("identityFile"), session.identityFile);
    session.proxyJump = stringValue(QStringLiteral("proxyJump"), session.proxyJump);
    if (values.contains(QStringLiteral("connection"))) session.connection = values.value(QStringLiteral("connection")).toMap();
    if (values.contains(QStringLiteral("port"))) {
        session.port = std::clamp(values.value(QStringLiteral("port")).toInt(), 1, 65535);
    }
    if (values.contains(QStringLiteral("tags"))) {
        session.tags = values.value(QStringLiteral("tags")).toStringList();
    }
    if (values.contains(QStringLiteral("forwardX11"))) {
        session.forwardX11 = values.value(QStringLiteral("forwardX11")).toBool();
    }
    if (values.contains(QStringLiteral("agentForwarding"))) {
        session.agentForwarding = values.value(QStringLiteral("agentForwarding")).toBool();
    }
    if (values.contains(QStringLiteral("compression"))) {
        session.compression = values.value(QStringLiteral("compression")).toBool();
    }
    if (values.contains(QStringLiteral("backspaceMode"))) {
        const QString requested = values.value(QStringLiteral("backspaceMode"))
                                      .toString().trimmed().toLower();
        session.backspaceMode = requested == QStringLiteral("bs")
                                    || requested == QStringLiteral("del")
                                    ? requested : QStringLiteral("default");
    }
    return session;
}

QVariantMap SessionStore::toMap(const Session& session) {
    return {
        {QStringLiteral("id"), session.id},
        {QStringLiteral("name"), session.name},
        {QStringLiteral("host"), session.host},
        {QStringLiteral("user"), session.user},
        {QStringLiteral("port"), session.port},
        {QStringLiteral("group"), session.group},
        {QStringLiteral("tags"), session.tags},
        {QStringLiteral("identityKey"), session.identityKey},
        {QStringLiteral("identityFile"), session.identityFile},
        {QStringLiteral("proxyJump"), session.proxyJump},
        {QStringLiteral("forwardX11"), session.forwardX11},
        {QStringLiteral("agentForwarding"), session.agentForwarding},
        {QStringLiteral("compression"), session.compression},
        {QStringLiteral("backspaceMode"), session.backspaceMode},
        {QStringLiteral("connection"), session.connection},
    };
}

QString SessionStore::normalizeFolderPath(const QString& path) {
    QString normalized = path;
    normalized.replace(u'\\', u'/');
    QStringList parts;
    for (const auto& part : normalized.split(u'/', Qt::SkipEmptyParts)) {
        const QString trimmed = part.trimmed();
        if (!trimmed.isEmpty() && trimmed != QStringLiteral(".")) {
            parts.push_back(trimmed);
        }
    }
    return parts.join(u'/');
}

void SessionStore::ensureFolderAncestors(const QString& path) {
    if (!validCatalogFolder(path)) return; // save()/import validation reports the error.
    const QString normalized = normalizeFolderPath(path);
    if (normalized.isEmpty()) {
        return;
    }
    QString current;
    for (const auto& part : normalized.split(u'/')) {
        if (m_folders.size() > 10000) return;
        current = current.isEmpty() ? part : QStringLiteral("%1/%2").arg(current, part);
        if (!m_folders.contains(current)) {
            m_folders.push_back(current);
        }
    }
}

void SessionStore::advanceRevision() {
    ++m_revision;
    emit revisionChanged();
}

bool SessionStore::save() {
    const auto document = yamlDocument();
    QString error;
    if (!validateYaml(document, error)) { setLastError(error); return false; }
    const bool ok = m_yaml.save(document);
    setLastError(m_yaml.error());
    return ok;
}

QVariantMap SessionStore::yamlDocument() const {
    QVariantList sessions;
    for (const auto& session : m_sessions) {
        auto fields = toMap(session);
        fields[QStringLiteral("folder")] = fields.take(QStringLiteral("group"));
        sessions << fields;
    }
    QStringList sortedFolders = m_folders;
    std::sort(sortedFolders.begin(), sortedFolders.end(),
              [](const QString& left, const QString& right) {
                  return naturalLess(left, right);
              });
    return {{QStringLiteral("sessions"), sessions}, {QStringLiteral("folders"), sortedFolders},
        {QStringLiteral("routes"), m_routes}, {QStringLiteral("folderConnections"), m_folderConnections}};
}

bool SessionStore::validateYaml(const QVariantMap& document, QString& error) {
    if (!YamlFile::fields(document, {QStringLiteral("version"), QStringLiteral("kind"),
            QStringLiteral("folders"), QStringLiteral("sessions"), QStringLiteral("routes"), QStringLiteral("folderConnections")}, error)) return false;
    const auto routesValue = document.value(QStringLiteral("routes"), QVariantMap{});
    const auto folderValue = document.value(QStringLiteral("folderConnections"), QVariantMap{});
    if (routesValue.typeId() != QMetaType::QVariantMap || folderValue.typeId() != QMetaType::QVariantMap
        || routesValue.toMap().size() > 256 || folderValue.toMap().size() > 10000) {
        error = QStringLiteral("Routes and folderConnections must be bounded mappings."); return false;
    }
    const auto routes = routesValue.toMap();
    for (auto it = routes.cbegin(); it != routes.cend(); ++it) {
        if (it.key().isEmpty() || !ConnectionRouting::validIdentity(it.key()) || it.value().typeId() != QMetaType::QVariantMap
            || !ConnectionRouting::validateRoute(it.value().toMap(), error)) {
            if (error.isEmpty()) error = QStringLiteral("Invalid route id or definition.");
            return false;
        }
    }
    const auto assignments = folderValue.toMap();
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        if (!validCatalogFolder(it.key()) || normalizeFolderPath(it.key()) != it.key()
            || it.value().typeId() != QMetaType::QVariantMap || !ConnectionRouting::validateSelection(it.value().toMap(), error)) {
            if (error.isEmpty()) error = QStringLiteral("Folder connection must name an existing canonical folder.");
            return false;
        }
    }
    const auto sessions = document.value(QStringLiteral("sessions"));
    if (sessions.typeId() != QMetaType::QVariantList) { error = QStringLiteral("sessions must be a list."); return false; }
    if (sessions.toList().size() > 10000) { error = QStringLiteral("At most 10,000 sessions are supported."); return false; }
    QSet<QString> expandedFolders;
    const auto checkFolder = [&](const QString& path) {
        if (!validCatalogFolder(path)) { error = QStringLiteral("Folder paths allow at most 1024 characters and 64 levels, without controls."); return false; }
        QString current;
        for (const auto& part : normalizeFolderPath(path).split(u'/', Qt::SkipEmptyParts)) {
            current = current.isEmpty() ? part : current + u'/' + part;
            expandedFolders.insert(current);
            if (expandedFolders.size() > 10000) { error = QStringLiteral("At most 10,000 expanded folders are supported."); return false; }
        }
        return true;
    };
    const QStringList strings{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("host"),
        QStringLiteral("user"), QStringLiteral("folder"), QStringLiteral("identityKey"), QStringLiteral("identityFile"),
        QStringLiteral("proxyJump"), QStringLiteral("backspaceMode")};
    const QStringList booleans{QStringLiteral("forwardX11"), QStringLiteral("agentForwarding"), QStringLiteral("compression")};
    QSet<QString> ids;
    for (const auto& value : sessions.toList()) {
        if (value.typeId() != QMetaType::QVariantMap) { error = QStringLiteral("Each session must be a mapping."); return false; }
        const auto row = value.toMap();
        if (!YamlFile::fields(row, strings + booleans + QStringList{QStringLiteral("port"), QStringLiteral("tags"), QStringLiteral("connection")}, error)) return false;
        if (row.contains(QStringLiteral("connection")) && (row[QStringLiteral("connection")].typeId() != QMetaType::QVariantMap
            || !ConnectionRouting::validateSelection(row[QStringLiteral("connection")].toMap(), error))) {
            if (error.isEmpty()) error = QStringLiteral("Connection must be a mapping.");
            return false;
        }
        for (const auto& key : strings) {
            if (row.contains(key) && (row.value(key).typeId() != QMetaType::QString || row.value(key).toString().size() > 4096)) {
                error = QStringLiteral("Session %1 must be text (quote numeric names/hosts).").arg(key); return false;
            }
        }
        if (!checkFolder(row.value(QStringLiteral("folder")).toString())) return false;
        for (const auto& key : booleans) {
            if (row.contains(key) && row.value(key).typeId() != QMetaType::Bool) {
                error = QStringLiteral("Session %1 must be true or false.").arg(key); return false;
            }
        }
        const auto id = row.value(QStringLiteral("id")).toString();
        if (id.trimmed().isEmpty() || id != id.trimmed() || ids.contains(id) || row.value(QStringLiteral("host")).toString().trimmed().isEmpty()) {
            error = QStringLiteral("Every session needs a nonempty host and a unique, nonempty id."); return false;
        }
        ids.insert(id);
        if (row.contains(QStringLiteral("port"))) {
            const auto port = row.value(QStringLiteral("port"));
            if ((port.typeId() != QMetaType::Int && port.typeId() != QMetaType::LongLong && port.typeId() != QMetaType::Double)
                || port.toDouble() != port.toInt() || port.toInt() < 1 || port.toInt() > 65535) {
                error = QStringLiteral("Session port must be an integer from 1 to 65535."); return false;
            }
        }
        const auto mode = row.value(QStringLiteral("backspaceMode"), QStringLiteral("default")).toString();
        if (!QStringList{QStringLiteral("default"), QStringLiteral("bs"), QStringLiteral("del")}.contains(mode)) {
            error = QStringLiteral("backspaceMode must be default, bs or del."); return false;
        }
        const QString identityKey = row.value(QStringLiteral("identityKey")).toString();
        static const QRegularExpression identityKeyPattern(QStringLiteral("\\A(?:none|[0-9a-f]{32})\\z"));
        if (!identityKey.isEmpty() && !identityKeyPattern.match(identityKey).hasMatch()) {
            error = QStringLiteral("identityKey must be empty, 'none', or a 32-character local key id.");
            return false;
        }
        if (row.contains(QStringLiteral("tags"))) {
            const auto tags = row.value(QStringLiteral("tags"));
            if (tags.typeId() != QMetaType::QVariantList && tags.typeId() != QMetaType::QStringList) {
                error = QStringLiteral("Session tags must be a list of strings."); return false;
            }
            for (const auto& tag : tags.toList()) if (tag.typeId() != QMetaType::QString) {
                error = QStringLiteral("Session tags must be strings."); return false;
            }
        }
    }
    if (document.contains(QStringLiteral("folders"))) {
        const auto folders = document.value(QStringLiteral("folders"));
        if (folders.typeId() != QMetaType::QVariantList && folders.typeId() != QMetaType::QStringList) {
            error = QStringLiteral("folders must be a list of paths."); return false;
        }
        if (folders.toList().size() > 10000) { error = QStringLiteral("At most 10,000 folder entries are supported."); return false; }
        for (const auto& folder : folders.toList()) {
            if (folder.typeId() != QMetaType::QString) { error = QStringLiteral("Folder paths must be strings."); return false; }
            if (!checkFolder(folder.toString())) return false;
        }
    }
    for (auto it = assignments.cbegin(); it != assignments.cend(); ++it) {
        if (!it.key().isEmpty() && !expandedFolders.contains(it.key())) {
            error = QStringLiteral("Folder connection must name an existing canonical folder."); return false;
        }
        const auto selection = it.value().toMap();
        if (selection.value(QStringLiteral("mode")) == QStringLiteral("inherit") && selection.contains(QStringLiteral("account"))) {
            error = QStringLiteral("An inherited folder rule cannot override an account. Choose a route explicitly."); return false;
        }
    }
    return true;
}

bool SessionStore::exportYaml(const QString& destination) {
    const bool ok = m_yaml.exportFile(destination, yamlDocument());
    setLastError(m_yaml.error()); return ok;
}

int SessionStore::importYaml(const QString& source) {
    QVariantMap document;
    QString error;
    if (!m_yaml.importFile(source, document)) { setLastError(m_yaml.error()); return -1; }
    if (!validateYaml(document, error)) { setLastError(error); return -1; }
    auto nextSessions = m_sessions;
    auto nextFolders = m_folders;
    QSet<QString> ids;
    for (const auto& session : m_sessions) ids.insert(session.id);
    const auto addFolderPath = [&nextFolders](const QString& path) {
        QString current;
        for (const auto& part : normalizeFolderPath(path).split(u'/', Qt::SkipEmptyParts)) {
            current = current.isEmpty() ? part : current + u'/' + part;
            if (!nextFolders.contains(current)) nextFolders << current;
        }
    };
    int added = 0, skipped = 0;
    for (const auto& value : document.value(QStringLiteral("sessions")).toList()) {
        auto row = value.toMap();
        row[QStringLiteral("group")] = row.take(QStringLiteral("folder"));
        const auto session = fromMap(row);
        if (ids.contains(session.id)) { ++skipped; continue; }
        ids.insert(session.id);
        nextSessions << session;
        addFolderPath(session.group);
        ++added;
    }
    for (const auto& folder : document.value(QStringLiteral("folders")).toList()) addFolderPath(folder.toString());
    QVariantList rows;
    for (const auto& session : nextSessions) {
        auto row = toMap(session);
        row[QStringLiteral("folder")] = row.take(QStringLiteral("group"));
        rows << row;
    }
    // Commit the staged catalog before exposing any changed rows to QML.
    auto nextRoutes = m_routes;
    const auto importedRoutes = document.value(QStringLiteral("routes")).toMap();
    for (auto it = importedRoutes.cbegin(); it != importedRoutes.cend(); ++it) {
        if (nextRoutes.contains(it.key()) && ConnectionRouting::fingerprint(nextRoutes.value(it.key()).toMap()) != ConnectionRouting::fingerprint(it.value().toMap())) {
            setLastError(QStringLiteral("Route id '%1' has a different definition. Import blocked; rename the route and its references first.").arg(it.key())); return -1;
        }
        nextRoutes[it.key()] = it.value();
    }
    auto nextAssignments = m_folderConnections;
    const auto importedAssignments = document.value(QStringLiteral("folderConnections")).toMap();
    for (auto it = importedAssignments.cbegin(); it != importedAssignments.cend(); ++it) {
        if (nextAssignments.contains(it.key()) && nextAssignments.value(it.key()) != it.value()) {
            setLastError(QStringLiteral("Folder '%1' already has a different connection rule. Import blocked.").arg(it.key())); return -1;
        }
        // Do not silently reroute existing inherited sessions through new assignments.
        for (const auto& existing : m_sessions) if (!nextAssignments.contains(it.key())
            && (it.key().isEmpty() || isPathOrDescendant(existing.group, it.key()))
            && existing.connection.value(QStringLiteral("mode")) == QStringLiteral("inherit")) {
            setLastError(QStringLiteral("Imported folder routing would affect existing sessions. Assign it explicitly after importing into a separate folder.")); return -1;
        }
        nextAssignments[it.key()] = it.value();
    }
    const QVariantMap merged{{QStringLiteral("sessions"), rows}, {QStringLiteral("folders"), nextFolders},
        {QStringLiteral("routes"), nextRoutes}, {QStringLiteral("folderConnections"), nextAssignments}};
    if (!validateYaml(merged, error)) { setLastError(error); return -1; }
    if (!m_yaml.save(merged)) {
        setLastError(m_yaml.error()); return -1;
    }
    beginResetModel();
    m_sessions = std::move(nextSessions); m_folders = std::move(nextFolders);
    m_routes = std::move(nextRoutes); m_folderConnections = std::move(nextAssignments);
    endResetModel();
    setLastError({});
    emit countChanged(); emit folderCountChanged(); advanceRevision();
    setLastImportSummary(QStringLiteral("Imported %1 sessions; skipped %2 existing ids. Existing sessions were not overwritten.").arg(added).arg(skipped));
    return added;
}

void SessionStore::setLastError(const QString& message) {
    if (m_lastError == message) {
        return;
    }
    m_lastError = message;
    emit lastErrorChanged();
}

bool SessionStore::saveRoute(const QString& id, const QVariantMap& route) {
    QString error;
    if (id.isEmpty() || !ConnectionRouting::validIdentity(id) || !ConnectionRouting::validateRoute(route, error)) {
        setLastError(error.isEmpty() ? QStringLiteral("Invalid route id.") : error); return false;
    }
    const auto previous = m_routes; m_routes[id] = route;
    if (!save()) { m_routes = previous; return false; }
    advanceRevision(); return true;
}
bool SessionStore::removeRoute(const QString& id) {
    // Keep references intact: a missing required route blocks, never falls back.
    const auto previous = m_routes; m_routes.remove(id);
    if (!save()) { m_routes = previous; return false; }
    advanceRevision(); return true;
}
bool SessionStore::assignFolderConnection(const QString& folder, const QVariantMap& selection) {
    QString error;
    if ((!folder.isEmpty() && !m_folders.contains(folder)) || !ConnectionRouting::validateSelection(selection, error)) {
        setLastError(error.isEmpty() ? QStringLiteral("Folder does not exist.") : error); return false;
    }
    if (selection.value(QStringLiteral("mode")) == QStringLiteral("inherit") && selection.contains(QStringLiteral("account"))) {
        setLastError(QStringLiteral("An inherited folder rule cannot override an account. Choose a route explicitly.")); return false;
    }
    const auto previous = m_folderConnections;
    if (selection.value(QStringLiteral("mode")) == QStringLiteral("inherit")) m_folderConnections.remove(folder);
    else m_folderConnections[folder] = selection;
    if (!save()) { m_folderConnections = previous; return false; }
    advanceRevision(); return true;
}
QVariantMap SessionStore::folderConnection(const QString& folder) const {
    return m_folderConnections.value(folder, QVariantMap{{QStringLiteral("mode"), QStringLiteral("inherit")}}).toMap();
}
QVariantMap SessionStore::prepareProfile(const QVariantMap& profile, const QString& username) const {
    const auto prepared = ConnectionRouting::prepare(profile, m_routes, m_folderConnections, username);
    if (!prepared) return {{QStringLiteral("error"), prepared.error()}};
    QVariantMap result{{QStringLiteral("profile"), *prepared}};
    if (prepared->contains(QStringLiteral("psmp"))) {
        const auto plan = prepared->value(QStringLiteral("psmp")).toMap();
        result[QStringLiteral("fingerprint")] = ConnectionRouting::fingerprint(plan.value(QStringLiteral("route")).toMap());
        result[QStringLiteral("login")] = *ConnectionRouting::login(*prepared);
    }
    return result;
}

void SessionStore::setLastImportSummary(const QString& message) {
    if (m_lastImportSummary == message) {
        return;
    }
    m_lastImportSummary = message;
    emit lastImportSummaryChanged();
}

bool SessionStore::isValidRow(int row) const {
    return row >= 0 && row < static_cast<int>(m_sessions.size());
}

} // namespace bitshell::core
