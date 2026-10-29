#include <bitshell/core/YamlFile.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <yaml-cpp/yaml.h>
#include <yaml-cpp/eventhandler.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>
#include <QStringDecoder>
#include <algorithm>
#include <vector>
#include <cmath>
#include <stdexcept>
#include <sstream>

namespace bitshell::core {
namespace {
constexpr qint64 maxBytes = 8 * 1024 * 1024;
// Stop recursive parsing before constructing a Node graph. A depth check on
// an already-built document is too late for stack/alias amplification attacks.
class BoundedYamlEvents final : public YAML::EventHandler {
    int depth = 0, budget = 200000, documents = 0;
    void value() { if (--budget < 0) throw std::runtime_error("YAML contains too many values."); }
    void enter() { value(); if (++depth > 32) throw std::runtime_error("YAML nesting exceeds 32 levels."); }
public:
    void OnDocumentStart(const YAML::Mark&) override {
        if (++documents > 1) throw std::runtime_error("Expected one YAML document.");
    }
    void OnDocumentEnd() override {}
    void OnNull(const YAML::Mark&, YAML::anchor_t) override { value(); }
    void OnAlias(const YAML::Mark&, YAML::anchor_t) override { throw std::runtime_error("YAML aliases are disabled for security."); }
    void OnAnchor(const YAML::Mark&, const std::string&) override { throw std::runtime_error("YAML anchors are disabled for security."); }
    void OnScalar(const YAML::Mark&, const std::string&, YAML::anchor_t, const std::string& scalar) override {
        value(); if (scalar.size() > 1024 * 1024) throw std::runtime_error("A YAML scalar exceeds 1 MiB.");
    }
    void OnSequenceStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override { enter(); }
    void OnSequenceEnd() override { --depth; }
    void OnMapStart(const YAML::Mark&, const std::string&, YAML::anchor_t, YAML::EmitterStyle::value) override { enter(); }
    void OnMapEnd() override { --depth; }
};
QByteArray digest(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}
QVariant convert(const YAML::Node& node, int depth, int& budget) {
    if (depth > 32 || --budget < 0)
        throw std::runtime_error("YAML is too deeply nested or contains too many values/aliases.");
    const auto tag = node.Tag();
    static const std::vector<std::string> tags{"", "?", "!", "tag:yaml.org,2002:str", "tag:yaml.org,2002:bool",
        "tag:yaml.org,2002:int", "tag:yaml.org,2002:float", "tag:yaml.org,2002:null", "tag:yaml.org,2002:map", "tag:yaml.org,2002:seq"};
    if (std::find(tags.cbegin(), tags.cend(), tag) == tags.cend())
        throw std::runtime_error("Custom YAML tags are not supported.");
    if (node.IsMap()) {
        QVariantMap map;
        for (const auto& pair : node) {
            if (!pair.first.IsScalar()) throw std::runtime_error("YAML keys must be strings.");
            const auto key = QString::fromStdString(pair.first.Scalar());
            if (map.contains(key)) throw std::runtime_error((QStringLiteral("Duplicate YAML key: ") + key).toStdString());
            map[key] = convert(pair.second, depth + 1, budget);
        }
        return map;
    }
    if (node.IsSequence()) {
        QVariantList list;
        for (const auto& value : node) list << convert(value, depth + 1, budget);
        return list;
    }
    if (node.IsNull()) return {};
    if (!node.IsScalar()) throw std::runtime_error("Unsupported YAML value.");
    const QString text = QString::fromStdString(node.Scalar());
    if (tag == "!" || tag == "tag:yaml.org,2002:str") return text;
    if (text == QStringLiteral("true")) return true;
    if (text == QStringLiteral("false")) return false;
    static const QRegularExpression integer(QStringLiteral("^-?(0|[1-9][0-9]*)$"));
    static const QRegularExpression number(QStringLiteral("^-?(0|[1-9][0-9]*)\\.[0-9]+([eE][+-]?[0-9]+)?$"));
    bool ok = false;
    if (integer.match(text).hasMatch()) {
        const auto value = text.toLongLong(&ok);
        if (ok) return value;
    }
    if (number.match(text).hasMatch()) {
        const auto value = text.toDouble(&ok);
        if (ok && std::isfinite(value)) return value;
    }
    return text;
}
QStringList readableKeys(const QVariantMap& map) {
    const QStringList preferred{QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("label"),
        QStringLiteral("host"), QStringLiteral("user"), QStringLiteral("port"), QStringLiteral("folder"),
        QStringLiteral("tags"), QStringLiteral("identityKey"), QStringLiteral("identityFile"), QStringLiteral("proxyJump"),
        QStringLiteral("backspaceMode"), QStringLiteral("forwardX11"), QStringLiteral("agentForwarding"),
        QStringLiteral("compression"), QStringLiteral("text"), QStringLiteral("enter"), QStringLiteral("buttons")};
    QStringList keys;
    for (const auto& key : preferred) if (map.contains(key)) keys << key;
    for (auto it = map.cbegin(); it != map.cend(); ++it) if (!keys.contains(it.key())) keys << it.key();
    return keys;
}
bool mustQuoteScalar(const QString& text) {
    // Quoted text stays a string. Plain true/false/null/numbers would come back
    // as a different YAML type and fail the catalog round trip.
    if (text.isEmpty() || text == u"~" || text == u"true" || text == u"false"
        || text.compare(u"null", Qt::CaseInsensitive) == 0
        || text.compare(u".nan", Qt::CaseInsensitive) == 0
        || text.compare(u".inf", Qt::CaseInsensitive) == 0
        || text.compare(u"-.inf", Qt::CaseInsensitive) == 0
        || text.compare(u"+.inf", Qt::CaseInsensitive) == 0) return true;
    static const QRegularExpression integer(QStringLiteral("^-?(0|[1-9][0-9]*)$"));
    static const QRegularExpression number(QStringLiteral("^-?(0|[1-9][0-9]*)\\.[0-9]+([eE][+-]?[0-9]+)?$"));
    return integer.match(text).hasMatch() || number.match(text).hasMatch();
}
void emitValue(YAML::Emitter& out, const QVariant& value, QList<qsizetype>& stripMarkers) {
    switch (value.typeId()) {
    case QMetaType::QVariantMap: {
        out << YAML::BeginMap;
        const auto map = value.toMap();
        for (const auto& key : readableKeys(map)) {
            out << YAML::Key << key.toStdString() << YAML::Value;
            emitValue(out, map.value(key), stripMarkers);
        }
        out << YAML::EndMap;
        break;
    }
    case QMetaType::QVariantList:
    case QMetaType::QStringList: {
        out << YAML::BeginSeq;
        for (const auto& entry : value.toList()) emitValue(out, entry, stripMarkers);
        out << YAML::EndSeq;
        break;
    }
    case QMetaType::Bool: out << value.toBool(); break;
    case QMetaType::Int:
    case QMetaType::LongLong: out << value.toLongLong(); break;
    case QMetaType::Double: out << value.toDouble(); break;
    default: {
        const auto text = value.toString();
        // yaml-cpp emits '|'. Record its exact location so a no-final-newline
        // macro can use '|-' without modifying any content inside the block.
        if (text.contains(u'\n') && !text.front().isSpace()
            && !text.endsWith(QStringLiteral("\n\n"))
            && std::none_of(text.cbegin(), text.cend(), [](QChar c) { return c.unicode() < 32 && c != u'\n' && c != u'\t'; })) {
            const auto start = static_cast<qsizetype>(out.size());
            out << YAML::Literal << text.toStdString();
            if (!text.endsWith(u'\n')) {
                const auto index = QByteArray(out.c_str(), static_cast<qsizetype>(out.size())).indexOf("|\n", start);
                if (index >= 0) stripMarkers << index + 1;
            }
        } else if (mustQuoteScalar(text)) out << YAML::DoubleQuoted << text.toStdString();
        else out << text.toStdString();
    }
}
}
}
YamlFile::YamlFile(QString path, QString kind, int version) : m_path(std::move(path)), m_kind(std::move(kind)), m_version(version) {}
QString YamlFile::localPath(const QString& source) {
    const QUrl url(source);
    if (url.isLocalFile()) return url.toLocalFile();
    return url.scheme().isEmpty() || (source.size() > 2 && source[1] == u':') ? source : QString{};
}
bool YamlFile::fields(const QVariantMap& values, const QStringList& allowed, QString& error) {
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!allowed.contains(it.key())) {
            error = QStringLiteral("Unknown field '%1'. Nothing was changed.").arg(it.key());
            return false;
        }
    }
    return true;
}
QByteArray YamlFile::encode(const QVariantMap& document, const QString& kind, int version) {
    YAML::Emitter out;
    QList<qsizetype> stripMarkers;
    out.SetIndent(2);
    out << YAML::Comment("8bitShell - UTF-8 YAML. Keep passwords and private key contents out of this file.");
    out << YAML::BeginMap << YAML::Key << "version" << YAML::Value << version;
    out << YAML::Key << "kind" << YAML::Value << kind.toStdString();
    for (auto it = document.cbegin(); it != document.cend(); ++it) {
        if (it.key() == QStringLiteral("version") || it.key() == QStringLiteral("kind")) continue;
        out << YAML::Key << it.key().toStdString() << YAML::Value;
        emitValue(out, it.value(), stripMarkers);
    }
    out << YAML::EndMap;
    QByteArray bytes(out.c_str(), static_cast<qsizetype>(out.size()));
    for (auto it = stripMarkers.crbegin(); it != stripMarkers.crend(); ++it) bytes.insert(*it, '-');
    return bytes + '\n';
}
bool YamlFile::decode(const QByteArray& bytes, const QString& kind, QVariantMap& document, QString& error, int maximumVersion) {
    try {
        if (bytes.size() > maxBytes) throw std::runtime_error("YAML file exceeds the 8 MiB limit.");
        QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
        const QString decodedText = utf8.decode(bytes);
        Q_UNUSED(decodedText);
        if (utf8.hasError()) throw std::runtime_error("Configuration files must use UTF-8.");
        const auto source = bytes.toStdString();
        std::istringstream stream(source);
        YAML::Parser parser(stream);
        BoundedYamlEvents events;
        while (parser.HandleNextDocument(events)) {}
        const auto docs = YAML::LoadAll(source);
        if (docs.size() != 1 || !docs[0].IsMap()) throw std::runtime_error("Expected one YAML mapping document.");
        int budget = 200000;
        const auto root = convert(docs[0], 0, budget).toMap();
        if (root.value(QStringLiteral("version")).typeId() != QMetaType::LongLong
            || root.value(QStringLiteral("version")).toLongLong() < 1
            || root.value(QStringLiteral("version")).toLongLong() > maximumVersion)
            throw std::runtime_error("Unsupported YAML version.");
        auto actualKind = root.value(QStringLiteral("kind")).toString();
        if (actualKind != kind)
            throw std::runtime_error((QStringLiteral("Wrong document kind (expected %1).").arg(kind)).toStdString());
        document = root;
        error.clear();
        return true;
    } catch (const std::exception& ex) {
        error = QString::fromUtf8(ex.what());
        return false;
    }
}
bool YamlFile::read(const QString& path, QByteArray& bytes, QString& error) {
    return PrivateFile::read(path, bytes, error, maxBytes);
}
bool YamlFile::write(const QString& path, const QByteArray& bytes, QString& error) {
    return PrivateFile::write(path, bytes, error);
}
void YamlFile::reject(const QString& error) { m_readOnly = true; m_loadError = error; m_error = error; }
bool YamlFile::load(QVariantMap& document) {
    m_readOnly = true;
    const bool exists = QFileInfo::exists(m_path);
    QByteArray bytes;
    if (exists && (!read(m_path, bytes, m_error) || !decode(bytes, m_kind, document, m_error, m_version))) {
        m_loadError = m_error; return false;
    }
    if (!exists) document = {};
    m_existed = exists;
    m_digest = digest(bytes);
    m_readOnly = false;
    m_error.clear();
    m_loadError.clear();
    return true;
}
bool YamlFile::save(const QVariantMap& document) {
    if (m_readOnly) { m_error = m_loadError; return false; }
    if (!PrivateFile::prepareParent(m_path, m_error)) return false;
    QLockFile lock(m_path + QStringLiteral(".lock"));
    if (!lock.tryLock(0)) { m_error = QStringLiteral("Configuration is being saved by another instance. Try again."); return false; }
    QByteArray current;
    const bool exists = QFileInfo::exists(m_path);
    if (exists && !read(m_path, current, m_error)) return false;
    if (exists != m_existed || digest(current) != m_digest) {
        m_error = QStringLiteral("File changed outside this window. Reload YAML before saving; your file was preserved.");
        return false;
    }
    const auto bytes = encode(document, m_kind, m_version);
    if (!write(m_path, bytes, m_error)) return false;
    m_existed = true;
    m_digest = digest(bytes);
    return true;
}
bool YamlFile::importFile(const QString& source, QVariantMap& document) {
    QByteArray bytes;
    return read(localPath(source), bytes, m_error) && decode(bytes, m_kind, document, m_error, m_version);
}
bool YamlFile::exportFile(const QString& destination, const QVariantMap& document) {
    const QString target = QFileInfo(localPath(destination)).absoluteFilePath();
    const QString activeConfig = Platform::appConfigDirectory();
    const QString newConfig = QDir(Platform::configRoot())
                                  .filePath(QStringLiteral("8bitshell"));
    const auto info = QFileInfo(target);
#ifdef Q_OS_WIN
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    const auto sameDirectory = [pathCase, &info](const QString& directory) {
        const QString targetParent = QDir(info.absolutePath()).canonicalPath();
        const QString managedParent = QDir(directory).canonicalPath();
        if (!targetParent.isEmpty() && !managedParent.isEmpty())
            return targetParent.compare(managedParent, pathCase) == 0;
        return QDir::cleanPath(info.absolutePath()).compare(
                   QDir::cleanPath(QFileInfo(directory).absoluteFilePath()), pathCase) == 0;
    };
    const bool managedName = QStringList{QStringLiteral("sessions.yaml"), QStringLiteral("settings.yaml"),
        QStringLiteral("macros.yaml"), QStringLiteral("personal.yaml"), QStringLiteral("keys.yaml"), QStringLiteral("local-profiles.yaml"),
        QStringLiteral("avatar.png")}.contains(info.fileName(), pathCase);
    const bool managed = managedName && (sameDirectory(activeConfig) || sameDirectory(newConfig));
    if (localPath(destination).isEmpty() || target.compare(QFileInfo(m_path).absoluteFilePath(), pathCase) == 0 || managed) {
        m_error = QStringLiteral("Export to a different file, outside the active configuration files."); return false;
    }
    return write(target, encode(document, m_kind, m_version), m_error);
}
}
