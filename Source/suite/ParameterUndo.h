#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>
#include <vector>

namespace suite {

// Records message-thread parameter edits in a JUCE UndoManager.
// Slider and button gestures become one step. Host automation is ignored.
class ParameterUndo : private juce::AudioProcessorParameter::Listener {
public:
    ParameterUndo() = default;
    ~ParameterUndo() override;

    void attach(juce::AudioProcessor& processor);
    void detach();

    void beginGesture();
    void endGesture();
    bool addAction(juce::UndoableAction* action);
    bool perform(juce::UndoableAction* action);

    void undo();
    void redo();
    void clear();
    void selectSlot(int slot);
    void clearSlot(int slot);
    bool canUndo() const { return managers[static_cast<std::size_t>(activeSlot)].canUndo(); }
    bool canRedo() const { return managers[static_cast<std::size_t>(activeSlot)].canRedo(); }

    void setSuspended(bool shouldSuspend) { suspended = shouldSuspend; }
    void resync();

    std::function<void()> onUserEdit;

    juce::UndoManager& manager() { return managers[static_cast<std::size_t>(activeSlot)]; }

private:
    void parameterValueChanged(int index, float newValue) override;
    void parameterGestureChanged(int index, bool starting) override;
    bool apply(int index, float normalized);
    bool onMessageThread() const;

    juce::AudioProcessor* processor = nullptr;
    std::array<juce::UndoManager, 2> managers;
    int activeSlot = 0;
    std::vector<float> last;
    int gestureDepth = 0;
    bool attached = false;
    bool applying = false;
    bool suspended = false;

    friend class ParameterChange;
};

class SuspendUndo {
public:
    explicit SuspendUndo(ParameterUndo& target) : undo(target) { undo.setSuspended(true); }
    ~SuspendUndo() { undo.setSuspended(false); undo.resync(); }
private:
    ParameterUndo& undo;
};

} // namespace suite
