#include <bitshell/core/MacroStore.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QSet>

namespace bitshell::core {
MacroStore::MacroStore(QObject* parent) : QObject(parent)
    , m_yaml(QDir(Platform::appConfigDirectory())
                 .filePath(QStringLiteral("macros.yaml")), QStringLiteral("8bitshell/macros")) {
    m_path = m_yaml.fileName();
    (void)reload();
}
bool MacroStore::reload() {
    QVariantMap document;
    if (!m_yaml.load(document)) { m_error = m_yaml.error(); emit changed(); return false; }
    const bool migrate = !QFileInfo::exists(m_path);
    if (migrate) {
        QFile file(QDir(QFileInfo(m_path).absolutePath()).filePath(QStringLiteral("macros.json")));
        if (file.exists()) {
            QJsonParseError error;
            QByteArray bytes;
            const bool opened = PrivateFile::read(file.fileName(), bytes, m_error);
            const auto json = QJsonDocument::fromJson(bytes, &error);
            if (!opened || error.error != QJsonParseError::NoError || !json.isObject()
                || json.object().value(QStringLiteral("version")).toInt() != 1) {
                m_error = QStringLiteral("Cannot migrate macros.json (invalid file or version). Original preserved.");
                m_yaml.reject(m_error); emit changed(); return false;
            }
            document = json.object().toVariantMap();
        } else document = {{QStringLiteral("groups"), QVariantList{}}, {QStringLiteral("folders"), QVariantMap{}}};
    }
    if (!validateYaml(document, m_error)) { m_yaml.reject(m_error); emit changed(); return false; }
    if (migrate && !m_yaml.save(document)) { m_error = m_yaml.error(); emit changed(); return false; }
    m_groups = document.value(QStringLiteral("groups")).toList();
    m_assignments = document.value(QStringLiteral("folders")).toMap();
    m_error.clear(); ++m_revision; emit changed(); return true;
}
QString MacroStore::normalizedPath(const QString& path) {
    QString copy = path;
    copy.replace(u'\\', u'/');
    QStringList parts;
    for (const auto& part : copy.split(u'/', Qt::SkipEmptyParts)) {
        if (!part.trimmed().isEmpty()) parts << part.trimmed();
    }
    return parts.join(u'/');
}
QVariantMap MacroStore::group(const QString& id) const {
    for (const auto& entry : m_groups) {
        const auto value = entry.toMap();
        if (value.value(QStringLiteral("id")).toString() == id) return value;
    }
    return {};
}
bool MacroStore::persist(const QVariantList& groups, const QVariantMap& assignments) {
    const QVariantMap document{{QStringLiteral("groups"), groups}, {QStringLiteral("folders"), assignments}};
    if (!validateYaml(document, m_error)) { emit changed(); return false; }
    if (!m_yaml.save(document)) {
        m_error = m_yaml.error();
        emit changed();
        return false;
    }
    m_groups = groups;
    m_assignments = assignments;
    m_error.clear();
    ++m_revision;
    emit changed();
    return true;
}

bool MacroStore::validateYaml(const QVariantMap& document, QString& error) {
    const auto fail = [&](const QString& text) { error = text; return false; };
    if (!YamlFile::fields(document, {QStringLiteral("version"), QStringLiteral("kind"), QStringLiteral("groups"), QStringLiteral("folders")}, error)) return false;
    if (document.value(QStringLiteral("groups")).typeId() != QMetaType::QVariantList) return fail(QStringLiteral("groups must be a list."));
    if (document.value(QStringLiteral("groups")).toList().size() > 256) return fail(QStringLiteral("At most 256 macro groups are supported."));
    QSet<QString> ids;
    for (const auto& entry : document.value(QStringLiteral("groups")).toList()) {
        if (entry.typeId() != QMetaType::QVariantMap) return fail(QStringLiteral("Each macro group must be a mapping."));
        const auto group = entry.toMap();
        if (!YamlFile::fields(group, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("buttons")}, error)) return false;
        for (const auto& key : {QStringLiteral("id"), QStringLiteral("name")})
            if (group.value(key).typeId() != QMetaType::QString || group.value(key).toString().trimmed().isEmpty()
                || group.value(key).toString().size() > 256)
                return fail(QStringLiteral("Macro groups require nonempty text id and name."));
        const auto id = group.value(QStringLiteral("id")).toString();
        if (ids.contains(id)) return fail(QStringLiteral("Duplicate macro group id: %1").arg(id));
        ids.insert(id);
        if (group.value(QStringLiteral("buttons")).typeId() != QMetaType::QVariantList) return fail(QStringLiteral("buttons must be a list."));
        if (group.value(QStringLiteral("buttons")).toList().size() > 256) return fail(QStringLiteral("At most 256 buttons per group are supported."));
        for (const auto& b : group.value(QStringLiteral("buttons")).toList()) {
            if (b.typeId() != QMetaType::QVariantMap) return fail(QStringLiteral("Each button must be a mapping."));
            const auto button = b.toMap();
            if (button.value(QStringLiteral("label")).toString().size() > 256
                || button.value(QStringLiteral("text")).toString().size() > 256 * 1024)
                return fail(QStringLiteral("Button labels are limited to 256 characters; text to 256 Ki characters."));
            if (!YamlFile::fields(button, {QStringLiteral("label"), QStringLiteral("text"), QStringLiteral("enter")}, error)) return false;
            for (const auto& key : {QStringLiteral("label"), QStringLiteral("text")})
                if (button.value(key).typeId() != QMetaType::QString || button.value(key).toString().isEmpty())
                    return fail(QStringLiteral("Buttons require a text label and text payload."));
            if (button.contains(QStringLiteral("enter")) && button.value(QStringLiteral("enter")).typeId() != QMetaType::Bool)
                return fail(QStringLiteral("Button enter must be true or false."));
        }
    }
    if (document.contains(QStringLiteral("folders")) && document.value(QStringLiteral("folders")).typeId() != QMetaType::QVariantMap)
        return fail(QStringLiteral("folders must map folder paths to group ids."));
    const auto folders = document.value(QStringLiteral("folders")).toMap();
    if (folders.size() > 10000) return fail(QStringLiteral("At most 10,000 folder assignments are supported."));
    for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
        if (it.key().size() > 1024 || it.key().count(u'/') + it.key().count(u'\\') >= 64)
            return fail(QStringLiteral("Folder assignments allow at most 1024 characters and 64 levels."));
        if (it.value().typeId() != QMetaType::QString || !ids.contains(it.value().toString()))
            return fail(QStringLiteral("Folder '%1' refers to an unknown group.").arg(it.key()));
        if (it.key() != normalizedPath(it.key())) return fail(QStringLiteral("Use normalized folder paths with / separators: %1").arg(it.key()));
    }
    error.clear(); return true;
}

