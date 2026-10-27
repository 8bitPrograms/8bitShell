#include <bitshell/core/HighlightRuleStore.h>
#include <bitshell/core/Platform.h>
#include <bitshell/core/PrivateFile.h>
#include <bitshell/core/BoundedRegex.h>

#include <bitshell/core/HighlightEngine.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

namespace bitshell::core {

HighlightRuleStore::HighlightRuleStore(QObject* parent)
    : QAbstractListModel(parent) {
    const QString configRoot = Platform::appConfigDirectory();
    m_storagePath = QDir(configRoot).filePath(QStringLiteral("highlights.json"));
    const QString legacyPath =
        QDir(Platform::iniSettingsDirectory())
            .filePath(QStringLiteral("highlights.json"));
    if (!QFileInfo::exists(m_storagePath) && QFileInfo::exists(legacyPath)) {
        QByteArray bytes;
        QString error;
        if (!PrivateFile::read(legacyPath, bytes, error, 1024 * 1024)
            || !PrivateFile::write(m_storagePath, bytes, error)) {
            loadDefaultRules();
            setLastError(error);
            return;
        }
    }
    loadDefaultRules();
    (void)reload();
}

int HighlightRuleStore::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(m_rules.size());
}

QVariant HighlightRuleStore::data(const QModelIndex& index, int role) const {
    if (!isValidRow(index.row())) {
        return {};
    }
    const auto& rule = m_rules.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return rule.name;
    case IdRole:
        return rule.id;
    case PatternRole:
        return rule.pattern;
    case ForegroundRole:
        return rule.foreground;
    case BackgroundRole:
        return rule.background;
    case BoldRole:
        return rule.bold;
    case UnderlineRole:
        return rule.underline;
    case EnabledRole:
        return rule.enabled;
    case CaseSensitiveRole:
        return rule.caseSensitive;
    case PriorityRole:
        return rule.priority;
    default:
        return {};
    }
}

QHash<int, QByteArray> HighlightRuleStore::roleNames() const {
    return {
        {IdRole, QByteArrayLiteral("ruleId")},
        {NameRole, QByteArrayLiteral("name")},
        {PatternRole, QByteArrayLiteral("pattern")},
        {ForegroundRole, QByteArrayLiteral("foreground")},
        {BackgroundRole, QByteArrayLiteral("background")},
        {BoldRole, QByteArrayLiteral("bold")},
        {UnderlineRole, QByteArrayLiteral("underline")},
        {EnabledRole, QByteArrayLiteral("enabledRule")},
        {CaseSensitiveRole, QByteArrayLiteral("caseSensitive")},
        {PriorityRole, QByteArrayLiteral("priority")},
    };
}

int HighlightRuleStore::count() const {
    return static_cast<int>(m_rules.size());
}

QVariantList HighlightRuleStore::rules() const {
    QVariantList result;
    result.reserve(m_rules.size());
    for (const auto& rule : m_rules) {
        result.push_back(toMap(rule));
    }
    return result;
}

QString HighlightRuleStore::storagePath() const {
    return m_storagePath;
}

QString HighlightRuleStore::lastError() const {
    return m_lastError;
}

QVariantMap HighlightRuleStore::get(int row) const {
    return isValidRow(row) ? toMap(m_rules.at(row)) : QVariantMap{};
}

int HighlightRuleStore::addRule(const QVariantMap& values) {
    Rule rule = fromMap(values);
    QString error;
    if (!validate(rule, &error)) {
        setLastError(error);
        return -1;
    }

    const int row = static_cast<int>(m_rules.size());
    beginInsertRows({}, row, row);
    m_rules.push_back(std::move(rule));
    endInsertRows();
    if (!save()) {
        beginRemoveRows({}, row, row);
        m_rules.removeLast();
        endRemoveRows();
        return -1;
    }
    setLastError({});
    emit countChanged();
    emit rulesChanged();
    return row;
}

bool HighlightRuleStore::updateRule(int row, const QVariantMap& values) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Highlight rule does not exist"));
        return false;
    }
    Rule updated = fromMap(values, &m_rules.at(row));
    QString error;
    if (!validate(updated, &error)) {
        setLastError(error);
        return false;
    }

    const Rule previous = m_rules.at(row);
    m_rules[row] = std::move(updated);
    emit dataChanged(index(row), index(row));
    if (!save()) {
        m_rules[row] = previous;
        emit dataChanged(index(row), index(row));
        return false;
    }
    setLastError({});
    emit rulesChanged();
    return true;
}

bool HighlightRuleStore::setRuleEnabled(int row, bool enabled) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Highlight rule does not exist"));
        return false;
    }
    if (m_rules.at(row).enabled == enabled) {
        return true;
    }
    return updateRule(row, QVariantMap{{QStringLiteral("enabled"), enabled}});
}

