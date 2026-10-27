#pragma once

#include <optional>

#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringView>

namespace bitshell::core {

enum class ConversationActionType {
    Ask,
    AskSecret,
    SendLiteral,
    Choose,
    Notify,
};

struct ConversationStep {
    QString id;
    QRegularExpression expect;
    ConversationActionType action = ConversationActionType::Ask;
    QString value;
    QStringList choices;
    int timeoutMs = 30'000;
    bool sensitive = false;
};

struct ConversationAction {
    QString stepId;
    ConversationActionType type = ConversationActionType::Ask;
    QString value;
    QStringList choices;
    bool sensitive = false;
    int timeoutMs = 30'000;
};

class ConversationEngine {
public:
    explicit ConversationEngine(QList<ConversationStep> steps = {});

    void setSteps(QList<ConversationStep> steps);
    void reset();

    [[nodiscard]] std::optional<ConversationAction> feed(QStringView output);
    [[nodiscard]] bool isComplete() const;
    [[nodiscard]] qsizetype currentStepIndex() const;
    [[nodiscard]] QString currentStepId() const;

private:
    static constexpr qsizetype MaxBufferedCharacters = 16 * 1024;

    QList<ConversationStep> m_steps;
    QString m_buffer;
    qsizetype m_index = 0;
};

} // namespace bitshell::core

