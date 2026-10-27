#include <bitshell/core/LocalProfileStore.h>
#include <bitshell/core/Platform.h>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringDecoder>
#include <QUuid>
#include <algorithm>

namespace bitshell::core {
namespace {
bool cleanText(const QString& text, qsizetype maximum) {
    return text.size() <= maximum && std::none_of(text.cbegin(), text.cend(), [](QChar c) {
        return c.unicode() < 32 || c.unicode() == 127;
    });
}
QVariantMap entry(const QString& id, const QString& name, const QString& program,
                  const QStringList& arguments = {}, const QString& directory = {}) {
    QVariantList args;
    for (const auto& argument : arguments) args << argument;
    return {{QStringLiteral("id"), id}, {QStringLiteral("name"), name},
        {QStringLiteral("program"), program}, {QStringLiteral("arguments"), args},
        {QStringLiteral("workingDirectory"), directory}, {QStringLiteral("kind"), QStringLiteral("local")},
        {QStringLiteral("distribution"), QString{}}};
}
bool usableExecutable(const QString& path) {
    const QFileInfo info(path);
    return !path.isEmpty() && info.exists() && !info.isDir();
}
void rememberExecutable(QVariantList& detected, QSet<QString>& seen, const QString& id,
                        const QString& name, const QString& path, const QStringList& arguments) {
    if (!usableExecutable(path)) return;
    const auto absolute = QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath());
    const auto key = absolute.toLower();
    if (seen.contains(key)) return;
    seen.insert(key);
    detected << entry(id, name, absolute, arguments);
}
QStringList registryValues(const QString& key, const QStringList& names) {
    QStringList values;
    QSettings settings(key, QSettings::NativeFormat);
    for (const auto& name : names) {
        const auto own = settings.value(name).toString().trimmed();
        if (!own.isEmpty()) values << own;
    }
    const auto groups = settings.childGroups();
    for (const auto& group : groups) {
        settings.beginGroup(group);
        for (const auto& name : names) {
            const auto value = settings.value(name).toString().trimmed();
            if (!value.isEmpty()) values << value;
        }
        settings.endGroup();
    }
    return values;
}
QStringList registryWslNames() {
    QStringList names;
#ifndef Q_OS_WIN
    return names;
#else
    for (auto name : registryValues(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Lxss"),
                                    {QStringLiteral("DistributionName")})) {
        if (name.startsWith(QChar(0xfeff))) name.remove(0, 1);
        name = name.trimmed();
        if (!name.isEmpty() && !name.startsWith(u'-') && cleanText(name, 256) && !names.contains(name))
            names << name;
    }
    return names;
#endif
}
QVariantMap wslEntry(const QString& name) {
    auto result = entry(QStringLiteral("wsl:") + name, name + QStringLiteral(" (WSL)"),
                       Platform::windowsSystemDirectory() + QStringLiteral("/wsl.exe"), {}, QStringLiteral("~"));
    result[QStringLiteral("kind")] = QStringLiteral("wsl");
    result[QStringLiteral("distribution")] = name;
    return result;
}
}
LocalProfileStore::LocalProfileStore(QObject* parent) : QObject(parent),
    m_yaml(QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("local-profiles.yaml")),
           QStringLiteral("8bitshell/local-profiles")) {
    discoverNative();
    reload();
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(8000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        finishDiscovery(false, QStringLiteral("WSL discovery timed out. You can refresh the list."));
        m_wsl.kill();
    });
    connect(&m_wsl, &QProcess::readyReadStandardOutput, this, [this] {
        m_wslOutput += m_wsl.readAllStandardOutput();
        if (m_wslOutput.size() > 65536) {
            finishDiscovery(false, QStringLiteral("WSL returned an oversized distribution list."));
            m_wsl.kill();
        }
    });
    connect(&m_wsl, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finishDiscovery(code == 0 && status == QProcess::NormalExit,
                        QStringLiteral("WSL is unavailable. Check its installation and refresh the list."));
    });
    connect(&m_wsl, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        finishDiscovery(false, QStringLiteral("Could not list WSL distributions."));
    });
    QTimer::singleShot(0, this, &LocalProfileStore::refresh);
}
LocalProfileStore::~LocalProfileStore() {
    if (m_wsl.state() != QProcess::NotRunning) m_wsl.kill();
}
void LocalProfileStore::discoverNative() {
    m_detected.clear();
#ifdef Q_OS_WIN
    QSet<QString> seen;
    rememberExecutable(m_detected, seen, QStringLiteral("system"), QStringLiteral("Windows PowerShell"),
                       Platform::localShell(), Platform::localShellArguments());
    QStringList candidates{QStandardPaths::findExecutable(QStringLiteral("pwsh.exe")),
                           QStandardPaths::findExecutable(QStringLiteral("pwsh")),
                           QStandardPaths::findExecutable(QStringLiteral("powershell.exe"))};
    for (const auto& variable : {"ProgramFiles", "ProgramW6432", "ProgramFiles(x86)", "LOCALAPPDATA"}) {
        const QString base = qEnvironmentVariable(variable);
        if (base.isEmpty()) continue;
        const QDir directory(QDir(base).filePath(QStringLiteral("PowerShell")));
        for (const auto& child : directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
            candidates << directory.filePath(child + QStringLiteral("/pwsh.exe"));
        candidates << QDir(base).filePath(QStringLiteral("Microsoft/WindowsApps/pwsh.exe"));
        candidates << QDir(base).filePath(QStringLiteral("Microsoft/WindowsApps/powershell.exe"));
    }
    const QDir home(QDir::homePath());
    candidates << home.filePath(QStringLiteral("scoop/apps/pwsh/current/pwsh.exe"));
    candidates << home.filePath(QStringLiteral("scoop/shims/pwsh.exe"));
    for (const auto& key : {QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\PowerShellCore\\InstalledVersions"),
                            QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\PowerShellCore\\InstalledVersions"),
                            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\PowerShell\\1\\PowerShellEngine"),
                            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\PowerShell\\3\\PowerShellEngine")}) {
        for (const auto& install : registryValues(key, {QStringLiteral("InstallPath"), QStringLiteral("ApplicationBase")})) {
            const QDir directory(install);
            candidates << directory.filePath(QStringLiteral("pwsh.exe"));
            candidates << directory.filePath(QStringLiteral("powershell.exe"));
        }
    }
    for (const auto& candidate : candidates) {
        if (!usableExecutable(candidate)) continue;
        const QFileInfo info(candidate);
        const auto absolute = QDir::fromNativeSeparators(info.absoluteFilePath());
        if (seen.contains(absolute.toLower())) continue;
        const auto suffix = QString::fromLatin1(QCryptographicHash::hash(absolute.toLower().toUtf8(), QCryptographicHash::Sha256).toHex().left(16));
        const bool modern = info.fileName().compare(QStringLiteral("pwsh.exe"), Qt::CaseInsensitive) == 0;
        rememberExecutable(m_detected, seen, (modern ? QStringLiteral("pwsh:") : QStringLiteral("powershell:")) + suffix,
            (modern ? QStringLiteral("PowerShell (%1)") : QStringLiteral("Windows PowerShell (%1)")).arg(info.dir().dirName()),
            absolute, {QStringLiteral("-NoLogo")});
    }
    rememberExecutable(m_detected, seen, QStringLiteral("cmd"), QStringLiteral("Command Prompt"),
                       Platform::windowsSystemDirectory() + QStringLiteral("/cmd.exe"), {QStringLiteral("/D")});
    for (const auto& name : registryWslNames()) m_detected << wslEntry(name);
#else
    const QString shell = Platform::localShell();
    m_detected << entry(QStringLiteral("system"), QStringLiteral("System shell (%1)").arg(QFileInfo(shell).fileName()),
                        shell, Platform::localShellArguments());
    for (const auto& name : {QStringLiteral("bash"), QStringLiteral("zsh"), QStringLiteral("fish")}) {
        const auto path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty() && QFileInfo(path).canonicalFilePath() != QFileInfo(shell).canonicalFilePath())
            m_detected << entry(QStringLiteral("shell:") + name, name, path, {QStringLiteral("-l")});
    }
#endif
}
void LocalProfileStore::refresh() {
    if (m_discovering || m_wsl.state() != QProcess::NotRunning) return;
    QVariantList previousWsl;
    for (const auto& value : m_detected)
        if (value.toMap().value(QStringLiteral("kind")) == QStringLiteral("wsl")) previousWsl << value;
    discoverNative();
    m_discoveryMessage.clear();
#ifdef Q_OS_WIN
    const auto program = Platform::windowsSystemDirectory() + QStringLiteral("/wsl.exe");
    if (QFileInfo::exists(program)) {
        m_detected += previousWsl;
        m_wslOutput.clear();
        m_discovering = true;
        m_wsl.setProgram(program);
        m_wsl.setArguments({QStringLiteral("--list"), QStringLiteral("--quiet")});
        m_wsl.setStandardInputFile(QProcess::nullDevice());
        m_wsl.setStandardErrorFile(QProcess::nullDevice());
        m_timeout.start();
        m_wsl.start();
    }
#endif
    emit changed();
}
void LocalProfileStore::finishDiscovery(bool success, const QString& message) {
    if (!m_discovering) return;
    m_timeout.stop();
    m_discovering = false;
    if (success) {
        m_wslOutput += m_wsl.readAllStandardOutput();
        for (auto it = m_detected.begin(); it != m_detected.end();) {
            if (it->toMap().value(QStringLiteral("kind")) == QStringLiteral("wsl")) it = m_detected.erase(it);
            else ++it;
        }
        QStringList names = distributionNames(m_wslOutput);
        for (const auto& name : registryWslNames())
            if (!names.contains(name)) names << name;
        for (const auto& name : names) m_detected << wslEntry(name);
        m_discoveryMessage.clear();
    } else m_discoveryMessage = message;
    m_wslOutput.clear();
    emit changed();
}
QStringList LocalProfileStore::distributionNames(const QByteArray& output) {
    if (output.size() > 65536) return {};
    // wsl.exe redirects UTF-16LE on Windows; newer versions may return UTF-8.
    QStringDecoder decoder(output.startsWith("\xff\xfe") || output.contains('\0')
                               ? QStringDecoder::Utf16LE : QStringDecoder::Utf8);
    const QString text = decoder(output);
    if (decoder.hasError()) return {};
    QStringList names;
    for (auto line : text.split(u'\n')) {
        line = line.trimmed();
        if (line.startsWith(QChar(0xfeff))) line.remove(0, 1);
        if (!line.isEmpty() && !line.startsWith(u'-') && cleanText(line, 256) && !names.contains(line)) names << line;
        if (names.size() == 128) break;
    }
    return names;
}
QVariantList LocalProfileStore::profiles() const {
    QVariantList result;
    QSet<QString> included;
    for (const auto& value : m_detected) {
        const auto id = value.toMap().value(QStringLiteral("id")).toString();
        result << profile(id);
        included.insert(id);
    }
    for (const auto& value : m_saved) {
        const auto id = value.toMap().value(QStringLiteral("id")).toString();
        if (!included.contains(id)) { result << profile(id); included.insert(id); }
    }
    // Keep a selected WSL default launchable during asynchronous discovery.
    if (!included.contains(m_default) && m_default.startsWith(QStringLiteral("wsl:"))) result << profile(m_default);
    return result;
}
QVariantMap LocalProfileStore::profile(const QString& id) const {
    QVariantMap result;
    bool detected = false, customized = false;
    for (const auto& value : m_detected) {
        if (value.toMap().value(QStringLiteral("id")) == id) { result = value.toMap(); detected = true; break; }
    }
    if (result.isEmpty() && id.startsWith(QStringLiteral("wsl:")) && id.size() > 4)
        result = wslEntry(id.sliced(4));
    for (const auto& value : m_saved) {
        if (value.toMap().value(QStringLiteral("id")) == id) { result = value.toMap(); customized = true; break; }
    }
    if (!result.isEmpty()) {
        result[QStringLiteral("detected")] = detected;
        result[QStringLiteral("customized")] = customized;
    }
    return result;
}
bool LocalProfileStore::validateProfile(const QVariantMap& value, QString& error) {
    const auto fail = [&](const QString& message) { error = message; return false; };
    if (!YamlFile::fields(value, {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("program"),
        QStringLiteral("arguments"), QStringLiteral("workingDirectory"), QStringLiteral("kind"), QStringLiteral("distribution")}, error)) return false;
    for (const auto& key : {QStringLiteral("id"), QStringLiteral("name"), QStringLiteral("program"),
                            QStringLiteral("workingDirectory"), QStringLiteral("kind"), QStringLiteral("distribution")}) {
        if (value.value(key).typeId() != QMetaType::QString || !cleanText(value.value(key).toString(), 32766))
            return fail(QStringLiteral("Profile fields must be text without line breaks or control characters."));
    }
    if (value.value(QStringLiteral("id")).toString().isEmpty() || value.value(QStringLiteral("id")).toString().size() > 300
        || value.value(QStringLiteral("name")).toString().trimmed().isEmpty() || value.value(QStringLiteral("name")).toString().size() > 128
        || value.value(QStringLiteral("program")).toString().trimmed().isEmpty())
        return fail(QStringLiteral("A profile needs a name (up to 128 characters) and an executable."));
    const auto kind = value.value(QStringLiteral("kind")).toString();
    if (kind != QStringLiteral("local") && kind != QStringLiteral("wsl")) return fail(QStringLiteral("Unknown local profile type."));
    const auto distro = value.value(QStringLiteral("distribution")).toString();
    if (kind == QStringLiteral("wsl") && (distro.isEmpty() || distro.startsWith(u'-') || distro.size() > 256))
        return fail(QStringLiteral("Choose a valid WSL distribution."));
    const auto arguments = value.value(QStringLiteral("arguments"));
    if (arguments.typeId() != QMetaType::QVariantList || arguments.toList().size() > 120)
        return fail(QStringLiteral("Arguments must be a list of at most 120 literal values."));
    qsizetype size = 0;
    for (const auto& argument : arguments.toList()) {
        if (argument.typeId() != QMetaType::QString || !cleanText(argument.toString(), 32766))
            return fail(QStringLiteral("Each argument must be text without line breaks or control characters."));
        size += argument.toString().size() + 1;
    }
    if (size > 30000) return fail(QStringLiteral("The argument list is too long."));
    error.clear();
    return true;
}
QVariantMap LocalProfileStore::prepareLaunch(const QVariantMap& profile, QString& error) {
    auto value = profile;
    value.remove(QStringLiteral("detected")); value.remove(QStringLiteral("customized"));
    if (!validateProfile(value, error)) return {};
    QString program = value.value(QStringLiteral("program")).toString();
    QString directory = value.value(QStringLiteral("workingDirectory")).toString();
    QStringList arguments;
    if (value.value(QStringLiteral("kind")) == QStringLiteral("wsl")) {
#ifdef Q_OS_WIN
        program = Platform::windowsSystemDirectory() + QStringLiteral("/wsl.exe");
        arguments << QStringLiteral("--distribution") << value.value(QStringLiteral("distribution")).toString()
                  << QStringLiteral("--cd") << (directory.isEmpty() ? QStringLiteral("~") : directory);
        directory.clear();
#else
        error = QStringLiteral("WSL profiles require Windows."); return {};
#endif
    } else {
        if (program == QStringLiteral("~") || program.startsWith(QStringLiteral("~/")) || program.startsWith(QStringLiteral("~\\")))
            program = QDir::homePath() + program.sliced(1);
        if (directory == QStringLiteral("~") || directory.startsWith(QStringLiteral("~/")) || directory.startsWith(QStringLiteral("~\\")))
            directory = QDir::homePath() + directory.sliced(1);
        if (!directory.isEmpty() && (!QDir::isAbsolutePath(directory) || !QFileInfo(directory).isDir())) {
            error = QStringLiteral("Starting directory does not exist or is not absolute: %1").arg(directory); return {};
        }
    }
    for (const auto& arg : value.value(QStringLiteral("arguments")).toList()) arguments << arg.toString();
    if (!QDir::isAbsolutePath(program)) program = QStandardPaths::findExecutable(program);
    if (program.isEmpty() || !QFileInfo(program).isFile() || !QFileInfo(program).isExecutable()) {
        error = QStringLiteral("Executable was not found. Use its full path or a program on PATH."); return {};
    }
#ifdef Q_OS_WIN
    if (!program.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || Platform::windowsCommandLine(program, arguments).isEmpty()) {
        error = QStringLiteral("Choose a Windows .exe and a command line under 32766 characters. Run scripts through their interpreter."); return {};
    }
#endif
    value[QStringLiteral("program")] = program;
    value[QStringLiteral("arguments")] = arguments;
    value[QStringLiteral("workingDirectory")] = directory;
    return value;
}
bool LocalProfileStore::reload() {
    QVariantMap document;
    if (!m_yaml.load(document)) { m_error = m_yaml.error(); emit changed(); return false; }
    if (document.isEmpty()) document = {{QStringLiteral("defaultProfile"), QStringLiteral("system")}, {QStringLiteral("profiles"), QVariantList{}}};
    bool valid = YamlFile::fields(document, {QStringLiteral("version"), QStringLiteral("kind"), QStringLiteral("defaultProfile"), QStringLiteral("profiles")}, m_error);
    if (valid && (document.value(QStringLiteral("defaultProfile")).typeId() != QMetaType::QString
        || !cleanText(document.value(QStringLiteral("defaultProfile")).toString(), 300)
        || document.value(QStringLiteral("defaultProfile")).toString().isEmpty()
        || document.value(QStringLiteral("profiles")).typeId() != QMetaType::QVariantList
        || document.value(QStringLiteral("profiles")).toList().size() > 128)) {
        valid = false; m_error = QStringLiteral("Invalid default profile or profile list (maximum 128).");
    }
    QSet<QString> ids;
    if (valid) for (const auto& item : document.value(QStringLiteral("profiles")).toList()) {
        if (item.typeId() != QMetaType::QVariantMap) {
            valid = false; m_error = QStringLiteral("Each local profile must be a mapping."); break;
        }
        if (!validateProfile(item.toMap(), m_error)) { valid = false; break; }
        const auto id = item.toMap().value(QStringLiteral("id")).toString();
        if (ids.contains(id)) { valid = false; m_error = QStringLiteral("Duplicate local profile id."); break; }
        ids.insert(id);
    }
    if (!valid) { m_yaml.reject(m_error); emit changed(); return false; }
    m_saved = document.value(QStringLiteral("profiles")).toList();
    m_default = document.value(QStringLiteral("defaultProfile")).toString();
    m_error.clear(); emit changed(); return true;
}
bool LocalProfileStore::persist(const QVariantList& profiles, const QString& defaultId) {
    if (profiles.size() > 128) { m_error = QStringLiteral("At most 128 customized profiles are supported."); emit changed(); return false; }
    if (!m_yaml.save({{QStringLiteral("defaultProfile"), defaultId}, {QStringLiteral("profiles"), profiles}})) {
        m_error = m_yaml.error(); emit changed(); return false;
    }
    m_saved = profiles; m_default = defaultId; m_error.clear(); emit changed(); return true;
}
QString LocalProfileStore::saveProfile(const QVariantMap& input) {
    auto value = input;
    value.remove(QStringLiteral("detected")); value.remove(QStringLiteral("customized"));
    if (value.value(QStringLiteral("id")).toString().isEmpty()) value[QStringLiteral("id")] = QStringLiteral("custom:") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    value[QStringLiteral("name")] = value.value(QStringLiteral("name")).toString().trimmed();
    if (!validateProfile(value, m_error) || prepareLaunch(value, m_error).isEmpty()) { emit changed(); return {}; }
    const auto id = value.value(QStringLiteral("id")).toString();
    auto next = m_saved;
    bool found = false;
    for (auto& item : next) if (item.toMap().value(QStringLiteral("id")) == id) { item = value; found = true; break; }
    if (!found) next << value;
    return persist(next, m_default) ? id : QString{};
}
bool LocalProfileStore::removeProfile(const QString& id) {
    auto next = m_saved;
    for (auto it = next.begin(); it != next.end();) {
        if (it->toMap().value(QStringLiteral("id")) == id) it = next.erase(it); else ++it;
    }
    if (next == m_saved) return false;
    return persist(next, id == m_default ? QStringLiteral("system") : m_default);
}
bool LocalProfileStore::setDefaultProfile(const QString& id) {
    if (profile(id).isEmpty()) { m_error = QStringLiteral("Choose an available local profile."); emit changed(); return false; }
    return persist(m_saved, id);
}
}