bool HighlightRuleStore::removeRule(int row) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Highlight rule does not exist"));
        return false;
    }
    const Rule removed = m_rules.at(row);
    beginRemoveRows({}, row, row);
    m_rules.removeAt(row);
    endRemoveRows();
    if (!save()) {
        beginInsertRows({}, row, row);
        m_rules.insert(row, removed);
        endInsertRows();
        return false;
    }
    setLastError({});
    emit countChanged();
    emit rulesChanged();
    return true;
}

int HighlightRuleStore::duplicateRule(int row) {
    if (!isValidRow(row)) {
        setLastError(QStringLiteral("Highlight rule does not exist"));
        return -1;
    }
    QVariantMap copy = toMap(m_rules.at(row));
    copy.remove(QStringLiteral("id"));
    copy[QStringLiteral("name")] =
        QStringLiteral("%1 copy").arg(copy.value(QStringLiteral("name")).toString());
    copy[QStringLiteral("priority")] =
        copy.value(QStringLiteral("priority")).toInt() - 1;
    return addRule(copy);
}

bool HighlightRuleStore::resetDefaults() {
    const QVector<Rule> previous = m_rules;
    beginResetModel();
    loadDefaultRules();
    endResetModel();
    if (!save()) {
        beginResetModel();
        m_rules = previous;
        endResetModel();
        return false;
    }
    setLastError({});
    emit countChanged();
    emit rulesChanged();
    return true;
}

bool HighlightRuleStore::reload() {
    QFile file(m_storagePath);
    if (!file.exists()) {
        setLastError({});
        return true;
    }
    QByteArray bytes;
    QString readError;
    if (!PrivateFile::read(m_storagePath, bytes, readError, 1024 * 1024)) {
        setLastError(readError);
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(QStringLiteral("Could not read highlight rules: %1")
                         .arg(parseError.errorString()));
        return false;
    }

    const QJsonArray profiles =
        document.object().value(QStringLiteral("profiles")).toArray();
    if (profiles.isEmpty() || !profiles.at(0).isObject()) {
        setLastError(QStringLiteral("Highlight catalog has no profile"));
        return false;
    }

    QVector<Rule> loaded;
    const QJsonArray jsonRules =
        profiles.at(0).toObject().value(QStringLiteral("rules")).toArray();
    if (jsonRules.size() > 64) { setLastError(QStringLiteral("At most 64 highlight rules are supported.")); return false; }
    loaded.reserve(jsonRules.size());
    for (const auto& value : jsonRules) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QJsonObject style = object.value(QStringLiteral("style")).toObject();
        QVariantMap values{
            {QStringLiteral("id"), object.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), object.value(QStringLiteral("name")).toString()},
            {QStringLiteral("pattern"), object.value(QStringLiteral("pattern")).toString()},
            {QStringLiteral("foreground"),
             style.value(QStringLiteral("foreground")).toString()},
            {QStringLiteral("background"),
             style.value(QStringLiteral("background")).toString()},
            {QStringLiteral("bold"), style.value(QStringLiteral("bold")).toBool()},
            {QStringLiteral("underline"),
             style.value(QStringLiteral("underline")).toBool()},
            {QStringLiteral("enabled"),
             object.value(QStringLiteral("enabled")).toBool(true)},
            {QStringLiteral("caseSensitive"),
             object.value(QStringLiteral("case_sensitive")).toBool(false)},
            {QStringLiteral("priority"),
             object.value(QStringLiteral("priority")).toInt()},
        };
        Rule rule = fromMap(values);
        QString error;
        if (!validate(rule, &error)) {
            setLastError(QStringLiteral("Invalid saved highlight rule: %1").arg(error));
            return false;
        }
        loaded.push_back(std::move(rule));
    }

    beginResetModel();
    m_rules = std::move(loaded);
    endResetModel();
    setLastError({});
    emit countChanged();
    emit rulesChanged();
    return true;
}

void HighlightRuleStore::loadDefaultRules() {
    m_rules.clear();
    const auto defaults = HighlightEngine::defaultRules();
    m_rules.reserve(defaults.size());
    for (const auto& source : defaults) {
        const bool caseSensitive =
            !source.pattern.patternOptions().testFlag(
                QRegularExpression::CaseInsensitiveOption);
        m_rules.push_back(Rule{
            .id = source.id,
            .name = source.displayName,
            .pattern = source.pattern.pattern(),
            .foreground = source.foreground.name(QColor::HexRgb),
            .background = source.background.alpha() == 0
                              ? QString{}
                              : source.background.name(QColor::HexRgb),
            .bold = source.bold,
            .underline = source.underline,
            .enabled = source.enabled,
            .caseSensitive = caseSensitive,
            .priority = source.priority,
        });
    }
}

