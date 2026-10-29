#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/Platform.h>
#include <QDir>
#include <QFileInfo>
#include <QUuid>
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <io.h>
#include <fcntl.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bitshell::core {
namespace {
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (valid()) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
    bool valid() const { return value && value != INVALID_HANDLE_VALUE; }
    HANDLE release() { return std::exchange(value, INVALID_HANDLE_VALUE); }
};
QString failure(const char* operation) {
    return QStringLiteral("%1 failed (Windows error %2).").arg(QString::fromLatin1(operation)).arg(GetLastError());
}
std::wstring nativePath(const QString& path) {
    return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString();
}
QString checkedPath(const QString& input, QString& error) {
    // No UNC/network paths, NT device namespace, ADS, DOS devices, or Win32
    // trailing-dot/space aliases in private storage. Fail visibly, not fallback.
    QString path = QDir::fromNativeSeparators(input);
    if (path.isEmpty() || path.size() > 4096 || path.startsWith(QStringLiteral("//"))) {
        error = QStringLiteral("Private storage requires a local drive path."); return {};
    }
    if (QDir::isRelativePath(path)) path = QDir::current().absoluteFilePath(path);
    path = QDir::cleanPath(path);
    if (path.size() < 4 || !path[0].isLetter() || path[0].unicode() > 127 || path[1] != u':' || path[2] != u'/') {
        error = QStringLiteral("Private storage requires an absolute local drive path."); return {};
    }
    for (const auto& part : path.sliced(3).split(u'/')) {
        if (!Platform::windowsFileName(part)) { error = QStringLiteral("Unsupported Windows file name or alternate data stream."); return {}; }
    }
    return path;
}
struct Security {
    std::vector<unsigned char> tokenData;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    SECURITY_ATTRIBUTES attributes{sizeof(SECURITY_ATTRIBUTES), nullptr, FALSE};
    PSID user = nullptr;
    ~Security() { if (descriptor) LocalFree(descriptor); }
    bool initialize(QString& error) {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw)) { error = failure("OpenProcessToken"); return false; }
        Handle token(raw);
        DWORD count = 0;
        GetTokenInformation(token.value, TokenUser, nullptr, 0, &count);
        tokenData.resize(count);
        if (!count || !GetTokenInformation(token.value, TokenUser, tokenData.data(), count, &count)) { error = failure("TokenUser"); return false; }
        user = reinterpret_cast<TOKEN_USER*>(tokenData.data())->User.Sid;
        LPWSTR sid = nullptr;
        if (!ConvertSidToStringSidW(user, &sid)) { error = failure("ConvertSid"); return false; }
        const QString userText = QString::fromWCharArray(sid);
        LocalFree(sid);
        const auto sddl = (QStringLiteral("O:") + userText + QStringLiteral("D:P(A;OICI;FA;;;") + userText + QStringLiteral(")(A;OICI;FA;;;SY)")).toStdWString();
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor, nullptr)) {
            error = failure("Private DACL"); return false;
        }
        attributes.lpSecurityDescriptor = descriptor;
        return true;
    }
    bool owns(HANDLE file, QString& error) const {
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR details = nullptr;
        const DWORD result = GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &details);
        const bool own = result == ERROR_SUCCESS && owner && EqualSid(owner, user);
        if (details) LocalFree(details);
        if (!own) error = QStringLiteral("Private file must be owned by the current Windows user.");
        return own;
    }
    bool protect(HANDLE file, QString& error) const {
        PACL acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
        if (!GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) || !present || !acl) return false;
        const DWORD result = SetSecurityInfo(file, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                            nullptr, nullptr, acl, nullptr);
        if (result != ERROR_SUCCESS) { error = QStringLiteral("Cannot restrict Windows file access (%1).").arg(result); return false; }
        return true;
    }
};
bool regular(HANDLE file, bool directory, bool singleLink, QString& error) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (GetFileType(file) != FILE_TYPE_DISK || !GetFileInformationByHandle(file, &info)
        || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        || bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != directory
        || (singleLink && !directory && info.nNumberOfLinks != 1)) {
        error = QStringLiteral("Reparse points, devices, and multiply-linked private files are not allowed."); return false;
    }
    return true;
}
bool renameWith(HANDLE file, const std::wstring& name, bool replace, bool extended) {
    const auto nameBytes = name.size() * sizeof(wchar_t);
    std::vector<unsigned char> storage(offsetof(FILE_RENAME_INFO, FileName) + nameBytes + sizeof(wchar_t));
    std::memset(storage.data(), 0, storage.size());
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->RootDirectory = nullptr;
    rename->FileNameLength = DWORD(nameBytes);
    std::memcpy(rename->FileName, name.data(), nameBytes);
    if (!extended) {
        rename->ReplaceIfExists = replace ? TRUE : FALSE;
        return SetFileInformationByHandle(file, FileRenameInfo, rename, DWORD(storage.size()));
    }
    rename->Flags = FILE_RENAME_REPLACE_IF_EXISTS | FILE_RENAME_POSIX_SEMANTICS
        | FILE_RENAME_IGNORE_READONLY_ATTRIBUTE;
    return SetFileInformationByHandle(file, FileRenameInfoEx, rename, DWORD(storage.size()));
}
bool renameHandle(HANDLE file, const QString& destination, bool replace) {
    const auto win32 = QDir::toNativeSeparators(destination).toStdWString();
    if (!replace) return renameWith(file, win32, false, false);
    // NT path + FileRenameInfoEx can replace a file the parent may delete even
    // when that file's own inherited ACL does not grant DELETE. Fall back to the
    // Win32 rename that already works for files this process created.
    if (renameWith(file, std::wstring(L"\\??\\") + win32, true, true)) return true;
    return renameWith(file, win32, true, false);
}
bool allowReplace(const std::wstring& native, const Security& security, QString& error) {
    Handle writable(CreateFileW(native.c_str(), READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    if (writable.valid() && security.protect(writable.value, error)) return true;
    error.clear();
    PACL acl = nullptr; BOOL present = FALSE, defaulted = FALSE;
    if (!GetSecurityDescriptorDacl(security.descriptor, &present, &acl, &defaulted) || !present || !acl) {
        error = QStringLiteral("Cannot build the private file DACL."); return false;
    }
    // The owner can replace an inherited DACL that never granted WRITE_DAC.
    const DWORD result = SetNamedSecurityInfoW(const_cast<LPWSTR>(native.c_str()), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr, nullptr, acl, nullptr);
    if (result != ERROR_SUCCESS) {
        error = QStringLiteral("Cannot restrict Windows file access (%1).").arg(result); return false;
    }
    return true;
}
struct Parents {
    QString path;
    std::vector<Handle> handles;
    Security security;
    bool open(const QString& input, bool create, QString& error) {
        path = checkedPath(input, error);
        if (path.isEmpty() || !security.initialize(error)) return false;
        const QString parent = QFileInfo(path).absolutePath();
        QString current = path.first(3);
        auto lockDirectory = [&](const QString& directory) {
            const auto native = nativePath(directory);
            Handle handle(CreateFileW(native.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            if (!handle.valid() || !regular(handle.value, true, false, error)) {
                if (error.isEmpty()) error = failure("Open parent directory"); return false;
            }
            // Keeping every ancestor open without FILE_SHARE_DELETE pins the
            // path while the final file is created/read/renamed.
            handles.push_back(std::move(handle));
            return true;
        };
        if (!lockDirectory(current)) return false;
        for (const auto& part : parent.sliced(3).split(u'/', Qt::SkipEmptyParts)) {
            if (!current.endsWith(u'/')) current += u'/';
            current += part;
            if (create && !CreateDirectoryW(nativePath(current).c_str(), &security.attributes) && GetLastError() != ERROR_ALREADY_EXISTS) {
                error = failure("Create private directory"); return false;
            }
            if (!lockDirectory(current)) return false;
        }
        return true;
    }
};
}
bool PrivateFile::prepareParent(const QString& path, QString& error) {
    error.clear(); Parents parents; return parents.open(path, true, error);
}
bool PrivateFile::openLog(QFile& file, bool append, QString& error) {
    error.clear(); Parents parents;
    if (!parents.open(file.fileName(), true, error)) return false;
    Handle handle(CreateFileW(nativePath(parents.path).c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ, &parents.security.attributes, OPEN_ALWAYS, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle.valid()) { error = failure("Open private log"); return false; }
    if (!regular(handle.value, false, true, error) || !parents.security.owns(handle.value, error) || !parents.security.protect(handle.value, error)) return false;
    LARGE_INTEGER start{};
    if ((!append && (!SetFilePointerEx(handle.value, start, nullptr, FILE_BEGIN) || !SetEndOfFile(handle.value)))
        || (append && !SetFilePointerEx(handle.value, start, nullptr, FILE_END))) { error = failure("Position log"); return false; }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(handle.value), _O_BINARY | _O_WRONLY | _O_NOINHERIT | (append ? _O_APPEND : 0));
    if (fd < 0) { error = QStringLiteral("Cannot adopt the private log handle."); return false; }
    handle.release();
    if (!file.open(fd, QIODevice::WriteOnly | (append ? QIODevice::Append : QIODevice::OpenMode{}), QFileDevice::AutoCloseHandle)) {
        _close(fd); error = file.errorString(); return false;
    }
    return true;
}
bool PrivateFile::read(const QString& path, QByteArray& bytes, QString& error, qint64 limit) {
    error.clear(); bytes.clear(); Parents parents;
    if (limit < 0 || !parents.open(path, false, error)) return false;
    Handle handle(CreateFileW(nativePath(parents.path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!handle.valid() || !regular(handle.value, false, false, error)) { if (error.isEmpty()) error = failure("Read private file"); return false; }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle.value, &size) || size.QuadPart < 0 || size.QuadPart > limit) { error = QStringLiteral("Input exceeds its size limit."); return false; }
    char buffer[65536]; DWORD count = 0;
    while (ReadFile(handle.value, buffer, sizeof(buffer), &count, nullptr)) {
        if (!count) return true;
        if (bytes.size() + qint64(count) > limit) { bytes.clear(); error = QStringLiteral("Input grew beyond its size limit."); return false; }
        bytes.append(buffer, qsizetype(count));
    }
    bytes.clear(); error = failure("ReadFile"); return false;
}
bool PrivateFile::write(const QString& path, const QByteArray& bytes, QString& error) {
    error.clear();
    if (bytes.size() > 8 * 1024 * 1024) { error = QStringLiteral("File exceeds the 8 MiB limit."); return false; }
    Parents parents;
    if (!parents.open(path, true, error)) return false;
    const auto target = nativePath(parents.path);
    {
        Handle existing(CreateFileW(target.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
        if (existing.valid()) {
            if (!regular(existing.value, false, true, error) || !parents.security.owns(existing.value, error)
                || !allowReplace(target, parents.security, error)) return false;
        } else if (GetLastError() != ERROR_FILE_NOT_FOUND) { error = failure("Inspect write destination"); return false; }
    }
    const auto temporary = nativePath(QFileInfo(parents.path).absolutePath() + QStringLiteral("/.bitshell-save-") + QUuid::createUuid().toString(QUuid::Id128));
    Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE | DELETE, 0, &parents.security.attributes,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (!output.valid()) { error = failure("Create atomic private file"); return false; }
    bool ok = true; qsizetype offset = 0;
    while (offset < bytes.size()) {
        DWORD written = 0;
        if (!WriteFile(output.value, bytes.constData() + offset, DWORD(bytes.size() - offset), &written, nullptr) || !written) { ok = false; break; }
        offset += qsizetype(written);
    }
    ok = ok && FlushFileBuffers(output.value);
    // Commit the still-open, non-shareable handle: no close/reopen name race.
    if (ok) ok = renameHandle(output.value, parents.path, true);
    if (!ok) {
        error = failure("Commit atomic private file");
        FILE_DISPOSITION_INFO dispose{TRUE};
        (void)SetFileInformationByHandle(output.value, FileDispositionInfo, &dispose, sizeof(dispose));
    }
    return ok;
}
bool PrivateFile::secureEntry(const QString& path, QString& error) {
    error.clear(); Parents parents;
    if (!parents.open(path, false, error)) return false;
    Handle handle(CreateFileW(nativePath(parents.path).c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL | WRITE_DAC,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!handle.valid() || !GetFileInformationByHandle(handle.value, &info)) { error = failure("Inspect private entry"); return false; }
    return regular(handle.value, bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), true, error)
        && parents.security.owns(handle.value, error) && parents.security.protect(handle.value, error);
}
bool PrivateFile::publishNoReplace(const QString& source, const QString& destination, bool& collision, QString& error) {
    error.clear(); collision = false; Parents from, to;
    if (!from.open(source, false, error) || !to.open(destination, false, error)) return false;
    Handle handle(CreateFileW(nativePath(from.path).c_str(), DELETE | READ_CONTROL | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (!handle.valid() || !GetFileInformationByHandle(handle.value, &info)) { error = failure("Open download for publication"); return false; }
    if (!regular(handle.value, bool(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY), true, error) || !from.security.owns(handle.value, error)) return false;
    if (renameHandle(handle.value, to.path, false)) return true;
    const DWORD code = GetLastError();
    collision = code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS;
    error = failure("Publish without replacement"); return false;
}
} // namespace bitshell::core
