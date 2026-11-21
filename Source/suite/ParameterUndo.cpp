#include "ParameterUndo.h"
#include <cmath>

namespace suite {

class ParameterChange : public juce::UndoableAction {
public:
    ParameterChange(ParameterUndo& owner, int index, float before, float after) noexcept
        : owner(owner), index(index), before(before), after(after) {}
    bool perform() override { return owner.apply(index, after); }
    bool undo() override { return owner.apply(index, before); }
    int getSizeInUnits() override { return 1; }
private:
    ParameterUndo& owner;
    int index;
    float before, after;
};

ParameterUndo::~ParameterUndo() {
    detach();
    for (auto& m : managers) m.clearUndoHistory();
}

void ParameterUndo::attach(juce::AudioProcessor& target) {
    detach();
    processor = &target;
    attached = true;
    resync();
    for (auto* parameter : processor->getParameters())
        parameter->addListener(this);
}

void ParameterUndo::detach() {
    if (processor != nullptr) {
        for (auto* parameter : processor->getParameters())
            parameter->removeListener(this);
    }
    processor = nullptr;
    attached = false;
    gestureDepth = 0;
    last.clear();
}

void ParameterUndo::beginGesture() {
    if (!attached || !onMessageThread()) return;
    if (gestureDepth++ == 0)
        managers[static_cast<std::size_t>(activeSlot)].beginNewTransaction();
}

void ParameterUndo::endGesture() {
    if (!attached || !onMessageThread()) return;
    if (gestureDepth > 0) --gestureDepth;
}

bool ParameterUndo::addAction(juce::UndoableAction* action) {
    if (!attached || !onMessageThread() || gestureDepth <= 0 || action == nullptr) {
        delete action;
        return false;
    }
    return managers[static_cast<std::size_t>(activeSlot)].perform(action);
}

bool ParameterUndo::perform(juce::UndoableAction* action) {
    if (!attached || !onMessageThread() || action == nullptr) {
        delete action;
        return false;
    }
    managers[static_cast<std::size_t>(activeSlot)].beginNewTransaction();
    return managers[static_cast<std::size_t>(activeSlot)].perform(action);
}

void ParameterUndo::undo() {
    if (onMessageThread()) managers[static_cast<std::size_t>(activeSlot)].undo();
}

void ParameterUndo::redo() {
    if (onMessageThread()) managers[static_cast<std::size_t>(activeSlot)].redo();
}

void ParameterUndo::clear() {
    if (onMessageThread()) managers[static_cast<std::size_t>(activeSlot)].clearUndoHistory();
}

void ParameterUndo::selectSlot(int slot) {
    activeSlot = (slot == 1) ? 1 : 0;
    resync();
}

void ParameterUndo::clearSlot(int slot) {
    if (onMessageThread()) managers[slot == 1 ? 1u : 0u].clearUndoHistory();
}

void ParameterUndo::resync() {
    if (processor == nullptr || !onMessageThread()) return;
    const auto parameters = processor->getParameters();
    last.resize(static_cast<std::size_t>(parameters.size()));
    for (int i = 0; i < parameters.size(); ++i)
        last[static_cast<std::size_t>(i)] = parameters[i]->getValue();
}

void ParameterUndo::parameterValueChanged(int index, float newValue) {
    if (!attached || !onMessageThread() || applying || suspended) return;
    if (index < 0 || index >= static_cast<int>(last.size())) return;
    const float previous = last[static_cast<std::size_t>(index)];
    last[static_cast<std::size_t>(index)] = newValue;
    if (gestureDepth <= 0 || std::abs(previous - newValue) < 1.0e-6f) return;
    managers[static_cast<std::size_t>(activeSlot)].perform(new ParameterChange(*this, index, previous, newValue));
    if (onUserEdit) onUserEdit();
}

void ParameterUndo::parameterGestureChanged(int, bool starting) {
    if (starting) beginGesture();
    else endGesture();
}

bool ParameterUndo::apply(int index, float normalized) {
    if (processor == nullptr) return false;
    const auto parameters = processor->getParameters();
    if (index < 0 || index >= parameters.size()) return false;
    const juce::ScopedValueSetter<bool> guard(applying, true);
    if (index < static_cast<int>(last.size()))
        last[static_cast<std::size_t>(index)] = normalized;
    parameters[index]->setValueNotifyingHost(normalized);
    return true;
}

bool ParameterUndo::onMessageThread() const {
    auto* manager = juce::MessageManager::getInstanceWithoutCreating();
    return manager != nullptr && manager->isThisTheMessageThread();
}

} // namespace suite
