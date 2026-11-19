#pragma once

#include <juce_core/juce_core.h>
#include <functional>

namespace suite {

// Two full-state slots. The active slot tracks the sound you hear.
class ABCompare {
public:
    std::function<juce::MemoryBlock()> capture;
    std::function<void(const juce::MemoryBlock&)> restore;

    void seed();
    void captureActive();
    void select(int slot);
    void copyAtoB();
    void copyBtoA();
    int active() const { return index; }
    const juce::MemoryBlock& slot(int which) const { return slots[which == 1 ? 1 : 0]; }
    void setSlots(juce::MemoryBlock a, juce::MemoryBlock b, int activeSlot);

private:
    juce::MemoryBlock slots[2];
    int index = 0;
};

} // namespace suite
