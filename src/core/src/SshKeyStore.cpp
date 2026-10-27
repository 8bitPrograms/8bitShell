#include <bitshell/core/SshKeyStore.h>

#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <memory>

namespace bitshell::core {
namespace {

constexpr qint64 maximumKeyBytes = 1024 * 1024;
constexpr int maximumKeys = 64;

const QRegularExpression& keyIdPattern() {
    static const QRegularExpression value(QStringLiteral("\\A[0-9a-f]{32}\\z"));
    return value;
}

const QRegularExpression& keyFilePattern() {
    static const QRegularExpression value(
        QStringLiteral("\\Akey-[0-9a-f]{32}-[0-9a-f]{32}\\.key\\z"));
    return value;
}

const QRegularExpression& fingerprintPattern() {
    static const QRegularExpression value(
        QStringLiteral("\\ASHA256:[A-Za-z0-9+/]+={0,2}\\z"));
    return value;
}

bool validKeyId(const QString& id) {
    return keyIdPattern().match(id).hasMatch();
}

QString generatedId() {
    return QUuid::createUuid().toString(QUuid::Id128).toLower();
}

QString generatedFileName(const QString& id) {
    return QStringLiteral("key-%1-%2.key").arg(id, generatedId());
}

QString cleanedName(const QString& requested, const QString& source) {
    QString name = requested.trimmed();
    if (name.isEmpty()) name = QFileInfo(YamlFile::localPath(source)).completeBaseName().trimmed();
    if (name.isEmpty()) name = QStringLiteral("SSH key");
    name.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f]+")), QStringLiteral(" "));
    name = name.simplified().left(128);
    return name.isEmpty() ? QStringLiteral("SSH key") : name;
}

bool processFinished(QProcess& process, int timeoutMs) {
    if (!process.waitForStarted(3000)) return false;
    process.closeWriteChannel();
    if (process.waitForFinished(timeoutMs)) return true;
    process.kill();
    (void)process.waitForFinished(2000);
    return false;
}

bool ppkEncrypted(const QByteArray& bytes) {
    for (const auto& line : bytes.split('\n')) {
        if (line.trimmed().startsWith(QByteArrayLiteral("Encryption:")))
            return line.trimmed() != QByteArrayLiteral("Encryption: none");
    }
    return true; // Malformed/ambiguous PPK must not fall into an interactive prompt.
}

bool looksLikeOpenSshPrivateKey(const QByteArray& bytes) {
    const QByteArray head = bytes.left(4096);
    return head.contains(QByteArrayLiteral("-----BEGIN OPENSSH PRIVATE KEY-----"))
        || head.contains(QByteArrayLiteral("-----BEGIN RSA PRIVATE KEY-----"))
        || head.contains(QByteArrayLiteral("-----BEGIN EC PRIVATE KEY-----"))
        || head.contains(QByteArrayLiteral("-----BEGIN PRIVATE KEY-----"))
        || head.contains(QByteArrayLiteral("-----BEGIN ENCRYPTED PRIVATE KEY-----"));
}

bool looksLikePpk(const QByteArray& bytes) {
    const QByteArray first = bytes.left(bytes.indexOf('\n') < 0 ? bytes.size() : bytes.indexOf('\n')).trimmed();
    return first.startsWith(QByteArrayLiteral("PuTTY-User-Key-File-2:"))
        || first.startsWith(QByteArrayLiteral("PuTTY-User-Key-File-3:"));
}

QString runtimeTemporaryRoot() {
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return runtime.isEmpty() ? QDir::tempPath() : runtime;
}

bool isNumericType(int type) {
    return type == QMetaType::Int || type == QMetaType::LongLong
        || type == QMetaType::UInt || type == QMetaType::ULongLong;
}

} // namespace

SshKeyStore::SshKeyStore(QObject* parent)
    : QObject(parent)
    , m_yaml(QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("keys.yaml")),
             QStringLiteral("8bitshell/keys"), 1) {
    (void)reload();
}

bool SshKeyStore::ppkAvailable() const {
    return QFileInfo(Platform::puttygenExecutable()).isExecutable();
}

