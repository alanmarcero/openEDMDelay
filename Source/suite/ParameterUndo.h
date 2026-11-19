#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
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
    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }

    void setSuspended(bool shouldSuspend) { suspended = shouldSuspend; }
    void resync();

    juce::UndoManager& manager() { return undoManager; }

private:
    void parameterValueChanged(int index, float newValue) override;
    void parameterGestureChanged(int index, bool starting) override;
    bool apply(int index, float normalized);
    bool onMessageThread() const;

    juce::AudioProcessor* processor = nullptr;
    juce::UndoManager undoManager;
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