bool MacroStore::exportYaml(const QString& destination, const QString& groupId) {
    QVariantList groups = m_groups;
    QVariantMap folders = m_assignments;
    if (!groupId.isEmpty()) {
        const auto selected = group(groupId);
        if (selected.isEmpty()) { m_error = QStringLiteral("Choose a saved group first."); emit changed(); return false; }
        groups = {selected}; folders.clear();
    }
    const bool ok = m_yaml.exportFile(destination, {{QStringLiteral("groups"), groups}, {QStringLiteral("folders"), folders}});
    m_error = m_yaml.error(); emit changed(); return ok;
}

int MacroStore::importYaml(const QString& source, bool includeAssignments) {
    QVariantMap document;
    if (!m_yaml.importFile(source, document)) { m_error = m_yaml.error(); emit changed(); return -1; }
    if (!validateYaml(document, m_error)) { emit changed(); return -1; }
    auto next = m_groups;
    auto assignments = m_assignments;
    QVariantMap remapped;
    int added = 0, copied = 0, skipped = 0;
    for (const auto& entry : document.value(QStringLiteral("groups")).toList()) {
        auto incoming = entry.toMap();
        const auto id = incoming.value(QStringLiteral("id")).toString();
        const auto existing = group(id);
        if (incoming == existing) { ++skipped; remapped[id] = id; continue; }
        if (!existing.isEmpty()) {
            incoming[QStringLiteral("id")] = QUuid::createUuid().toString(QUuid::WithoutBraces);
            incoming[QStringLiteral("name")] = incoming.value(QStringLiteral("name")).toString() + QStringLiteral(" (imported)");
            ++copied;
        }
        remapped[id] = incoming.value(QStringLiteral("id"));
        next << incoming; ++added;
    }
    int kept = 0;
    if (includeAssignments) {
        const auto folders = document.value(QStringLiteral("folders")).toMap();
        for (auto it = folders.cbegin(); it != folders.cend(); ++it) {
            if (assignments.contains(it.key())) { ++kept; continue; }
            assignments[it.key()] = remapped.value(it.value().toString());
        }
    }
    if (!persist(next, assignments)) return -1;
    m_summary = QStringLiteral("Imported %1 groups (%2 conflict copies); skipped %3 identical groups. %4 existing folder assignments kept.")
                    .arg(added).arg(copied).arg(skipped).arg(kept);
    emit changed(); return added;
}
QString MacroStore::saveGroup(const QString& id, const QString& name, const QVariantList& buttons) {
    if (name.trimmed().isEmpty()) {
        m_error = QStringLiteral("A group name is required."); emit changed(); return {};
    }
    QVariantList clean;
    for (const auto& entry : buttons) {
        const auto b = entry.toMap();
        const auto label = b.value(QStringLiteral("label")).toString().trimmed();
        const auto text = b.value(QStringLiteral("text")).toString();
        if (label.isEmpty() || text.isEmpty()) {
            m_error = QStringLiteral("Every button needs a label and text."); emit changed(); return {};
        }
        clean << QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("text"), text},
                             {QStringLiteral("enter"), b.value(QStringLiteral("enter")).toBool()}};
    }
    const QString actual = id.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : id;
    QVariantList next = m_groups;
    const QVariantMap entry{{QStringLiteral("id"), actual}, {QStringLiteral("name"), name.trimmed()},
                            {QStringLiteral("buttons"), clean}};
    bool found = false;
    for (auto& value : next) {
        if (value.toMap().value(QStringLiteral("id")).toString() == actual) {
            value = entry; found = true; break;
        }
    }
    if (!found) next << entry;
    return persist(next, m_assignments) ? actual : QString{};
}
bool MacroStore::removeGroup(const QString& id) {
    QVariantList next;
    for (const auto& value : m_groups)
        if (value.toMap().value(QStringLiteral("id")).toString() != id) next << value;
    QVariantMap assignments = m_assignments;
    for (auto it = assignments.begin(); it != assignments.end();) {
        if (it.value().toString() == id) it = assignments.erase(it); else ++it;
    }
    return persist(next, assignments);
}
bool MacroStore::assignFolder(const QString& path, const QString& groupId) {
    if (!groupId.isEmpty() && group(groupId).isEmpty()) return false;
    QVariantMap next = m_assignments;
    const QString key = normalizedPath(path);
    if (groupId.isEmpty()) next.remove(key); else next[key] = groupId;
    return persist(m_groups, next);
}
QString MacroStore::assignedGroup(const QString& path) const {
    return m_assignments.value(normalizedPath(path)).toString();
}
QVariantMap MacroStore::effectiveGroup(const QString& path) const {
    QString current = normalizedPath(path);
    while (true) {
        QVariantMap result = group(assignedGroup(current));
        if (!result.value(QStringLiteral("buttons")).toList().isEmpty()) {
            result.insert(QStringLiteral("inheritedFrom"), current);
            return result;
        }
        if (current.isEmpty()) return {};
        const auto slash = current.lastIndexOf(u'/');
        current = slash < 0 ? QString{} : current.first(slash);
    }
}
bool MacroStore::remapFolder(const QString& oldPath, const QString& newPath) {
    const QString from = normalizedPath(oldPath), to = normalizedPath(newPath);
    if (from.isEmpty()) return false;
    QVariantMap next;
    for (auto it = m_assignments.cbegin(); it != m_assignments.cend(); ++it) {
        const QString key = it.key();
        next[key == from || key.startsWith(from + u'/') ? to + key.sliced(from.size()) : key] = it.value();
    }
    return persist(m_groups, next);
}
bool MacroStore::removeFolder(const QString& path) {
    return removeFolders({path});
}
bool MacroStore::removeFolders(const QStringList& paths) {
    QSet<QString> prefixes;
    for (const auto& path : paths) {
        const auto prefix = normalizedPath(path);
        if (!prefix.isEmpty()) prefixes.insert(prefix);
    }
    QVariantMap next = m_assignments;
    for (auto it = next.begin(); it != next.end();) {
        auto folder = it.key();
        bool remove = false;
        while (!folder.isEmpty()) {
            if (prefixes.contains(folder)) { remove = true; break; }
            const auto separator = folder.lastIndexOf(u'/');
            folder = separator < 0 ? QString{} : folder.left(separator);
        }
        if (remove) it = next.erase(it); else ++it;
    }
    if (next == m_assignments) return true;
    return persist(m_groups, next);
}
}
