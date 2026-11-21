#include "PresetBrowser.h"

namespace suite {

PresetBrowser::PresetBrowser(juce::File pluginRoot, PresetHandlers handlersIn)
    : presets(std::move(pluginRoot)), handlers(std::move(handlersIn)) {
    search.setComponentID("presetSearch");
    search.setTitle("Search presets");
    search.setTextToShowWhenEmpty("Search", juce::Colours::grey);
    search.setTooltip("Find a factory or user preset by name.");
    search.onTextChange = [this] { rebuild(); };
    addAndMakeVisible(search);

    list.setComponentID("presetList");
    list.setTitle("Preset list");
    list.setTextWhenNothingSelected("Presets");
    list.setTooltip("Factory presets and your saved presets. Factory presets stay read-only.");
    list.onChange = [this] { choose(list.getSelectedId()); };
    addAndMakeVisible(list);

    favorite.setComponentID("presetFavorite");
    favorite.setTitle("Favorite");
    favorite.setClickingTogglesState(true);
    favorite.setTooltip("Mark the selected preset as a favorite.");
    favorite.onClick = [this] { toggleFavorite(); };
    addAndMakeVisible(favorite);

    save.setComponentID("presetSave");
    save.setTitle("Save preset");
    save.setTooltip("Save the current sound as a user preset. The search field is the name.");
    save.onClick = [this] { handleSaveClick(); };
    addAndMakeVisible(save);

    rename.setComponentID("presetRename");
    rename.setTitle("Rename preset");
    rename.setTooltip("Rename the selected user preset. Factory presets stay read-only.");
    rename.onClick = [this] { renameSelected(); };
    addAndMakeVisible(rename);

    remove.setComponentID("presetDelete");
    remove.setTitle("Delete preset");
    remove.setTooltip("Delete the selected user preset. Factory presets stay read-only.");
    remove.onClick = [this] { handleDeleteClick(); };
    addAndMakeVisible(remove);

    rebuild();
}

void PresetBrowser::resized() {
    auto row = getLocalBounds();
    search.setBounds(row.removeFromLeft(140).reduced(0, 0));
    row.removeFromLeft(4);
    favorite.setBounds(row.removeFromRight(44));
    row.removeFromRight(4);
    remove.setBounds(row.removeFromRight(64));
    row.removeFromRight(4);
    rename.setBounds(row.removeFromRight(68));
    row.removeFromRight(4);
    save.setBounds(row.removeFromRight(52));
    row.removeFromRight(4);
    list.setBounds(row);
}

void PresetBrowser::setSearch(const juce::String& text) {
    search.setText(text, juce::dontSendNotification);
    rebuild();
}

void PresetBrowser::rebuild() {
    const juce::ScopedValueSetter<bool> guard(rebuilding, true);
    shown.clear();
    list.clear(juce::dontSendNotification);
    const auto query = search.getText();
    const auto push = [&](bool favoritesOnly) {
        if (handlers.count && handlers.name) {
            const int count = handlers.count();
            for (int i = 0; i < count; ++i) {
                const auto name = handlers.name(i);
                const auto key = PresetStore::factoryKey(name);
                const bool favorite = presets.isFavorite(key);
                if (favorite != favoritesOnly || !PresetStore::matches(name, query)) continue;
                shown.push_back({name, key, true, favorite, i, {}});
            }
        }
        for (const auto& entry : presets.list()) {
            const auto key = PresetStore::userKey(entry.file.getFileName());
            const bool favorite = presets.isFavorite(key);
            if (favorite != favoritesOnly || !PresetStore::matches(entry.name, query)) continue;
            shown.push_back({entry.name, key, false, favorite, -1, entry.file});
        }
    };
    push(true);
    push(false);
    int selectedId = 0;
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        const auto label = (shown[static_cast<std::size_t>(i)].favorite ? juce::String::fromUTF8("\xe2\x98\x85 ") : juce::String())
            + shown[static_cast<std::size_t>(i)].name;
        list.addItem(label, i + 1);
        if (shown[static_cast<std::size_t>(i)].key == selectedKey) selectedId = i + 1;
    }
    list.setSelectedId(selectedId, juce::dontSendNotification);
    updateActionStates();
}

void PresetBrowser::choose(int id) {
    if (rebuilding) return;
    clearConfirmations();
    if (id <= 0 || id > static_cast<int>(shown.size())) return;
    const auto& row = shown[static_cast<std::size_t>(id - 1)];
    selectedKey = row.key;
    updateActionStates();
    if (row.factory) {
        if (handlers.loadFactory) handlers.loadFactory(row.factoryIndex);
    } else if (handlers.loadUser) {
        juce::MemoryBlock state;
        if (presets.load(row.file, state)) handlers.loadUser(state);
    }
}

const PresetBrowser::Row* PresetBrowser::current() const {
    const int id = list.getSelectedId();
    if (id <= 0 || id > static_cast<int>(shown.size())) return nullptr;
    return &shown[static_cast<std::size_t>(id - 1)];
}

void PresetBrowser::timerCallback() {
    clearConfirmations();
}

void PresetBrowser::clearConfirmations() {
    stopTimer();
    confirmingReplace = false;
    confirmingDelete = false;
    save.setButtonText("Save");
    remove.setButtonText("Delete");
}