std::expected<SshKeyStore::ImportedKey, QString>
SshKeyStore::readAndConvert(const QString& source, const QString& ppkPassphrase) {
    const QString sourcePath = YamlFile::localPath(source);
    if (sourcePath.isEmpty() || !QDir::isAbsolutePath(sourcePath))
        return std::unexpected(QStringLiteral("Choose a local private-key file."));

    QByteArray bytes;
    QString error;
    if (!PrivateFile::read(sourcePath, bytes, error, maximumKeyBytes))
        return std::unexpected(QStringLiteral("Cannot safely read the selected key: %1").arg(error));
    if (bytes.contains('\0')) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("The selected file is not a supported text private key."));
    }

    if (looksLikeOpenSshPrivateKey(bytes))
        return ImportedKey{std::move(bytes), QStringLiteral("openssh")};
    if (!looksLikePpk(bytes)) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("Select an OpenSSH/PEM private key or a PuTTY .ppk file. Public keys are not imported."));
    }

    const bool encrypted = ppkEncrypted(bytes);
    if (encrypted && ppkPassphrase.isEmpty()) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("This PPK is encrypted. Enter its passphrase for one-time conversion; it will not be saved."));
    }
    if (ppkPassphrase.size() > 1024 || ppkPassphrase.contains(u'\n')
        || ppkPassphrase.contains(u'\r') || ppkPassphrase.contains(QChar::Null)) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("The PPK passphrase contains unsupported characters or is too long."));
    }

    const QString converter = Platform::puttygenExecutable();
    if (!QFileInfo(converter).isExecutable()) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("PPK import needs the trusted PuTTYgen converter. Install the 'putty' package, then retry."));
    }

    const QString preferredTemporaryRoot = runtimeTemporaryRoot();
    auto temporary = std::make_unique<QTemporaryDir>(
        QDir(preferredTemporaryRoot).filePath(QStringLiteral("bitshell-ppk-XXXXXX")));
    if (!temporary->isValid() && QDir::cleanPath(preferredTemporaryRoot) != QDir::cleanPath(QDir::tempPath())) {
        temporary = std::make_unique<QTemporaryDir>(
            QDir(QDir::tempPath()).filePath(QStringLiteral("bitshell-ppk-XXXXXX")));
    }
    if (!temporary->isValid() || !PrivateFile::secureEntry(temporary->path(), error)) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("Cannot create private temporary storage for PPK conversion."));
    }
    const QString inputPath = temporary->filePath(QStringLiteral("input.ppk"));
    const QString outputPath = temporary->filePath(QStringLiteral("output.key"));
    if (!PrivateFile::write(inputPath, bytes, error)) {
        bytes.fill('\0');
        return std::unexpected(QStringLiteral("Cannot stage the PPK in private temporary storage."));
    }
    bytes.fill('\0');

    QStringList arguments{inputPath};
    QByteArray passphraseBytes;
    QString passphrasePath;
    if (encrypted) {
        passphrasePath = temporary->filePath(QStringLiteral("passphrase"));
        passphraseBytes = ppkPassphrase.toUtf8();
        if (!PrivateFile::write(passphrasePath, passphraseBytes, error)) {
            passphraseBytes.fill('\0');
            return std::unexpected(QStringLiteral("Cannot stage the PPK passphrase in private runtime storage."));
        }
        arguments << QStringLiteral("--old-passphrase") << passphrasePath
                  << QStringLiteral("--new-passphrase") << passphrasePath;
    }
    arguments << QStringLiteral("-O") << QStringLiteral("private-openssh-new")
              << QStringLiteral("-o") << outputPath;

    QProcess process;
    process.setProcessEnvironment(SshSecurity::environment());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(converter, arguments, QIODevice::ReadOnly);
    const bool finished = processFinished(process, 15000);
    passphraseBytes.fill('\0');
    if (!passphrasePath.isEmpty()) (void)QFile::remove(passphrasePath);
    if (!finished || process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return std::unexpected(QStringLiteral("PPK conversion failed. Check the file and passphrase; the original file was not changed."));
    }

    QByteArray converted;
    if (!PrivateFile::read(outputPath, converted, error, maximumKeyBytes)
        || !looksLikeOpenSshPrivateKey(converted)) {
        converted.fill('\0');
        return std::unexpected(QStringLiteral("PuTTYgen did not produce a valid OpenSSH private key."));
    }
    return ImportedKey{std::move(converted), QStringLiteral("ppk")};
}

