#include <bitshell/core/ConversationEngine.h>
#include <bitshell/core/BoundedRegex.h>

#include <utility>

namespace bitshell::core {

ConversationEngine::ConversationEngine(QList<ConversationStep> steps)
    : m_steps(std::move(steps)) {}

void ConversationEngine::setSteps(QList<ConversationStep> steps) {
    m_steps = std::move(steps);
    reset();
}

void ConversationEngine::reset() {
    m_buffer.clear();
    m_index = 0;
}

std::optional<ConversationAction> ConversationEngine::feed(QStringView output) {
    if (isComplete()) {
        return std::nullopt;
    }

    m_buffer += output.last(std::min(output.size(), qsizetype(MaxBufferedCharacters)));
    if (m_buffer.size() > MaxBufferedCharacters) {
        m_buffer.remove(0, m_buffer.size() - MaxBufferedCharacters);
    }

    const auto& step = m_steps.at(m_index);
    if (!step.expect.isValid()) {
        return std::nullopt;
    }

    const auto expression = boundedRegex(step.expect.pattern(), step.expect.patternOptions());
    if (!expression.isValid()) return std::nullopt;
    const auto match = expression.match(m_buffer);
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    ConversationAction action{
        .stepId = step.id,
        .type = step.action,
        .value = step.value,
        .choices = step.choices,
        .sensitive = step.sensitive || step.action == ConversationActionType::AskSecret,
        .timeoutMs = step.timeoutMs,
    };

    m_buffer.remove(0, match.capturedEnd());
    ++m_index;
    return action;
}

bool ConversationEngine::isComplete() const {
    return m_index >= m_steps.size();
}

qsizetype ConversationEngine::currentStepIndex() const {
    return m_index;
}

QString ConversationEngine::currentStepId() const {
    return isComplete() ? QString{} : m_steps.at(m_index).id;
}

} // namespace bitshell::core
