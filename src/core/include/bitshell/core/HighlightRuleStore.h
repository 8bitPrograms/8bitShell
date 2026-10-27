#pragma once

#include <QAbstractListModel>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace bitshell::core {

class HighlightRuleStore : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY rulesChanged)
    Q_PROPERTY(QString storagePath READ storagePath CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PatternRole,
        ForegroundRole,
        BackgroundRole,
        BoldRole,
        UnderlineRole,
        EnabledRole,
        CaseSensitiveRole,
        PriorityRole,
    };
    Q_ENUM(Role)

    explicit HighlightRuleStore(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index,
                                int role = Qt::DisplayRole) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const;
    [[nodiscard]] QVariantList rules() const;
    [[nodiscard]] QString storagePath() const;
    [[nodiscard]] QString lastError() const;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int addRule(const QVariantMap& values);
    Q_INVOKABLE bool updateRule(int row, const QVariantMap& values);
    Q_INVOKABLE bool setRuleEnabled(int row, bool enabled);
    Q_INVOKABLE bool removeRule(int row);
    Q_INVOKABLE int duplicateRule(int row);
    Q_INVOKABLE bool resetDefaults();
    Q_INVOKABLE bool reload();

signals:
    void countChanged();
    void rulesChanged();
    void lastErrorChanged();

private:
    struct Rule {
        QString id;
        QString name;
        QString pattern;
        QString foreground;
        QString background;
        bool bold = false;
        bool underline = false;
        bool enabled = true;
        bool caseSensitive = false;
        int priority = 0;
    };

    void loadDefaultRules();
    void setLastError(const QString& error);
    [[nodiscard]] bool save();
    [[nodiscard]] bool isValidRow(int row) const;
    [[nodiscard]] static bool validate(const Rule& rule, QString* error);
    [[nodiscard]] static Rule fromMap(const QVariantMap& values,
                                      const Rule* existing = nullptr);
    [[nodiscard]] static QVariantMap toMap(const Rule& rule);

    QVector<Rule> m_rules;
    QString m_storagePath;
    QString m_lastError;
};

} // namespace bitshell::core