std::expected<QVariantMap, QString>
SshKeyStore::inspectManagedKey(const QString& path) {
    QString error;
    if (!PrivateFile::secureEntry(path, error))
        return std::unexpected(QStringLiteral("Imported key is not a private, single-link file: %1").arg(error));

    const QString keygen = Platform::sshKeygenExecutable();
    if (!QFileInfo(keygen).isExecutable())
        return std::unexpected(QStringLiteral("The system OpenSSH key inspector is unavailable at %1.").arg(keygen));
    QProcess process;
    process.setProcessEnvironment(SshSecurity::environment());
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(keygen, {QStringLiteral("-lf"), path, QStringLiteral("-E"), QStringLiteral("sha256")},
                  QIODevice::ReadOnly);
    if (!processFinished(process, 10000) || process.exitStatus() != QProcess::NormalExit
        || process.exitCode() != 0) {
        return std::unexpected(QStringLiteral("OpenSSH rejected this private key. The file may be malformed or unsupported."));
    }

    const QString line = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    const QStringList fields = line.split(u' ', Qt::SkipEmptyParts);
    static const QRegularExpression typePattern(QStringLiteral("\\(([^()]+)\\)\\s*\\z"));
    const auto typeMatch = typePattern.match(line);
    bool bitsOk = false;
    const int bits = fields.isEmpty() ? 0 : fields.first().toInt(&bitsOk);
    const QString fingerprint = fields.size() > 1 ? fields.at(1) : QString{};
    const QString type = typeMatch.hasMatch() ? typeMatch.captured(1).toUpper() : QString{};
    if (!bitsOk || bits <= 0 || !fingerprintPattern().match(fingerprint).hasMatch()
        || !QStringList{QStringLiteral("ED25519"), QStringLiteral("ECDSA"), QStringLiteral("RSA")}.contains(type)) {
        return std::unexpected(QStringLiteral("Only OpenSSH ED25519, ECDSA, and RSA private keys are accepted."));
    }
    if (type == QStringLiteral("RSA") && bits < 3072)
        return std::unexpected(QStringLiteral("RSA keys must be at least 3072 bits."));
    return QVariantMap{{QStringLiteral("fingerprint"), fingerprint},
                       {QStringLiteral("type"), type},
                       {QStringLiteral("bits"), bits}};
}

QString SshKeyStore::managedPath(const QString& fileName) {
    if (!keyFilePattern().match(fileName).hasMatch()) return {};
    return QDir(QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("keys")))
        .filePath(fileName);
}

