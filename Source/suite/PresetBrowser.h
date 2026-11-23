#pragma once

#include "PresetStore.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace suite {

struct PresetHandlers {
    std::function<int()> count;
    std::function<juce::String(int)> name;
    std::function<void(int)> loadFactory;
    std::function<juce::MemoryBlock()> capture;
    std::function<void(const juce::MemoryBlock&)> loadUser;
    std::function<void(const juce::String&)> onSaved;
};

// Searchable list of factory programs and user presets. Factory rows cannot be renamed or deleted.
class PresetBrowser : public juce::Component, private juce::Timer {
public:
    PresetBrowser(juce::File pluginRoot, PresetHandlers handlers);
    ~PresetBrowser() override { stopTimer(); }
    void resized() override;

    void setSearch(const juce::String& text);
    void setListVisible(bool visible);
    void saveCurrent();
    bool renameSelected();
    bool deleteSelected();
    void toggleFavorite();
    void selectFactory(int index);
    void stepPreset(int delta);
    void syncSelection(const juce::String& presetName, bool isModified);
    juce::String selectedDisplayName() const;
    juce::StringArray visibleNames() const;
    bool selectionIsFactory() const;
    PresetStore& store() { return presets; }

private:
    struct Row {
        juce::String name;
        juce::String key;
        bool factory = false;
        bool favorite = false;
        int factoryIndex = -1;
        juce::File file;
    };

    void timerCallback() override;
    void clearConfirmations();
    void handleSaveClick();
    void handleDeleteClick();
    void updateActionStates();
    void rebuild();
    void choose(int id);
    const Row* current() const;
    void keepSelection(int id);

    PresetStore presets;
    PresetHandlers handlers;
    juce::TextEditor search;
    juce::ComboBox list;
    juce::TextButton favorite{"Fav"}, save{"Save"}, rename{"Rename"}, remove{"Delete"};
    std::vector<Row> shown;
    juce::String selectedKey;
    bool listVisible = true;
    bool rebuilding = false;
    bool confirmingReplace = false;
    bool confirmingDelete = false;
};

} // namespace suite
