#include "PresetStore.h"
#include <algorithm>

namespace suite {

PresetStore::PresetStore(juce::File root) : pluginRoot(std::move(root)) {}

juce::File PresetStore::defaultRoot(const juce::String& pluginName) {
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Alan Marcero")
        .getChildFile(pluginName);
}

juce::String PresetStore::factoryKey(const juce::String& programName) { return "factory:" + programName; }
juce::String PresetStore::userKey(const juce::String& filename) { return "user:" + filename; }

bool PresetStore::matches(const juce::String& name, const juce::String& query) {
    return query.trim().isEmpty() || name.containsIgnoreCase(query.trim());
}

juce::String PresetStore::safeStem(const juce::String& displayName) {
    auto stem = displayName.retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 .-+'_");
    stem = stem.trim().substring(0, 60);
    return stem.isEmpty() ? juce::String("preset") : stem;
}

std::vector<PresetStore::Entry> PresetStore::list() const {
    std::vector<Entry> entries;
    if (!presetsDirectory().isDirectory()) return entries;
    for (const auto& file : presetsDirectory().findChildFiles(juce::File::findFiles, false, "*.oedpreset"))
        if (file.getSize() > 0) entries.push_back({file.getFileNameWithoutExtension(), file});
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.name.compareIgnoreCase(b.name) < 0;
    });
    return entries;
}

bool PresetStore::save(const juce::String& displayName, const juce::MemoryBlock& state, juce::String& error) {
    auto name = displayName.trim().substring(0, 80);
    if (name.isEmpty()) { error = "Name is empty"; return false; }
    if (state.getSize() == 0) { error = "Preset is empty"; return false; }
    if (!presetsDirectory().createDirectory().wasOk() && !presetsDirectory().isDirectory()) {
        error = "Could not create the preset folder";
        return false;
    }
    const auto target = presetsDirectory().getChildFile(safeStem(name) + ".oedpreset");
    if (!target.replaceWithData(state.getData(), state.getSize())) { error = "Could not write the preset"; return false; }
    return true;
}

bool PresetStore::rename(const juce::File& file, const juce::String& displayName, juce::String& error) {
    auto name = displayName.trim().substring(0, 80);
    if (name.isEmpty()) { error = "Name is empty"; return false; }
    if (!file.existsAsFile()) { error = "Preset is missing"; return false; }
    const auto dest = presetsDirectory().getChildFile(safeStem(name) + ".oedpreset");
    if (dest == file) return true;
    if (dest.existsAsFile()) { error = "That name is already used"; return false; }
    const bool favorite = isFavorite(userKey(file.getFileName()));
    if (!file.moveFileTo(dest)) { error = "Could not rename the preset"; return false; }
    if (favorite) {
        setFavorite(userKey(file.getFileName()), false);
        setFavorite(userKey(dest.getFileName()), true);
    }
    return true;
}

bool PresetStore::remove(const juce::File& file) {
    setFavorite(userKey(file.getFileName()), false);
    return file.deleteFile();
}

bool PresetStore::load(const juce::File& file, juce::MemoryBlock& state) const {
    return file.existsAsFile() && file.loadFileAsData(state) && state.getSize() > 0;
}

juce::StringArray PresetStore::readFavorites() const {
    return juce::StringArray::fromLines(favoritesFile().loadFileAsString());
}

void PresetStore::writeFavorites(const juce::StringArray& keys) const {
    pluginRoot.createDirectory();
    favoritesFile().replaceWithText(keys.joinIntoString("\n"));
}

bool PresetStore::isFavorite(const juce::String& key) const {
    return key.isNotEmpty() && readFavorites().contains(key);
}

void PresetStore::setFavorite(const juce::String& key, bool shouldFavorite) {
    if (key.isEmpty()) return;
    auto keys = readFavorites();
    keys.removeEmptyStrings();
    keys.removeString(key);
    if (shouldFavorite) keys.add(key);
    writeFavorites(keys);
}

} // namespace suite
