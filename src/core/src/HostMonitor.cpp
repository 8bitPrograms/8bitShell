#include <bitshell/core/HostMonitor.h>
#include <bitshell/core/SshSecurity.h>
#include <bitshell/core/PasswordBroker.h>
#include <bitshell/core/PasswordStore.h>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <cwchar>
#include <vector>
#endif

namespace bitshell::core {
HostMonitor::HostMonitor(QObject* parent) : QObject(parent) {
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] { consume(m_process.readAllStandardOutput()); });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_error = (m_error + m_process.readAllStandardError()).right(4096);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this] {
        m_passwordBroker.reset();
        m_process.setProcessEnvironment(SshSecurity::environment());
        m_timeout.stop();
        if (!m_stopping) { m_status = m_process.errorString(); emit changed(); }
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        m_passwordBroker.reset();
        m_process.setProcessEnvironment(SshSecurity::environment());
        m_timeout.stop();
        if (!m_stopping && m_enabled) {
            m_metrics.clear();
            m_status = QStringLiteral("Monitoring stopped. %1").arg(QString::fromUtf8(m_error).trimmed().left(220));
            emit changed();
        }
    });
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        stop(); m_metrics.clear(); m_status = QStringLiteral("No monitoring data. Check SSH authentication and Linux /proc."); emit changed();
    });
#ifdef Q_OS_WIN
    m_localTimer.setInterval(5000);
    connect(&m_localTimer, &QTimer::timeout, this, &HostMonitor::sampleLocalWindows);
