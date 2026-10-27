#include <bitshell/core/PersonalProfile.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/ConnectionRouting.h>
#include <bitshell/core/PrivateFile.h>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>

namespace bitshell::core {
QString PersonalProfile::path() {
    return QDir(Platform::appConfigDirectory()).filePath(QStringLiteral("personal.yaml"));
}
PersonalProfile::PersonalProfile(QObject* parent) : QObject(parent), m_file(path(), QStringLiteral("8bitshell/personal")) { (void)reload(); }
QString PersonalProfile::avatarPath() const { return QDir(QFileInfo(path()).absolutePath()).filePath(QStringLiteral("avatar.png")); }
QString PersonalProfile::displayName() const {
    const auto value = m_data.value(QStringLiteral("displayName")).toString().trimmed();
    return value.isEmpty() ? qEnvironmentVariable("USER", QStringLiteral("User")) : value;
}
QString PersonalProfile::employeeId() const {
    const auto value = m_data.value(QStringLiteral("employeeId")).toString();
    return value.isEmpty() ? m_data.value(QStringLiteral("username")).toString() : value;
}
QString PersonalProfile::initial() const {
    const auto name = displayName();
    return name.left(1).toUpper();
}
QUrl PersonalProfile::avatarUrl() const {
    if (m_avatarDigest.isEmpty()) return {};
    auto url = QUrl::fromLocalFile(avatarPath()); url.setFragment(m_avatarDigest); return url;
}
bool PersonalProfile::validate(const QVariantMap& data, QString& error) {
    if (!YamlFile::fields(data, {QStringLiteral("version"), QStringLiteral("kind"), QStringLiteral("displayName"),
                                QStringLiteral("employeeId"), QStringLiteral("username"),
                                QStringLiteral("cyberArkEnabled"), QStringLiteral("avatarEnabled"),
                                QStringLiteral("approvedRoutes")}, error)) return false;
    const auto display = data.value(QStringLiteral("displayName")).toString();
    const auto employee = data.contains(QStringLiteral("employeeId"))
        ? data.value(QStringLiteral("employeeId")).toString()
        : data.value(QStringLiteral("username")).toString();
    const bool invalidDisplay = display.size() > 128
        || std::any_of(display.cbegin(), display.cend(), [](QChar ch) { return ch.unicode() < 32 || ch.unicode() == 127; });
    if ((data.contains(QStringLiteral("displayName")) && data.value(QStringLiteral("displayName")).typeId() != QMetaType::QString)
        || (data.contains(QStringLiteral("employeeId")) && data.value(QStringLiteral("employeeId")).typeId() != QMetaType::QString)
        || (data.contains(QStringLiteral("username")) && data.value(QStringLiteral("username")).typeId() != QMetaType::QString)
        || !ConnectionRouting::validIdentity(employee) || invalidDisplay
        || (data.contains(QStringLiteral("cyberArkEnabled")) && data.value(QStringLiteral("cyberArkEnabled")).typeId() != QMetaType::Bool)
        || (data.contains(QStringLiteral("avatarEnabled")) && data.value(QStringLiteral("avatarEnabled")).typeId() != QMetaType::Bool)) {
        error = QStringLiteral("Invalid profile identity or avatar preference."); return false;
    }
    const auto approvals = data.value(QStringLiteral("approvedRoutes"), QStringList{});
    if ((approvals.typeId() != QMetaType::QStringList && approvals.typeId() != QMetaType::QVariantList) || approvals.toList().size() > 256) {
        error = QStringLiteral("At most 256 local route approvals are allowed."); return false;
    }
    static const QRegularExpression hash(QStringLiteral("\\A[0-9a-f]{64}\\z"));
    for (const auto& item : approvals.toList()) if (item.typeId() != QMetaType::QString || !hash.match(item.toString()).hasMatch()) {
        error = QStringLiteral("Invalid local route fingerprint."); return false;
    }
    return true;
}
bool PersonalProfile::reload() {
    QVariantMap data;
    if (!m_file.load(data) || !validate(data, m_error)) {
        if (m_error.isEmpty()) m_error = m_file.error();
        m_file.reject(m_error); m_data.clear(); emit changed(); return false;
    }
    const bool hadEmployeeId = data.contains(QStringLiteral("employeeId"));
    const bool hadLegacyUsername = data.contains(QStringLiteral("username"));
    if (!data.contains(QStringLiteral("displayName")))
        data[QStringLiteral("displayName")] = qEnvironmentVariable("USER", QStringLiteral("User"));
    if (!hadEmployeeId)
        data[QStringLiteral("employeeId")] = data.value(QStringLiteral("username")).toString();
    if (!data.contains(QStringLiteral("cyberArkEnabled")))
        data[QStringLiteral("cyberArkEnabled")] = hadLegacyUsername && !data.value(QStringLiteral("username")).toString().isEmpty();
    m_data = data; m_error.clear(); m_avatarDigest.clear();
    QByteArray image; QString ignored;
    if (QFileInfo::exists(avatarPath()) && PrivateFile::read(avatarPath(), image, ignored, 1024 * 1024))
        m_avatarDigest = QString::fromLatin1(QCryptographicHash::hash(image, QCryptographicHash::Sha256).toHex());
    emit changed(); return true;
}
bool PersonalProfile::save(QVariantMap data) {
    // New files use the explicit employeeId name. Keep accepting the old
    // username field on read, but do not perpetuate duplicate identity fields.
    if (data.contains(QStringLiteral("employeeId")))
        data.remove(QStringLiteral("username"));
    if (!validate(data, m_error) || !m_file.save(data)) {
        if (m_error.isEmpty()) m_error = m_file.error();
        emit changed(); return false;
    }
    m_data = std::move(data); m_error.clear(); emit changed(); return true;
}
bool PersonalProfile::setUsername(const QString& value) {
    auto data = m_data;
    const auto normalized = value.trimmed();
    data[QStringLiteral("employeeId")] = normalized;
    if (!normalized.isEmpty()) data[QStringLiteral("cyberArkEnabled")] = true;
    return save(data);
}
bool PersonalProfile::setProfile(const QString& display, const QString& employee, bool enabled) {
    auto data = m_data;
    data[QStringLiteral("displayName")] = display.trimmed();
    data[QStringLiteral("employeeId")] = employee.trimmed();
    data[QStringLiteral("cyberArkEnabled")] = enabled;
    return save(data);
}
bool PersonalProfile::setDisplayName(const QString& value) {
    auto data = m_data; data[QStringLiteral("displayName")] = value.trimmed(); return save(data);
}
bool PersonalProfile::setEmployeeId(const QString& value) {
    auto data = m_data;
    data[QStringLiteral("employeeId")] = value.trimmed();
    return save(data);
}
void PersonalProfile::setCyberArkEnabled(bool enabled) {
    auto data = m_data; data[QStringLiteral("cyberArkEnabled")] = enabled; (void)save(data);
}
void PersonalProfile::setAvatarEnabled(bool enabled) {
    auto data = m_data; data[QStringLiteral("avatarEnabled")] = enabled; (void)save(data);
}
bool PersonalProfile::chooseAvatar(const QUrl& source) {
    QByteArray bytes;
    if (!source.isLocalFile() || !m_file.writable()) { m_error = QStringLiteral("Choose a local PNG/JPEG and reload invalid personal settings first."); emit changed(); return false; }
    if (!PrivateFile::read(source.toLocalFile(), bytes, m_error, 8 * 1024 * 1024)) { emit changed(); return false; }
    QBuffer input(&bytes); input.open(QIODevice::ReadOnly);
    QImageReader reader(&input); reader.setAutoTransform(true);
    const auto format = reader.format(); const auto size = reader.size();
    if ((format != "png" && format != "jpeg") || !size.isValid() || size.width() > 4096 || size.height() > 4096) {
        m_error = QStringLiteral("Use a PNG/JPEG up to 8 MiB and 4096 × 4096 pixels. SVG, animations and remote URLs are not accepted."); emit changed(); return false;
    }
    const auto image = reader.read();
    if (image.isNull()) { m_error = QStringLiteral("Cannot decode this profile picture."); emit changed(); return false; }
    // Re-render pixels into a fresh small image, dropping source metadata.
    auto scaled = image.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage clean(scaled.size(), QImage::Format_ARGB32); clean.fill(Qt::transparent);
    for (int row = 0; row < clean.height(); ++row) for (int col = 0; col < clean.width(); ++col) clean.setPixelColor(col, row, scaled.pixelColor(col, row));
    QByteArray png; QBuffer output(&png); output.open(QIODevice::WriteOnly);
    if (!clean.save(&output, "PNG") || !PrivateFile::write(avatarPath(), png, m_error)) { emit changed(); return false; }
    m_avatarDigest = QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex());
    // Selecting a file does not silently turn the opt-in preference on.
    m_error.clear(); emit changed(); return true;
}
bool PersonalProfile::removeAvatar() {
    auto data = m_data; data[QStringLiteral("avatarEnabled")] = false;
    if (!save(data)) return false;
    const auto file = avatarPath();
    if (QFileInfo::exists(file) && (!QFileInfo(file).isFile() || QFileInfo(file).isSymLink() || !QFile::remove(file))) {
        m_error = QStringLiteral("Cannot remove the app-owned profile picture."); emit changed(); return false;
    }
    m_avatarDigest.clear(); emit changed(); return true;
}
bool PersonalProfile::approveRoute(const QString& fingerprint) {
    auto data = m_data; auto approved = data.value(QStringLiteral("approvedRoutes")).toStringList();
    if (!approved.contains(fingerprint)) approved << fingerprint;
    data[QStringLiteral("approvedRoutes")] = approved; return save(data);
}
bool PersonalProfile::clearRouteApprovals() { auto data = m_data; data[QStringLiteral("approvedRoutes")] = QStringList{}; return save(data); }
bool PersonalProfile::routeApproved(const QString& fingerprint) const { return m_error.isEmpty() && m_data.value(QStringLiteral("approvedRoutes")).toStringList().contains(fingerprint); }
bool PersonalProfile::isRouteApproved(const QString& fingerprint) {
    YamlFile file(path(), QStringLiteral("8bitshell/personal")); QVariantMap data; QString error;
    return file.load(data) && validate(data, error) && data.value(QStringLiteral("approvedRoutes")).toStringList().contains(fingerprint);
}
}
