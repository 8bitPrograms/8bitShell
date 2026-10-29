#include <bitshell/core/SettingsFile.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <QDir>
#include <QFileInfo>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QTime>
#include <QSet>
#include <QTemporaryFile>
#include <cmath>

namespace bitshell::core {
namespace {
// An older profile may already use a newly introduced default shortcut. Keep
// explicitly stored bindings and leave only the colliding implicit default
// unbound. Imports and interactive edits still reject all duplicate bindings.
void preserveExplicitShortcuts(QVariantMap& values, const QVariantMap& explicitValues) {
    QSet<QString> occupied;
    for (auto it = explicitValues.cbegin(); it != explicitValues.cend(); ++it) {
        if (!it.key().startsWith(QStringLiteral("shortcuts/")) || it.value().toString().isEmpty()) continue;
        occupied.insert(QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText)
                            .toString(QKeySequence::PortableText));
    }
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!it.key().startsWith(QStringLiteral("shortcuts/")) || explicitValues.contains(it.key())
            || it.value().toString().isEmpty()) continue;
        const auto normalized = QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText)
                                    .toString(QKeySequence::PortableText);
        if (occupied.contains(normalized)) it.value() = QString{};
        else occupied.insert(normalized);
    }
}
}
SettingsFile::SettingsFile(QVariantMap defaults)
    : m_defaults(std::move(defaults)), m_values(m_defaults)
    , m_file(QDir(Platform::appConfigDirectory())
                 .filePath(QStringLiteral("settings.yaml")), QStringLiteral("8bitshell/settings")) {
    (void)reload();
}
QVariant SettingsFile::value(const QString& key, const QVariant& fallback) const { return m_values.value(key, fallback); }
QVariantMap SettingsFile::document(const QVariantMap& values) {
    QVariantMap root;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        const auto parts = it.key().split(u'/');
        auto group = root.value(parts[0]).toMap();
        group[parts[1]] = it.value();
        root[parts[0]] = group;
    }
    return root;
}
bool SettingsFile::flatten(const QVariantMap& root, QVariantMap& values, bool restoringDefaults) {
    QVariantMap explicitValues;
    for (auto it = root.cbegin(); it != root.cend(); ++it) {
        if (it.key() == QStringLiteral("version") || it.key() == QStringLiteral("kind")) continue;
        if (it.value().typeId() != QMetaType::QVariantMap || !document(m_defaults).contains(it.key())) {
            m_error = QStringLiteral("Unknown settings section or non-mapping value: %1").arg(it.key()); return false;
        }
        const auto group = it.value().toMap();
        for (auto field = group.cbegin(); field != group.cend(); ++field) {
            const auto key = it.key() + u'/' + field.key();
            values[key] = field.value();
            explicitValues[key] = field.value();
        }
    }
    if (restoringDefaults) preserveExplicitShortcuts(values, explicitValues);
    return validate(values);
}
bool SettingsFile::validate(QVariantMap& values) {
    m_error.clear();
    const auto invalid = [this](const QString& key, const QString& reason) {
        m_error = QStringLiteral("Invalid setting %1: %2. Nothing was changed.").arg(key, reason); return false;
    };
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!m_defaults.contains(it.key())) return invalid(it.key(), QStringLiteral("unknown field"));
        const int expected = m_defaults.value(it.key()).typeId(), actual = it.value().typeId();
        if (expected == QMetaType::Bool && actual != QMetaType::Bool) return invalid(it.key(), QStringLiteral("use true or false"));
        if (expected == QMetaType::QString && actual != QMetaType::QString) return invalid(it.key(), QStringLiteral("expected text"));
        if (expected == QMetaType::Int || expected == QMetaType::Double) {
            if ((actual != QMetaType::Int && actual != QMetaType::LongLong && actual != QMetaType::Double)
                || !std::isfinite(it.value().toDouble())) return invalid(it.key(), QStringLiteral("expected a number"));
            if (expected == QMetaType::Int && it.value().toDouble() != it.value().toInt()) return invalid(it.key(), QStringLiteral("expected an integer"));
        }
        if (expected == QMetaType::QStringList) {
            if (actual != QMetaType::QStringList && actual != QMetaType::QVariantList) return invalid(it.key(), QStringLiteral("expected a list of strings"));
            for (const auto& entry : it.value().toList())
                if (entry.typeId() != QMetaType::QString) return invalid(it.key(), QStringLiteral("expected strings"));
        }
    }
    const auto range = [&](const QString& key, double min, double max) {
        return !values.contains(key) || (values[key].toDouble() >= min && values[key].toDouble() <= max)
            || invalid(key, QStringLiteral("out of range %1–%2").arg(min).arg(max));
    };
    if (!range(QStringLiteral("terminal/fontSize"), 7, 40)
        || !range(QStringLiteral("terminal/scrollbackLines"), 100, 100000)
        || !range(QStringLiteral("logging/rotationMegabytes"), 0, 4096)
        || !range(QStringLiteral("logging/retainedFiles"), 0, 50)) return false;
    for (const auto& key : {QStringLiteral("appearance/dayStart"), QStringLiteral("appearance/nightStart")}) {
        if (values.contains(key) && !QTime::fromString(values[key].toString(), QStringLiteral("HH:mm")).isValid())
            return invalid(key, QStringLiteral("expected HH:mm"));
    }
    if (values.contains(QStringLiteral("appearance/mode"))
        && !QStringList{QStringLiteral("dark"), QStringLiteral("light"), QStringLiteral("system"), QStringLiteral("schedule")}.contains(values[QStringLiteral("appearance/mode")].toString()))
        return invalid(QStringLiteral("appearance/mode"), QStringLiteral("use dark, light, system or schedule"));
    if (values.contains(QStringLiteral("terminal/backspaceMode"))
        && !QStringList{QStringLiteral("bs"), QStringLiteral("del")}.contains(values[QStringLiteral("terminal/backspaceMode")].toString()))
        return invalid(QStringLiteral("terminal/backspaceMode"), QStringLiteral("use bs or del"));
    QSet<QString> shortcuts;
    if (!QStringList{QStringLiteral("system"), QStringLiteral("grayscale"), QStringLiteral("aliased")}
             .contains(values.value(QStringLiteral("terminal/textAntialiasing")).toString()))
        return invalid(QStringLiteral("terminal/textAntialiasing"), QStringLiteral("use system, grayscale or aliased"));
    if (!QStringList{QStringLiteral("default"), QStringLiteral("none"), QStringLiteral("vertical"), QStringLiteral("full")}
             .contains(values.value(QStringLiteral("terminal/fontHinting")).toString()))
        return invalid(QStringLiteral("terminal/fontHinting"), QStringLiteral("use default, none, vertical or full"));
    for (auto it = values.begin(); it != values.end(); ++it) {
        if (!it.key().startsWith(QStringLiteral("shortcuts/")) || it.value().toString().isEmpty()) continue;
        const auto key = QKeySequence::fromString(it.value().toString(), QKeySequence::PortableText);
        const auto normalized = key.toString(QKeySequence::PortableText);
        if (key.count() != 1 || key[0].key() == Qt::Key_unknown || shortcuts.contains(normalized))
            return invalid(it.key(), QStringLiteral("invalid or duplicate shortcut"));
        it.value() = normalized;
        shortcuts.insert(normalized);
    }
    return true;
}
bool SettingsFile::replace(const QVariantMap& values) {
    auto next = values;
    if (!validate(next) || !m_file.save(document(next))) return false;
    m_values = next;
    return true;
}
bool SettingsFile::setValue(const QString& key, const QVariant& value) {
    auto next = m_values;
    next[key] = value;
    return replace(next);
}
bool SettingsFile::reload() {
    m_error.clear();
    QVariantMap root;
    if (!m_file.load(root)) return false;
    const bool migrate = !QFileInfo::exists(m_file.fileName());
    auto next = m_defaults;
    if (migrate) {
        const auto ini = QDir(QFileInfo(m_file.fileName()).absolutePath()).filePath(QStringLiteral("settings.ini"));
        const QString legacyPath = QFileInfo::exists(ini) || QFileInfo(ini).isSymLink() ? ini
            : QSettings().fileName();
        QByteArray legacyBytes;
        QString readError;
        if ((QFileInfo::exists(legacyPath) || QFileInfo(legacyPath).isSymLink())
            && !PrivateFile::read(legacyPath, legacyBytes, readError)) {
            m_file.reject(QStringLiteral("Cannot safely migrate legacy settings: %1").arg(readError)); return false;
        }
        // Parse a bounded, private snapshot, not a potentially replaced path.
        QTemporaryFile snapshot;
        if (!snapshot.open() || snapshot.write(legacyBytes) != legacyBytes.size() || !snapshot.flush()) {
            m_file.reject(QStringLiteral("Cannot create the private settings migration snapshot.")); return false;
        }
        QSettings legacy(snapshot.fileName(), QSettings::IniFormat);
        if (legacy.status() != QSettings::NoError) {
            m_file.reject(QStringLiteral("Cannot migrate settings.ini; original file preserved.")); return false;
        }
        QVariantMap explicitValues;
        for (const auto& key : legacy.allKeys()) {
            if (!m_defaults.contains(key)) { m_file.reject(QStringLiteral("Unknown legacy setting: %1. Original file preserved.").arg(key)); return false; }
            auto value = legacy.value(key);
            if (!value.convert(m_defaults.value(key).metaType())) {
                m_file.reject(QStringLiteral("Cannot migrate setting: %1. Original file preserved.").arg(key)); return false;
            }
            next[key] = value;
            explicitValues[key] = value;
        }
        preserveExplicitShortcuts(next, explicitValues);
        if (!validate(next)) { m_file.reject(m_error); return false; }
        if (!m_file.save(document(next))) return false;
    } else if (!flatten(root, next, true)) { m_file.reject(m_error); return false; }
    m_values = next;
    return true;
}
bool SettingsFile::importYaml(const QString& source) {
    m_error.clear();
    QVariantMap root;
    if (!m_file.importFile(source, root)) return false;
    auto next = m_values;
    if (!flatten(root, next)) return false;
    return replace(next);
}
bool SettingsFile::exportYaml(const QString& destination) {
    m_error.clear();
    return m_file.exportFile(destination, document(m_values));
}
}