bool SshKeyStore::validateDocument(const QVariantMap& document, QString& error) {
    if (!YamlFile::fields(document,
            {QStringLiteral("version"), QStringLiteral("kind"), QStringLiteral("defaultKey"),
             QStringLiteral("cyberArkKey"), QStringLiteral("keys")}, error))
        return false;
    for (const auto& name : {QStringLiteral("defaultKey"), QStringLiteral("cyberArkKey")}) {
        if (document.contains(name) && document.value(name).typeId() != QMetaType::QString) {
            error = QStringLiteral("%1 must be a key id or empty text.").arg(name);
            return false;
        }
    }
    const QVariant keyValue = document.value(QStringLiteral("keys"), QVariantList{});
    if (keyValue.typeId() != QMetaType::QVariantList || keyValue.toList().size() > maximumKeys) {
        error = QStringLiteral("keys must be a list containing at most %1 entries.").arg(maximumKeys);
        return false;
    }
    QSet<QString> ids;
    QSet<QString> files;
    for (const auto& value : keyValue.toList()) {
        if (value.typeId() != QMetaType::QVariantMap) {
            error = QStringLiteral("Each SSH key entry must be a mapping.");
            return false;
        }
        const QVariantMap entry = value.toMap();
        if (!YamlFile::fields(entry,
                {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("file"),
                 QStringLiteral("format"), QStringLiteral("fingerprint"), QStringLiteral("type"),
                 QStringLiteral("bits")}, error))
            return false;
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString name = entry.value(QStringLiteral("name")).toString();
        const QString file = entry.value(QStringLiteral("file")).toString();
        const QString format = entry.value(QStringLiteral("format")).toString();
        const QString fingerprint = entry.value(QStringLiteral("fingerprint")).toString();
        const QString type = entry.value(QStringLiteral("type")).toString();
        if (!validKeyId(id) || ids.contains(id) || name.isEmpty() || name.size() > 128
            || std::any_of(name.cbegin(), name.cend(), [](QChar ch) { return ch.unicode() < 32 || ch.unicode() == 127; })
            || !keyFilePattern().match(file).hasMatch() || files.contains(file)
            || !QStringList{QStringLiteral("openssh"), QStringLiteral("ppk")}.contains(format)
            || !fingerprintPattern().match(fingerprint).hasMatch() || fingerprint.size() > 128
            || !QStringList{QStringLiteral("ED25519"), QStringLiteral("ECDSA"), QStringLiteral("RSA")}.contains(type)
            || !isNumericType(entry.value(QStringLiteral("bits")).typeId())
            || entry.value(QStringLiteral("bits")).toLongLong() < 1) {
            error = QStringLiteral("Invalid or duplicate SSH key metadata.");
            return false;
        }
        ids.insert(id);
        files.insert(file);
    }
    for (const auto& selected : {document.value(QStringLiteral("defaultKey")).toString(),
                                 document.value(QStringLiteral("cyberArkKey")).toString()}) {
        if (!selected.isEmpty() && !ids.contains(selected)) {
            error = QStringLiteral("A selected default refers to a missing SSH key.");
            return false;
        }
    }
    error.clear();
    return true;
}

QVariantMap SshKeyStore::document() const {
    return {{QStringLiteral("defaultKey"), m_defaultKeyId},
            {QStringLiteral("cyberArkKey"), m_cyberArkKeyId},
            {QStringLiteral("keys"), m_keys}};
}

bool SshKeyStore::save() {
    QString error;
    const QVariantMap values = document();
    if (!validateDocument(values, error) || !m_yaml.save(values)) {
        setLastError(error.isEmpty() ? m_yaml.error() : error);
        return false;
    }
    setLastError({});
    return true;
}

int SshKeyStore::indexOf(const QString& id) const {
    for (int index = 0; index < m_keys.size(); ++index)
        if (m_keys.at(index).toMap().value(QStringLiteral("id")).toString() == id) return index;
    return -1;
}

QString SshKeyStore::importKey(const QString& source, const QString& name,
                               const QString& ppkPassphrase) {
    if (m_keys.size() >= maximumKeys) {
        setLastError(QStringLiteral("The key library is limited to %1 entries.").arg(maximumKeys));
        return {};
    }
    auto imported = readAndConvert(source, ppkPassphrase);
    if (!imported) {
        setLastError(imported.error());
        return {};
    }
    const QString id = generatedId();
    const QString file = generatedFileName(id);
    const QString path = managedPath(file);
    QString error;
    if (!PrivateFile::write(path, imported->bytes, error)) {
        imported->bytes.fill('\0');
        setLastError(QStringLiteral("Cannot store the imported key privately: %1").arg(error));
        return {};
    }
    imported->bytes.fill('\0');
    const auto details = inspectManagedKey(path);
    if (!details) {
        (void)QFile::remove(path);
        setLastError(details.error());
        return {};
    }
    QVariantMap entry{{QStringLiteral("id"), id},
                      {QStringLiteral("name"), cleanedName(name, source)},
                      {QStringLiteral("file"), file},
                      {QStringLiteral("format"), imported->format},
                      {QStringLiteral("fingerprint"), details->value(QStringLiteral("fingerprint"))},
                      {QStringLiteral("type"), details->value(QStringLiteral("type"))},
                      {QStringLiteral("bits"), details->value(QStringLiteral("bits"))}};
    m_keys.append(entry);
    if (!save()) {
        m_keys.removeLast();
        (void)QFile::remove(path);
        return {};
    }
    emit keysChanged();
    return id;
}