#endif
}
HostMonitor::~HostMonitor() { stop(); }
void HostMonitor::setEnabled(bool value) {
    if (m_enabled == value) return;
    m_enabled = value;
    if (value) start(); else { stop(); m_metrics.clear(); m_status.clear(); }
    emit changed();
}
void HostMonitor::setProfile(const QVariantMap& value) {
    if (m_profile == value) return;
    m_profile = value;
    if (m_enabled) start();
    emit changed();
}
void HostMonitor::stop() {
    m_stopping = true;
    m_passwordBroker.reset();
    m_process.setProcessEnvironment(SshSecurity::environment());
    m_timeout.stop();
#ifdef Q_OS_WIN
    m_localTimer.stop();
#endif
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(500); }
    m_stopping = false;
}
QByteArray HostMonitor::sampleScript() {
    return QByteArrayLiteral(R"monitor(LC_ALL=C
PATH=/usr/bin:/bin
export LC_ALL PATH
if [ ! -r /proc/stat ] || [ ! -r /proc/meminfo ]; then
  printf 'Linux /proc monitoring is unavailable\n' >&2
  exit 1
fi
while :; do
  printf '__BIT8SHELL_MONITOR_BEGIN__\n'
  printf 'host '; hostname
  awk '/^cpu / {total=0; for(i=2;i<=9;i++) total+=$i; printf "cpu %.0f %.0f\n",total,$5+$6; exit}' /proc/stat
  awk '/^MemTotal:/ {t=$2} /^MemAvailable:/ {a=$2} END {printf "memory %.0f %.0f\n",t,a}' /proc/meminfo
  awk '{print "uptime " $1}' /proc/uptime
  awk '{print "load " $1 " " $2 " " $3}' /proc/loadavg
  awk 'NR>2 {sub(/^[[:space:]]*/, ""); split($0, interface, ":"); if(interface[1]!="lo") {sub(/^[^:]*:[[:space:]]*/, ""); rx+=$1; tx+=$9}} END {printf "net %.0f %.0f\n",rx,tx}' /proc/net/dev
  df -Pk / 2>/dev/null | awk 'NR==2 {print "disk " $5 " " $4}'
  df -Pk 2>/dev/null | awk '
    NR>1 && $2 ~ /^[0-9]+$/ && $2>0 {
      mount=$6; for(i=7;i<=NF;i++) mount=mount " " $i
      if(mount !~ /^\// || mount ~ /^\/(dev|proc|sys|run)(\/|$)/ || mount ~ /^\/snap(\/|$)/) next
      used=$2-$4; gsub(/%/, "%25", mount); gsub(/ /, "%20", mount)
      printf "diskfs %s %.0f %.0f %s\n", mount, used, $2, $5
    }' | sort -k3,3nr | head -n 4
  printf '__BIT8SHELL_MONITOR_END__\n'
  sleep 5
done
)monitor");
}
void HostMonitor::start() {
    stop(); m_buffer.clear(); m_frame.clear(); m_error.clear(); m_metrics.clear(); m_lastTotal = -1;
#ifdef Q_OS_WIN
    m_lastNetworkAvailable = false;
#endif
    if (m_profile.value(QStringLiteral("unsupported")).toBool()) {
        m_status = QStringLiteral("Save this SSH connection as a profile to enable remote monitoring.");
        return;
    }
    m_status = QStringLiteral("Connecting monitor…");
    const QString host = m_profile.value(QStringLiteral("host")).toString();
#ifdef Q_OS_WIN
    if (host.isEmpty()) {
        sampleLocalWindows();
        if (m_enabled) m_localTimer.start();
        return;
    }
#endif
    QString program = QStringLiteral("/bin/sh");
    QStringList args{QStringLiteral("-s")};
    auto environment = SshSecurity::environment();
    if (!host.isEmpty()) {
        const auto command = SshSecurity::command(m_profile, SshSecurity::Transport::Monitor);
        if (!command) { m_status = command.error(); return; }
        program = m_testTransport.isEmpty() ? command->program : m_testTransport;
        args = command->arguments;
        args << QStringLiteral("/bin/sh -s");
        if (m_profile.value(QStringLiteral("authenticationMode")).toString() == QStringLiteral("password")) {
            auto password = PasswordStore::read(m_profile);
            if (!password) { m_status = password.error(); return; }
            if (password->isEmpty()) {
                m_status = QStringLiteral("Save a password on this device in the SSH session before enabling password-authenticated monitoring.");
                return;
            }
            m_passwordBroker = std::make_unique<PasswordBroker>();
            const auto additions = m_passwordBroker->start(m_profile, *password);
            password->fill('\0');
            if (!additions) { m_passwordBroker.reset(); m_status = additions.error(); return; }
            for (const auto& key : additions->keys()) environment.insert(key, additions->value(key));
        }
    }
#ifdef Q_OS_WIN
    if (!m_testTransport.isEmpty()) {
        const auto path = QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH"));
        if (!path.isEmpty()) environment.insert(QStringLiteral("PATH"), path);
    }
#endif
    // One persistent SSH process streams samples. Authentication failure leaves
    // monitoring stopped until the user explicitly retries; no password loop.
    m_process.setProcessEnvironment(environment);
    m_process.start(program, args);
    m_process.write(sampleScript());
    m_process.closeWriteChannel();
    m_timeout.start(15000);
}
#ifdef Q_OS_WIN
void HostMonitor::sampleLocalWindows() {
    if (!m_enabled) return;

    FILETIME idleTime{}, kernelTime{}, userTime{};
    MEMORYSTATUSEX memory{};
    memory.dwLength = sizeof(memory);
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime) || !GlobalMemoryStatusEx(&memory)) {
        m_metrics.clear();
        m_status = QStringLiteral("Cannot read local Windows performance counters.");
        emit changed();
        return;
    }
    const auto ticks = [](const FILETIME& value) {
        ULARGE_INTEGER result{};
        result.LowPart = value.dwLowDateTime;
        result.HighPart = value.dwHighDateTime;
        return double(result.QuadPart);
    };
    const double total = ticks(kernelTime) + ticks(userTime);
    const double idle = ticks(idleTime);
    const double uptime = double(GetTickCount64()) / 1000.0;

    double rx = 0, tx = 0;
    MIB_IF_TABLE2* interfaces = nullptr;
    const bool networkAvailable = GetIfTable2(&interfaces) == NO_ERROR && interfaces;
    if (networkAvailable) {
        for (ULONG index = 0; index < interfaces->NumEntries; ++index) {
            const auto& row = interfaces->Table[index];
            if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK || row.OperStatus != IfOperStatusUp) continue;
            rx += double(row.InOctets);
            tx += double(row.OutOctets);
        }
    }
    if (interfaces) FreeMibTable(interfaces);

    struct DriveSample { double total = 0; QVariantMap values; };
    std::vector<DriveSample> drives;
    const DWORD required = GetLogicalDriveStringsW(0, nullptr);
    if (required) {
        std::vector<wchar_t> roots(std::size_t(required) + 1U, L'\0');
        if (GetLogicalDriveStringsW(DWORD(roots.size()), roots.data())) {
            for (const wchar_t* root = roots.data(); *root; root += std::wcslen(root) + 1) {
                if (GetDriveTypeW(root) != DRIVE_FIXED) continue;
                ULARGE_INTEGER available{}, capacity{}, free{};
                if (!GetDiskFreeSpaceExW(root, &available, &capacity, &free) || !capacity.QuadPart) continue;
                const double totalGB = double(capacity.QuadPart) / 1024.0 / 1024.0 / 1024.0;
                const double usedGB = double(capacity.QuadPart - free.QuadPart) / 1024.0 / 1024.0 / 1024.0;
                drives.push_back({totalGB, QVariantMap{
                    {QStringLiteral("mount"), QString::fromWCharArray(root)},
                    {QStringLiteral("usedGB"), usedGB},
                    {QStringLiteral("totalGB"), totalGB},
                    {QStringLiteral("percent"), std::clamp(100.0 * usedGB / totalGB, 0.0, 100.0)}}});
            }
        }
    }
    std::sort(drives.begin(), drives.end(), [](const DriveSample& left, const DriveSample& right) {
        return left.total > right.total;
    });
    QVariantList disks;
    for (std::size_t index = 0; index < drives.size() && index < 4; ++index) disks << drives[index].values;

    wchar_t hostBuffer[MAX_COMPUTERNAME_LENGTH + 1]{};
    DWORD hostLength = MAX_COMPUTERNAME_LENGTH + 1;
    const QString host = GetComputerNameW(hostBuffer, &hostLength)
        ? QString::fromWCharArray(hostBuffer, int(hostLength)) : QStringLiteral("Windows");
    QVariantMap parsed{
        {QStringLiteral("host"), host},
        {QStringLiteral("uptime"), uptime},
        {QStringLiteral("memoryPercent"), double(memory.dwMemoryLoad)},
        {QStringLiteral("memoryTotalMB"), double(memory.ullTotalPhys) / 1024.0 / 1024.0},
        {QStringLiteral("disks"), disks}};
    parsed[QStringLiteral("cpuPercent")] = m_lastTotal >= 0 && total > m_lastTotal
        ? std::clamp(100.0 * (1.0 - (idle - m_lastIdle) / (total - m_lastTotal)), 0.0, 100.0) : -1.0;
    const double elapsed = uptime - m_lastUptime;
    parsed[QStringLiteral("rxKB")] = m_lastNetworkAvailable && networkAvailable && elapsed > 0
        ? std::max(0.0, (rx - m_lastRx) / elapsed / 1024.0) : 0.0;
    parsed[QStringLiteral("txKB")] = m_lastNetworkAvailable && networkAvailable && elapsed > 0
        ? std::max(0.0, (tx - m_lastTx) / elapsed / 1024.0) : 0.0;
    m_lastTotal = total;
    m_lastIdle = idle;
    m_lastUptime = uptime;
    if (networkAvailable) {
        m_lastRx = rx;
        m_lastTx = tx;
    }
    m_lastNetworkAvailable = networkAvailable;
    m_metrics = parsed;
    m_status = QStringLiteral("Live · local Windows · every 5 seconds");
    emit changed();
}
#endif
void HostMonitor::consume(const QByteArray& bytes) {
    m_buffer += bytes;
    if (m_buffer.size() > 65536) { m_buffer.clear(); return; }
    while (m_buffer.contains('\n')) {
        const auto end = m_buffer.indexOf('\n');
        const auto line = m_buffer.left(end).trimmed();
        m_buffer.remove(0, end + 1);
        if (line == "__BIT8SHELL_MONITOR_BEGIN__") m_frame.clear();
        else if (line == "__BIT8SHELL_MONITOR_END__") finishSample();
        else if (m_frame.size() < 32768) m_frame += line + '\n';
    }
}
void HostMonitor::finishSample() {
    QVariantMap parsed;
    QVariantList disks;
    double total = -1, idle = 0, uptime = 0, rx = 0, tx = 0;
    for (const auto& line : QString::fromUtf8(m_frame).split(u'\n')) {
        const auto fields = line.split(u' ', Qt::SkipEmptyParts);
        if (fields.size() < 2) continue;
        const QString key = fields[0];
        if (key == QStringLiteral("host")) parsed[key] = fields[1];
        else if (key == QStringLiteral("load")) parsed[key] = fields.mid(1).join(u' ');
        else if (key == QStringLiteral("uptime")) { uptime = fields[1].toDouble(); parsed[key] = uptime; }
        else if (key == QStringLiteral("cpu") && fields.size() >= 3) { total = fields[1].toDouble(); idle = fields[2].toDouble(); }
        else if (key == QStringLiteral("net") && fields.size() >= 3) { rx = fields[1].toDouble(); tx = fields[2].toDouble(); }
        else if (key == QStringLiteral("memory") && fields.size() >= 3) {
            const double kb = fields[1].toDouble();
            parsed[QStringLiteral("memoryPercent")] = kb > 0 ? std::clamp(100 * (kb - fields[2].toDouble()) / kb, 0.0, 100.0) : 0;
            parsed[QStringLiteral("memoryTotalMB")] = kb / 1024;
        } else if (key == QStringLiteral("disk")) parsed[key] = fields[1];
        else if (key == QStringLiteral("diskfs") && fields.size() >= 5) {
            bool usedOk = false, totalOk = false;
            const double usedKb = fields[2].toDouble(&usedOk);
            const double totalKb = fields[3].toDouble(&totalOk);
            QString percent = fields[4]; percent.remove(u'%');
            if (usedOk && totalOk && totalKb > 0 && disks.size() < 4) {
                disks << QVariantMap{
                    {QStringLiteral("mount"), QUrl::fromPercentEncoding(fields[1].toUtf8())},
                    {QStringLiteral("usedGB"), usedKb / 1024.0 / 1024.0},
                    {QStringLiteral("totalGB"), totalKb / 1024.0 / 1024.0},
                    {QStringLiteral("percent"), percent.toDouble()}};
            }
        }
    }
    if (total < 0 || !parsed.contains(QStringLiteral("memoryPercent"))) return;
    parsed[QStringLiteral("cpuPercent")] = m_lastTotal >= 0 && total > m_lastTotal
        ? std::clamp(100 * (1 - (idle - m_lastIdle) / (total - m_lastTotal)), 0.0, 100.0) : -1;
    const double dt = uptime - m_lastUptime;
    parsed[QStringLiteral("rxKB")] = m_lastTotal >= 0 && dt > 0 ? std::max(0.0, (rx - m_lastRx) / dt / 1024) : 0;
    parsed[QStringLiteral("txKB")] = m_lastTotal >= 0 && dt > 0 ? std::max(0.0, (tx - m_lastTx) / dt / 1024) : 0;
    parsed[QStringLiteral("disks")] = disks;
    m_lastTotal = total; m_lastIdle = idle; m_lastUptime = uptime; m_lastRx = rx; m_lastTx = tx;
    m_metrics = parsed; m_status = QStringLiteral("Live · every 5 seconds");
    m_timeout.start(20000); emit changed();
}
}
