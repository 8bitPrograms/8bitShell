#include <bitshell/core/PasswordStore.h>

#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/SshSecurity.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dpapi.h>
#elif defined(Q_OS_MACOS)
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

namespace bitshell::core {
namespace {
constexpr qsizetype maximumPasswordBytes = 4096;
constexpr qint64 maximumStorageBytes = 32 * 1024;
const QByteArray markerMagic = QByteArrayLiteral("8bitShell password reference v1\n");

QString canonicalHost(QString host) {
    if (!SshSecurity::safeHost(host)) return {};
    if (host.startsWith(u'[') && host.endsWith(u']')) host = host.mid(1, host.size() - 2);
    if (host.contains(u':')) {
        QHostAddress address;
        if (!address.setAddress(host)) return {};
        return QStringLiteral("ipv6:") + address.toString();
    }
    if (host.endsWith(u'.')) host.chop(1);
    return host.isEmpty() ? QString{} : QStringLiteral("host:") + host.toCaseFolded();
}

bool validPassword(const QByteArray& password) {
    return !password.isEmpty() && password.size() <= maximumPasswordBytes
        && !password.contains('\0') && !password.contains('\r') && !password.contains('\n')
        && QString::fromUtf8(password).toUtf8() == password;
}

QString slotId(const QVariantMap& profile) {
    const auto target = PasswordStore::binding(profile);
    if (target.isEmpty()) return {};
    QString root = QDir::cleanPath(QDir(Platform::appConfigDirectory()).absolutePath());
#ifdef Q_OS_WIN
    root = root.toCaseFolded();
#endif
    const QJsonArray key{QStringLiteral("8bitShell/password/v1"), root,
        profile.value(QStringLiteral("id")).toString(), target};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(key).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

QString slotPath(const QString& slot) {
    return QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("passwords/password-%1.dat").arg(slot));
}

QString eligibilityError() {
    return QStringLiteral("Remembering a password requires a saved direct SSH connection with a username. "
                          "Gateway and jump-host passwords must be entered interactively.");
}

bool markerPresent(const QString& slot) {
    const QFileInfo info(slotPath(slot));
    if (!info.exists() || !info.isFile() || info.isSymLink() || info.size() <= 0
        || info.size() > maximumStorageBytes) return false;
#ifdef Q_OS_WIN
    return true; // DPAPI is checked only immediately before use.
#else
    QByteArray marker;
    QString error;
    return PrivateFile::read(info.absoluteFilePath(), marker, error, 256)
        && marker == markerMagic + slot.toLatin1() + '\n';
#endif
}

#ifdef Q_OS_WIN
QString nativeError(const QString& operation) {
    return QStringLiteral("%1 failed (Windows error %2). The password was not exposed.").arg(operation).arg(GetLastError());
}

std::expected<QByteArray, QString> backendRead(const QString& slot) {
    QByteArray encrypted;
    QString error;
    if (!PrivateFile::read(slotPath(slot), encrypted, error, maximumStorageBytes))
        return std::unexpected(QStringLiteral("Cannot read the protected password: %1").arg(error));
    QByteArray entropy = QByteArrayLiteral("8bitShell/password/v1:") + slot.toLatin1();
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()), reinterpret_cast<BYTE*>(encrypted.data())};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, &extra, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output))
        return std::unexpected(nativeError(QStringLiteral("Windows password decryption")));
    QByteArray password;
    if (output.cbData <= static_cast<DWORD>(maximumPasswordBytes))
        password = QByteArray(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    if (!validPassword(password)) {
        password.fill('\0');
        return std::unexpected(QStringLiteral("The protected password record is invalid."));
    }
    return password;
}

