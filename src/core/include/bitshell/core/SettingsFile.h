#pragma once
#include <bitshell/core/YamlFile.h>

namespace bitshell::core {
// Flat access for application code; grouped, human-editable YAML on disk.
class SettingsFile {
public:
    explicit SettingsFile(QVariantMap defaults);
    QVariant value(const QString& key, const QVariant& fallback = {}) const;
    bool setValue(const QString& key, const QVariant& value);
    bool replace(const QVariantMap& values);
    bool reload();
    bool importYaml(const QString& source);
    bool exportYaml(const QString& destination);
    QVariantMap values() const { return m_values; }
    QString fileName() const { return m_file.fileName(); }
    QString error() const { return m_error.isEmpty() ? m_file.error() : m_error; }
private:
    bool validate(QVariantMap& values);
    bool flatten(const QVariantMap& document, QVariantMap& values);
    static QVariantMap document(const QVariantMap& values);
    QVariantMap m_defaults;
    QVariantMap m_values;
    YamlFile m_file;
    QString m_error;
};
}