void PresetBrowser::updateActionStates() {
    const auto* row = current();
    favorite.setToggleState(row != nullptr && row->favorite, juce::dontSendNotification);
    const bool userSelected = row != nullptr && !row->factory;
    rename.setEnabled(userSelected);
    remove.setEnabled(userSelected);
}

void PresetBrowser::handleSaveClick() {
    auto name = search.getText().trim();
    if (name.isEmpty()) name = "User preset";
    bool exists = false;
    for (const auto& entry : presets.list())
        if (entry.name.equalsIgnoreCase(name)) { exists = true; break; }
    if (exists && !confirmingReplace) {
        clearConfirmations();
        confirmingReplace = true;
        save.setButtonText("Replace?");
        startTimer(3000);
        return;
    }
    clearConfirmations();
    saveCurrent();
}

void PresetBrowser::handleDeleteClick() {
    const auto* row = current();
    if (row == nullptr || row->factory) return;
    if (!confirmingDelete) {
        clearConfirmations();
        confirmingDelete = true;
        remove.setButtonText("Delete?");
        startTimer(3000);
        return;
    }
    clearConfirmations();
    deleteSelected();
}

void PresetBrowser::saveCurrent() {
    if (!handlers.capture) return;
    auto name = search.getText().trim();
    if (name.isEmpty()) name = "User preset";
    juce::String error;
    if (!presets.save(name, handlers.capture(), error)) return;
    if (handlers.onSaved) handlers.onSaved(name);
    for (const auto& entry : presets.list())
        if (entry.name.equalsIgnoreCase(name)) selectedKey = PresetStore::userKey(entry.file.getFileName());
    search.setText(name, juce::dontSendNotification);
    rebuild();
}

bool PresetBrowser::renameSelected() {
    const auto* row = current();
    juce::File file;
    if (row != nullptr) {
        if (row->factory) return false;
        file = row->file;
    } else {
        for (const auto& entry : presets.list())
            if (PresetStore::userKey(entry.file.getFileName()) == selectedKey) file = entry.file;
    }
    if (!file.existsAsFile()) return false;
    const auto name = search.getText().trim();
    if (name.isEmpty()) return false;
    juce::String error;
    if (!presets.rename(file, name, error)) return false;
    for (const auto& entry : presets.list())
        if (entry.name.equalsIgnoreCase(name))
            selectedKey = PresetStore::userKey(entry.file.getFileName());
    rebuild();
    return true;
}

bool PresetBrowser::deleteSelected() {
    const auto* row = current();
    if (row == nullptr || row->factory) return false;
    const auto file = row->file;
    if (!presets.remove(file)) return false;
    selectedKey.clear();
    rebuild();
    return true;
}

void PresetBrowser::toggleFavorite() {
    const auto* row = current();
    if (row == nullptr) {
        favorite.setToggleState(false, juce::dontSendNotification);
        return;
    }
    const bool next = !presets.isFavorite(row->key);
    presets.setFavorite(row->key, next);
    rebuild();
}

void PresetBrowser::selectFactory(int index) {
    clearConfirmations();
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        if (shown[static_cast<std::size_t>(i)].factory && shown[static_cast<std::size_t>(i)].factoryIndex == index) {
            list.setSelectedId(i + 1, juce::sendNotificationSync);
            return;
        }
    }
}

void PresetBrowser::stepPreset(int delta) {
    if (shown.empty()) return;
    clearConfirmations();
    int currentIdx = list.getSelectedId() - 1;
    if (currentIdx < 0 || currentIdx >= static_cast<int>(shown.size())) {
        currentIdx = -1;
        for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
            if (shown[static_cast<std::size_t>(i)].key == selectedKey) { currentIdx = i; break; }
        }
    }
    const int count = static_cast<int>(shown.size());
    const int nextIdx = currentIdx < 0
        ? (delta >= 0 ? 0 : count - 1)
        : (currentIdx + delta % count + count) % count;
    list.setSelectedId(nextIdx + 1, juce::sendNotificationSync);
}

void PresetBrowser::syncSelection(const juce::String& presetName, bool isModified) {
    const juce::ScopedValueSetter<bool> guard(rebuilding, true);
    int matchedId = 0;
    for (int i = 0; i < static_cast<int>(shown.size()); ++i) {
        if (shown[static_cast<std::size_t>(i)].name == presetName) {
            selectedKey = shown[static_cast<std::size_t>(i)].key;
            matchedId = isModified ? 0 : (i + 1);
            break;
        }
    }
    list.setTextWhenNothingSelected(presetName.isNotEmpty() ? (presetName + (isModified ? " *" : "")) : juce::String("Presets"));
    list.setSelectedId(matchedId, juce::dontSendNotification);
    updateActionStates();
}

juce::String PresetBrowser::selectedDisplayName() const {
    if (const auto* row = current()) return row->name;
    return {};
}

juce::StringArray PresetBrowser::visibleNames() const {
    juce::StringArray names;
    for (const auto& row : shown) names.add(row.name);
    return names;
}

bool PresetBrowser::selectionIsFactory() const {
    const auto* row = current();
    return row != nullptr && row->factory;
}

} // namespace suite
