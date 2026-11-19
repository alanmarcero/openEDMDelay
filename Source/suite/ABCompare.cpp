#include "ABCompare.h"

namespace suite {

void ABCompare::seed() {
    if (!capture) return;
    const auto state = capture();
    slots[0] = state;
    slots[1] = state;
    index = 0;
}

void ABCompare::captureActive() {
    if (capture) slots[index] = capture();
}

void ABCompare::select(int slot) {
    slot = slot == 1 ? 1 : 0;
    if (slot == index) return;
    captureActive();
    index = slot;
    if (restore && slots[index].getSize() > 0) restore(slots[index]);
}

void ABCompare::copyAtoB() {
    captureActive();
    slots[1] = slots[0];
    if (index == 1 && restore && slots[1].getSize() > 0) restore(slots[1]);
}

void ABCompare::copyBtoA() {
    captureActive();
    slots[0] = slots[1];
    if (index == 0 && restore && slots[0].getSize() > 0) restore(slots[0]);
}

void ABCompare::setSlots(juce::MemoryBlock a, juce::MemoryBlock b, int activeSlot) {
    slots[0] = std::move(a);
    slots[1] = std::move(b);
    index = activeSlot == 1 ? 1 : 0;
}

} // namespace suite