bool SshKeyStore::replaceKey(const QString& id, const QString& source,
                             const QString& ppkPassphrase) {
    const int index = indexOf(id);
    if (index < 0) {
        setLastError(QStringLiteral("The selected SSH key no longer exists."));
        return false;
    }
    auto imported = readAndConvert(source, ppkPassphrase);
    if (!imported) {
        setLastError(imported.error());
        return false;
    }
    const QString file = generatedFileName(id);
    const QString path = managedPath(file);
    QString error;
    if (!PrivateFile::write(path, imported->bytes, error)) {
        imported->bytes.fill('\0');
        setLastError(QStringLiteral("Cannot store the replacement key privately: %1").arg(error));
        return false;
    }
    imported->bytes.fill('\0');
    const auto details = inspectManagedKey(path);
    if (!details) {
        (void)QFile::remove(path);
        setLastError(details.error());
        return false;
    }

    const QVariantMap previous = m_keys.at(index).toMap();
    QVariantMap replacement = previous;
    replacement[QStringLiteral("file")] = file;
    replacement[QStringLiteral("format")] = imported->format;
    replacement[QStringLiteral("fingerprint")] = details->value(QStringLiteral("fingerprint"));
    replacement[QStringLiteral("type")] = details->value(QStringLiteral("type"));
    replacement[QStringLiteral("bits")] = details->value(QStringLiteral("bits"));
    m_keys[index] = replacement;
    if (!save()) {
        m_keys[index] = previous;
        (void)QFile::remove(path);
        return false;
    }
    const QString previousPath = managedPath(previous.value(QStringLiteral("file")).toString());
    if (!previousPath.isEmpty()) (void)QFile::remove(previousPath);
    emit keysChanged();
    return true;
}

bool SshKeyStore::removeKey(const QString& id) {
    const int index = indexOf(id);
    if (index < 0) {
        setLastError(QStringLiteral("The selected SSH key no longer exists."));
        return false;
    }
    const QVariantList previousKeys = m_keys;
    const QString previousDefault = m_defaultKeyId;
    const QString previousCyberArk = m_cyberArkKeyId;
    const QString file = m_keys.at(index).toMap().value(QStringLiteral("file")).toString();
    m_keys.removeAt(index);
    if (m_defaultKeyId == id) m_defaultKeyId.clear();
    if (m_cyberArkKeyId == id) m_cyberArkKeyId.clear();
    if (!save()) {
        m_keys = previousKeys;
        m_defaultKeyId = previousDefault;
        m_cyberArkKeyId = previousCyberArk;
        return false;
    }
    const QString path = managedPath(file);
    if (!path.isEmpty()) (void)QFile::remove(path);
    emit keysChanged();
    if (previousDefault != m_defaultKeyId) emit defaultKeyIdChanged();
    if (previousCyberArk != m_cyberArkKeyId) emit cyberArkKeyIdChanged();
    return true;
}

bool SshKeyStore::setDefaultKey(const QString& id) {
    if (!id.isEmpty() && indexOf(id) < 0) {
        setLastError(QStringLiteral("Choose an existing SSH key."));
        return false;
    }
    if (m_defaultKeyId == id) return true;
    const QString previous = m_defaultKeyId;
    m_defaultKeyId = id;
    if (!save()) {
        m_defaultKeyId = previous;
        return false;
    }
    emit defaultKeyIdChanged();
    return true;
}

bool SshKeyStore::setCyberArkKey(const QString& id) {
    if (!id.isEmpty() && indexOf(id) < 0) {
        setLastError(QStringLiteral("Choose an existing SSH key."));
        return false;
    }
    if (m_cyberArkKeyId == id) return true;
    const QString previous = m_cyberArkKeyId;
    m_cyberArkKeyId = id;
    if (!save()) {
        m_cyberArkKeyId = previous;
        return false;
    }
    emit cyberArkKeyIdChanged();
    return true;
}