void HighlightRuleStore::setLastError(const QString& error) {
    if (m_lastError == error) {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

bool HighlightRuleStore::save() {
    if (m_rules.size() > 64) { setLastError(QStringLiteral("At most 64 highlight rules are supported.")); return false; }
    QJsonArray jsonRules;
    for (const auto& rule : m_rules) {
        QJsonObject style{
            {QStringLiteral("foreground"), rule.foreground},
            {QStringLiteral("bold"), rule.bold},
            {QStringLiteral("underline"), rule.underline},
        };
        if (!rule.background.isEmpty()) {
            style.insert(QStringLiteral("background"), rule.background);
        }
        jsonRules.push_back(QJsonObject{
            {QStringLiteral("id"), rule.id},
            {QStringLiteral("name"), rule.name},
            {QStringLiteral("enabled"), rule.enabled},
            {QStringLiteral("pattern"), rule.pattern},
            {QStringLiteral("case_sensitive"), rule.caseSensitive},
            {QStringLiteral("priority"), rule.priority},
            {QStringLiteral("style"), style},
        });
    }
    const QJsonObject root{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("profiles"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QStringLiteral("default")},
             {QStringLiteral("name"), QStringLiteral("Default")},
             {QStringLiteral("rules"), jsonRules},
         }}},
    };

    QString error;
    if (!PrivateFile::write(m_storagePath, QJsonDocument(root).toJson(QJsonDocument::Indented), error)) {
        setLastError(error);
        return false;
    }
    return true;
}

bool HighlightRuleStore::isValidRow(int row) const {
    return row >= 0 && row < static_cast<int>(m_rules.size());
}

bool HighlightRuleStore::validate(const Rule& rule, QString* error) {
    if (rule.pattern.trimmed().isEmpty()) {
        if (error != nullptr) {
            *error = QStringLiteral("A regular expression is required");
        }
        return false;
    }
    QRegularExpression::PatternOptions options =
        QRegularExpression::UseUnicodePropertiesOption;
    if (!rule.caseSensitive) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    const QRegularExpression expression = boundedRegex(rule.pattern, options);
    if (!expression.isValid()) {
        if (error != nullptr) {
            *error = QStringLiteral("Invalid regular expression: %1")
                         .arg(expression.errorString());
        }
        return false;
    }
    if (!QColor::isValidColorName(rule.foreground)) {
        if (error != nullptr) {
            *error = QStringLiteral("Foreground must be a valid color");
        }
        return false;
    }
    if (!rule.background.isEmpty() && !QColor::isValidColorName(rule.background)) {
        if (error != nullptr) {
            *error = QStringLiteral("Background must be empty or a valid color");
        }
        return false;
    }
    return true;
}

HighlightRuleStore::Rule HighlightRuleStore::fromMap(const QVariantMap& values,
                                                      const Rule* existing) {
    Rule rule = existing == nullptr ? Rule{} : *existing;
    const auto stringValue = [&values](const QString& key, const QString& fallback) {
        return values.contains(key) ? values.value(key).toString().trimmed() : fallback;
    };
    rule.id = stringValue(QStringLiteral("id"), rule.id);
    if (rule.id.isEmpty()) {
        rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    rule.name = stringValue(QStringLiteral("name"), rule.name);
    if (rule.name.isEmpty()) {
        rule.name = QStringLiteral("Custom rule");
    }
    rule.pattern = stringValue(QStringLiteral("pattern"), rule.pattern);
    rule.foreground = stringValue(QStringLiteral("foreground"), rule.foreground);
    if (rule.foreground.isEmpty()) {
        rule.foreground = QStringLiteral("#e8eef7");
    }
    rule.background = stringValue(QStringLiteral("background"), rule.background);
    if (values.contains(QStringLiteral("bold"))) {
        rule.bold = values.value(QStringLiteral("bold")).toBool();
    }
    if (values.contains(QStringLiteral("underline"))) {
        rule.underline = values.value(QStringLiteral("underline")).toBool();
    }
    if (values.contains(QStringLiteral("enabled"))) {
        rule.enabled = values.value(QStringLiteral("enabled")).toBool();
    }
    if (values.contains(QStringLiteral("caseSensitive"))) {
        rule.caseSensitive = values.value(QStringLiteral("caseSensitive")).toBool();
    }
    if (values.contains(QStringLiteral("priority"))) {
        rule.priority = std::clamp(values.value(QStringLiteral("priority")).toInt(),
                                   -10000,
                                   10000);
    }
    return rule;
}

QVariantMap HighlightRuleStore::toMap(const Rule& rule) {
    return {
        {QStringLiteral("id"), rule.id},
        {QStringLiteral("name"), rule.name},
        {QStringLiteral("pattern"), rule.pattern},
        {QStringLiteral("foreground"), rule.foreground},
        {QStringLiteral("background"), rule.background},
        {QStringLiteral("bold"), rule.bold},
        {QStringLiteral("underline"), rule.underline},
        {QStringLiteral("enabled"), rule.enabled},
        {QStringLiteral("caseSensitive"), rule.caseSensitive},
        {QStringLiteral("priority"), rule.priority},
    };
}

} // namespace bitshell::core