std::expected<void, QString> backendWrite(const QString& slot, const QByteArray& password) {
    QByteArray entropy = QByteArrayLiteral("8bitShell/password/v1:") + slot.toLatin1();
    DATA_BLOB input{static_cast<DWORD>(password.size()), reinterpret_cast<BYTE*>(const_cast<char*>(password.constData()))};
    DATA_BLOB extra{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"8bitShell saved SSH password", &extra, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
        return std::unexpected(nativeError(QStringLiteral("Windows password encryption")));
    const QByteArray encrypted(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    LocalFree(output.pbData);
    QString error;
    if (!PrivateFile::write(slotPath(slot), encrypted, error))
        return std::unexpected(QStringLiteral("Cannot save the protected password: %1").arg(error));
    return {};
}

std::expected<void, QString> backendRemove(const QString&) { return {}; }

#elif defined(Q_OS_MACOS)
struct CfValue {
    CFTypeRef value = nullptr;
    explicit CfValue(CFTypeRef object) : value(object) {}
    ~CfValue() { if (value) CFRelease(value); }
    CfValue(const CfValue&) = delete;
    CfValue& operator=(const CfValue&) = delete;
};

CFMutableDictionaryRef keychainQuery(const QString& slot) {
    auto query = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                          &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (!query) return nullptr;
    const auto account = slot.toUtf8();
    CfValue accountValue(CFStringCreateWithBytes(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(account.constData()), account.size(), kCFStringEncodingUTF8, false));
    if (!accountValue.value) { CFRelease(query); return nullptr; }
    CFDictionarySetValue(query, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(query, kSecAttrService, CFSTR("io.github.8bitPrograms.8bitShell.password.v1"));
    CFDictionarySetValue(query, kSecAttrAccount, accountValue.value);
    CFDictionarySetValue(query, kSecAttrSynchronizable, kCFBooleanFalse);
    // No keychain-unlock dialog can hold a terminal/UI launch indefinitely.
    CFDictionarySetValue(query, kSecUseAuthenticationUI, kSecUseAuthenticationUIFail);
    return query;
}

QString keychainError(OSStatus status) {
    return QStringLiteral("The macOS Keychain could not access the saved password (status %1). "
                          "Unlock the login keychain and retry.").arg(status);
}

std::expected<QByteArray, QString> backendRead(const QString& slot) {
    auto rawQuery = keychainQuery(slot);
    CfValue query(rawQuery);
    if (!rawQuery) return std::unexpected(QStringLiteral("Cannot create a macOS Keychain query."));
    CFDictionarySetValue(rawQuery, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(rawQuery, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef rawResult = nullptr;
    const OSStatus status = SecItemCopyMatching(rawQuery, &rawResult);
    CfValue result(rawResult);
    if (status == errSecItemNotFound) return QByteArray{};
    if (status != errSecSuccess) return std::unexpected(keychainError(status));
    if (!rawResult || CFGetTypeID(rawResult) != CFDataGetTypeID())
        return std::unexpected(QStringLiteral("The macOS Keychain password record is invalid."));
    const auto data = static_cast<CFDataRef>(rawResult);
    const auto length = CFDataGetLength(data);
    if (length < 1 || length > maximumPasswordBytes)
        return std::unexpected(QStringLiteral("The macOS Keychain password record exceeds its size limit."));
    return QByteArray(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), length);
}

std::expected<void, QString> backendWrite(const QString& slot, const QByteArray& password) {
    auto rawQuery = keychainQuery(slot);
    CfValue query(rawQuery);
    CfValue data(CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(password.constData()), password.size()));
    if (!rawQuery || !data.value) return std::unexpected(QStringLiteral("Cannot create a macOS Keychain record."));
    auto rawUpdate = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CfValue update(rawUpdate);
    if (!rawUpdate) return std::unexpected(QStringLiteral("Cannot create a macOS Keychain update."));
    CFDictionarySetValue(rawUpdate, kSecValueData, data.value);
    OSStatus status = SecItemUpdate(rawQuery, rawUpdate);
    if (status == errSecItemNotFound) {
        CFDictionarySetValue(rawQuery, kSecValueData, data.value);
        CFDictionarySetValue(rawQuery, kSecAttrLabel, CFSTR("8bitShell SSH password"));
        status = SecItemAdd(rawQuery, nullptr);
    }
    if (status != errSecSuccess) return std::unexpected(keychainError(status));
    return {};
}

std::expected<void, QString> backendRemove(const QString& slot) {
    auto rawQuery = keychainQuery(slot);
    CfValue query(rawQuery);
    if (!rawQuery) return std::unexpected(QStringLiteral("Cannot create a macOS Keychain query."));
    const OSStatus status = SecItemDelete(rawQuery);
    if (status != errSecSuccess && status != errSecItemNotFound) return std::unexpected(keychainError(status));
    return {};
}

#elif defined(Q_OS_LINUX)
// The desktop's libsecret is optional. Resolve its stable C API at runtime so
// the application remains usable without a running Secret Service or dev package.
struct SecretApi {
    QLibrary library{QStringLiteral("libsecret-1.so.0")};
    void* schema = nullptr;
    using SchemaNew = void* (*)(const char*, int, ...);
    using SchemaUnref = void (*)(void*);
    using Lookup = char* (*)(const void*, void*, void**, ...);
    using Store = int (*)(const void*, const char*, const char*, const char*, void*, void**, ...);
    using Clear = int (*)(const void*, void*, void**, ...);
    using Free = void (*)(void*);
    using CancelNew = void* (*)();
    SchemaUnref schemaUnref = nullptr;
    Lookup lookup = nullptr;
    Store store = nullptr;
    Clear clear = nullptr;
    Free freePassword = nullptr, freeError = nullptr, cancel = nullptr, unref = nullptr;
    CancelNew cancellableNew = nullptr;

    SecretApi() {
        library.setLoadHints(QLibrary::ResolveAllSymbolsHint | QLibrary::PreventUnloadHint);
        if (!library.load()) return;
        const auto schemaNew = reinterpret_cast<SchemaNew>(library.resolve("secret_schema_new"));
        schemaUnref = reinterpret_cast<SchemaUnref>(library.resolve("secret_schema_unref"));
        lookup = reinterpret_cast<Lookup>(library.resolve("secret_password_lookup_sync"));
        store = reinterpret_cast<Store>(library.resolve("secret_password_store_sync"));
        clear = reinterpret_cast<Clear>(library.resolve("secret_password_clear_sync"));
        freePassword = reinterpret_cast<Free>(library.resolve("secret_password_free"));
        freeError = reinterpret_cast<Free>(library.resolve("g_error_free"));
        cancellableNew = reinterpret_cast<CancelNew>(library.resolve("g_cancellable_new"));
        cancel = reinterpret_cast<Free>(library.resolve("g_cancellable_cancel"));
        unref = reinterpret_cast<Free>(library.resolve("g_object_unref"));
        if (schemaNew && schemaUnref && lookup && store && clear && freePassword && freeError
            && cancellableNew && cancel && unref)
            schema = schemaNew("io.github.8bitPrograms.8bitShell.password.v1", 0, "slot", 0, nullptr);
    }
    ~SecretApi() { if (schema) schemaUnref(schema); }
    bool ready() const { return schema && !qEnvironmentVariableIsEmpty("DBUS_SESSION_BUS_ADDRESS"); }
};

SecretApi& secretApi() { static SecretApi api; return api; }

class SecretDeadline {
    SecretApi& m_api;
public:
    explicit SecretDeadline(SecretApi& api) : m_api(api), value(api.cancellableNew()) {
        if (value) m_timer = std::thread([this] {
            std::unique_lock lock(m_mutex);
            if (!m_done.wait_for(lock, std::chrono::seconds(5), [this] { return m_finished; }))
                m_api.cancel(value);
        });
    }
    ~SecretDeadline() {
        { std::lock_guard lock(m_mutex); m_finished = true; }
        m_done.notify_all();
        if (m_timer.joinable()) m_timer.join();
        if (value) m_api.unref(value);
    }
    void* value = nullptr;
private:
    std::mutex m_mutex;
    std::condition_variable m_done;
    bool m_finished = false;
    std::thread m_timer;
};

QString secretError() {
    return QStringLiteral("The desktop Secret Service is unavailable, locked, or did not respond in time. "
                          "Unlock the desktop keyring and retry; the password was not saved to a file.");
}

std::expected<QByteArray, QString> backendRead(const QString& slot) {
    auto& api = secretApi();
    if (!api.ready()) return std::unexpected(secretError());
    SecretDeadline deadline(api);
    if (!deadline.value) return std::unexpected(secretError());
    void* error = nullptr;
    const auto key = slot.toLatin1();
    char* value = api.lookup(api.schema, deadline.value, &error, "slot", key.constData(), nullptr);
    if (error) { api.freeError(error); if (value) api.freePassword(value); return std::unexpected(secretError()); }
    if (!value) return QByteArray{};
    qsizetype length = 0;
    while (length <= maximumPasswordBytes && value[length] != '\0') ++length;
    QByteArray result;
    if (length <= maximumPasswordBytes) result = QByteArray(value, length);
    api.freePassword(value);
    if (result.isEmpty()) return std::unexpected(QStringLiteral("The Secret Service password record is invalid."));
    return result;
}

std::expected<void, QString> backendWrite(const QString& slot, const QByteArray& password) {
    auto& api = secretApi();
    if (!api.ready()) return std::unexpected(secretError());
    SecretDeadline deadline(api);
    if (!deadline.value) return std::unexpected(secretError());
    void* error = nullptr;
    const auto key = slot.toLatin1();
    const bool success = api.store(api.schema, nullptr, "8bitShell SSH password", password.constData(),
                                  deadline.value, &error, "slot", key.constData(), nullptr) != 0;
    if (error) api.freeError(error);
    if (!success || error) return std::unexpected(secretError());
    return {};
}

std::expected<void, QString> backendRemove(const QString& slot) {
    auto& api = secretApi();
    if (!api.ready()) return std::unexpected(secretError());
    SecretDeadline deadline(api);
    if (!deadline.value) return std::unexpected(secretError());
    void* error = nullptr;
    const auto key = slot.toLatin1();
    (void)api.clear(api.schema, deadline.value, &error, "slot", key.constData(), nullptr);
    if (error) { api.freeError(error); return std::unexpected(secretError()); }
    return {};
}
#else
std::expected<QByteArray, QString> backendRead(const QString&) {
    return std::unexpected(QStringLiteral("Secure password storage is unavailable on this operating system."));
}
std::expected<void, QString> backendWrite(const QString&, const QByteArray&) {
    return std::unexpected(QStringLiteral("Secure password storage is unavailable on this operating system."));
}
std::expected<void, QString> backendRemove(const QString&) { return {}; }
#endif

// Tests may stand in for the OS keychain; see PasswordStore::useTestKeychain.
// Windows keeps DPAPI, which works everywhere and stores the secret itself
// in the slot file, so the hook does not apply there.
PasswordStore::TestKeychain* testKeychain = nullptr;
#ifdef Q_OS_WIN
constexpr bool testKeychainApplies = false;
#else
constexpr bool testKeychainApplies = true;
#endif
std::expected<QByteArray, QString> keychainRead(const QString& slot) {
    return testKeychainApplies && testKeychain ? testKeychain->read(slot) : backendRead(slot);
}
std::expected<void, QString> keychainWrite(const QString& slot, const QByteArray& secret) {
    return testKeychainApplies && testKeychain ? testKeychain->write(slot, secret) : backendWrite(slot, secret);
}
std::expected<void, QString> keychainRemove(const QString& slot) {
    return testKeychainApplies && testKeychain ? testKeychain->remove(slot) : backendRemove(slot);
}
} // namespace

void PasswordStore::useTestKeychain(TestKeychain* keychain) { testKeychain = keychain; }

QString PasswordStore::backendName() {
    if (testKeychainApplies && testKeychain) return QStringLiteral("Test keychain");
#ifdef Q_OS_WIN
    return QStringLiteral("Windows account encryption (DPAPI)");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS Keychain");
#elif defined(Q_OS_LINUX)
    return QStringLiteral("Desktop Secret Service");
#else
    return QStringLiteral("Unavailable");
#endif
}

bool PasswordStore::available() {
    if (testKeychainApplies && testKeychain) return true;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    return true;
#elif defined(Q_OS_LINUX)
    return secretApi().ready();
#else
    return false;
#endif
}

QString PasswordStore::binding(const QVariantMap& profile) {
    const auto id = profile.value(QStringLiteral("id")).toString();
    const auto user = profile.value(QStringLiteral("user")).toString();
    const auto host = canonicalHost(profile.value(QStringLiteral("host")).toString());
    const auto connection = profile.value(QStringLiteral("connection")).toMap();
    bool validPort = false;
    const int port = profile.value(QStringLiteral("port"), 22).toInt(&validPort);
    if (profile.value(QStringLiteral("authenticationMode")).toString() != QStringLiteral("password")
        || id.isEmpty() || id.size() > 512 || !SshSecurity::safePath(id)
        || user.isEmpty() || !SshSecurity::safeUser(user) || host.isEmpty()
        || !validPort || port < 1 || port > 65535
        || profile.contains(QStringLiteral("psmp"))
        || !profile.value(QStringLiteral("proxyJump")).toString().isEmpty()
        || (profile.contains(QStringLiteral("connection"))
            && connection.value(QStringLiteral("mode")).toString() != QStringLiteral("direct")))
        return {};
    const QJsonArray endpoint{QStringLiteral("password"), host, user, port};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(endpoint).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

bool PasswordStore::has(const QVariantMap& profile) {
    const auto slot = slotId(profile);
    return !slot.isEmpty() && markerPresent(slot);
}

std::expected<QByteArray, QString> PasswordStore::read(const QVariantMap& profile) {
    const auto slot = slotId(profile);
    if (slot.isEmpty()) return std::unexpected(eligibilityError());
    if (!QFileInfo::exists(slotPath(slot))) return QByteArray{};
    if (!markerPresent(slot)) return std::unexpected(QStringLiteral("The saved password reference is invalid."));
    auto result = keychainRead(slot);
    if (!result || result->isEmpty()) return result;
    if (!validPassword(*result)) {
        result->fill('\0');
        return std::unexpected(QStringLiteral("The saved password contains unsupported data."));
    }
    return result;
}

std::expected<void, QString> PasswordStore::write(const QVariantMap& profile, const QByteArray& password) {
    const auto slot = slotId(profile);
    if (slot.isEmpty()) return std::unexpected(eligibilityError());
    if (!validPassword(password)) return std::unexpected(QStringLiteral("Enter a nonempty password of at most 4096 UTF-8 bytes, without line breaks or NUL."));
#ifndef Q_OS_WIN
    // Keep only an opaque marker locally, never secret data. Prepare it before
    // changing the keyring so a read-only local profile cannot lose an old value.
    const bool existingMarker = QFileInfo::exists(slotPath(slot));
    QString error;
    if (!PrivateFile::write(slotPath(slot), markerMagic + slot.toLatin1() + '\n', error))
        return std::unexpected(QStringLiteral("Cannot save the local password reference: %1").arg(error));
#endif
    const auto result = keychainWrite(slot, password);
#ifndef Q_OS_WIN
    if (!result && !existingMarker) (void)QFile::remove(slotPath(slot));
#endif
    return result;
}

std::expected<void, QString> PasswordStore::remove(const QVariantMap& profile) {
    const auto slot = slotId(profile);
    if (slot.isEmpty() || !QFileInfo::exists(slotPath(slot))) return {};
    if (!markerPresent(slot)) return std::unexpected(QStringLiteral("The saved password reference is invalid; it was not followed."));
    const auto removed = keychainRemove(slot);
    if (!removed) return removed;
    // The path is generated from a SHA-256 slot and never from profile text.
    // QFile removes a link itself rather than traversing its destination.
    if (!QFile::remove(slotPath(slot)))
        return std::unexpected(QStringLiteral("Cannot remove the local saved-password record."));
    return {};
}

// Credential-profile secrets reuse the backends above without changing them.
// A header entry holds the chunk count; chunks hold base64 text, which fits
// the backends' 4096-byte, no-line-break limit.
namespace {
constexpr qsizetype secretChunkBytes = 3000;
constexpr int maximumSecretChunks = 22; // about 64 KiB

QString secretSlot(const QString& reference, int chunk) {
    QString root = QDir::cleanPath(QDir(Platform::appConfigDirectory()).absolutePath());
#ifdef Q_OS_WIN
    root = root.toCaseFolded();
#endif
    const QJsonArray key{QStringLiteral("8bitShell/credential/v1"), root, reference, chunk};
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(key).toJson(QJsonDocument::Compact),
                                                       QCryptographicHash::Sha256).toHex());
}

std::expected<void, QString> removeSlot(const QString& slot) {
    const auto removed = keychainRemove(slot);
#ifdef Q_OS_WIN
    // The DPAPI backend keeps the value in this file; its remove is a no-op.
    if (QFileInfo::exists(slotPath(slot)) && !QFile::remove(slotPath(slot)))
        return std::unexpected(QStringLiteral("Cannot remove the local credential record."));
#endif
    return removed;
}

std::expected<int, QString> secretChunks(const QString& reference) {
#ifdef Q_OS_WIN
    if (!QFileInfo::exists(slotPath(secretSlot(reference, -1)))) return 0;
#endif
    auto header = keychainRead(secretSlot(reference, -1));
    if (!header) return std::unexpected(header.error());
    if (header->isEmpty()) return 0;
    bool valid = false;
    const int count = header->startsWith("v1:") ? header->sliced(3).toInt(&valid) : 0;
    header->fill('\0');
    if (!valid || count < 1 || count > maximumSecretChunks)
        return std::unexpected(QStringLiteral("The saved credential record is invalid."));
    return count;
}
} // namespace

std::expected<QByteArray, QString> PasswordStore::readSecret(const QString& reference) {
    const auto count = secretChunks(reference);
    if (!count) return std::unexpected(count.error());
    QByteArray encoded;
    for (int chunk = 0; chunk < *count; ++chunk) {
        auto part = keychainRead(secretSlot(reference, chunk));
        if (!part || part->isEmpty()) {
            encoded.fill('\0');
            return std::unexpected(part ? QStringLiteral("The saved credential is incomplete.") : part.error());
        }
        encoded += *part;
        part->fill('\0');
    }
    auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    encoded.fill('\0');
    if (!decoded) return std::unexpected(QStringLiteral("The saved credential contains invalid data."));
    return std::move(decoded.decoded);
}

std::expected<void, QString> PasswordStore::writeSecret(const QString& reference, const QByteArray& secret) {
    if (reference.isEmpty() || secret.isEmpty())
        return std::unexpected(QStringLiteral("Nothing to save."));
    QByteArray encoded = secret.toBase64();
    const int count = static_cast<int>((encoded.size() + secretChunkBytes - 1) / secretChunkBytes);
    if (count > maximumSecretChunks) {
        encoded.fill('\0');
        return std::unexpected(QStringLiteral("The secret is larger than 64 KiB."));
    }
    const auto previous = secretChunks(reference);
    for (int chunk = 0; chunk < count; ++chunk) {
        QByteArray part = encoded.sliced(chunk * secretChunkBytes,
                                         std::min(secretChunkBytes, encoded.size() - chunk * secretChunkBytes));
        const auto written = keychainWrite(secretSlot(reference, chunk), part);
        part.fill('\0');
        if (!written) { encoded.fill('\0'); return written; }
    }
    encoded.fill('\0');
    const auto header = keychainWrite(secretSlot(reference, -1), "v1:" + QByteArray::number(count));
    if (!header) return header;
    for (int chunk = count; previous && chunk < *previous; ++chunk)
        (void)removeSlot(secretSlot(reference, chunk));
    return {};
}

std::expected<void, QString> PasswordStore::removeSecret(const QString& reference) {
    const auto count = secretChunks(reference);
    const auto header = removeSlot(secretSlot(reference, -1));
    for (int chunk = 0; chunk < (count ? *count : maximumSecretChunks); ++chunk)
        (void)removeSlot(secretSlot(reference, chunk));
    if (!header) return header;
    return {};
}
} // namespace bitshell::core