QVariantMap SshKeyStore::get(const QString& id) const {
    const int index = indexOf(id);
    return index < 0 ? QVariantMap{} : m_keys.at(index).toMap();
}

bool SshKeyStore::reload() {
    QVariantMap values;
    if (!m_yaml.load(values)) {
        setLastError(m_yaml.error());
        return false;
    }
    QString error;
    if (!validateDocument(values, error)) {
        m_yaml.reject(error);
        setLastError(error);
        return false;
    }
    for (const auto& value : values.value(QStringLiteral("keys"), QVariantList{}).toList()) {
        const QString path = managedPath(value.toMap().value(QStringLiteral("file")).toString());
        if (path.isEmpty() || !PrivateFile::secureEntry(path, error)) {
            error = QStringLiteral("A managed SSH key is missing or unsafe. Key settings are read-only: %1").arg(error);
            m_yaml.reject(error);
            setLastError(error);
            return false;
        }
    }
    m_keys = values.value(QStringLiteral("keys"), QVariantList{}).toList();
    m_defaultKeyId = values.value(QStringLiteral("defaultKey")).toString();
    m_cyberArkKeyId = values.value(QStringLiteral("cyberArkKey")).toString();
    setLastError({});
    emit keysChanged();
    emit defaultKeyIdChanged();
    emit cyberArkKeyIdChanged();
    return true;
}

std::expected<QVariantMap, QString>
SshKeyStore::applyDefaults(const QVariantMap& profile) {
    QVariantMap result = profile;
    const QString requested = profile.value(QStringLiteral("identityKey")).toString();
    if (!requested.isEmpty() && requested != QStringLiteral("none") && !validKeyId(requested))
        return std::unexpected(QStringLiteral("The session contains an invalid SSH key reference."));

    const QString metadataPath = QDir(Platform::appConfigDirectory())
                                     .filePath(QStringLiteral("keys.yaml"));
    if (!QFileInfo::exists(metadataPath)) {
        if (!requested.isEmpty() && requested != QStringLiteral("none"))
            return std::unexpected(QStringLiteral("The SSH key selected by this session is not available on this device."));
        if (requested == QStringLiteral("none")) result.remove(QStringLiteral("identityFile"));
        return result;
    }

    QByteArray bytes;
    QString error;
    QVariantMap values;
    if (!PrivateFile::read(metadataPath, bytes, error)
        || !YamlFile::decode(bytes, QStringLiteral("8bitshell/keys"), values, error, 1)
        || !validateDocument(values, error)) {
        return std::unexpected(QStringLiteral("SSH key settings are invalid; authentication is blocked: %1").arg(error));
    }

    QString selected;
    if (profile.contains(QStringLiteral("psmp")))
        selected = values.value(QStringLiteral("cyberArkKey")).toString();
    if (selected.isEmpty()) {
        if (requested == QStringLiteral("none")) {
            result.remove(QStringLiteral("identityFile"));
            return result;
        }
        if (!requested.isEmpty()) selected = requested;
        else if (!profile.value(QStringLiteral("identityFile")).toString().isEmpty()) return result;
        else selected = values.value(QStringLiteral("defaultKey")).toString();
    }
    if (selected.isEmpty()) return result;

    QVariantMap entry;
    for (const auto& value : values.value(QStringLiteral("keys")).toList()) {
        const auto candidate = value.toMap();
        if (candidate.value(QStringLiteral("id")).toString() == selected) {
            entry = candidate;
            break;
        }
    }
    if (entry.isEmpty())
        return std::unexpected(QStringLiteral("The selected SSH key is missing. Authentication fallback is blocked."));
    const QString path = managedPath(entry.value(QStringLiteral("file")).toString());
    if (path.isEmpty() || !PrivateFile::secureEntry(path, error))
        return std::unexpected(QStringLiteral("The selected SSH key is missing or unsafe: %1").arg(error));
    result[QStringLiteral("identityKey")] = selected;
    result[QStringLiteral("identityFile")] = path;
    return result;
}

void SshKeyStore::setLastError(const QString& error) {
    if (m_lastError == error) return;
    m_lastError = error;
    emit lastErrorChanged();
}

} // namespace bitshell::core
