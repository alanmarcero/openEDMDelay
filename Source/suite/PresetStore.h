#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace suite {

// User presets live in ~/Documents/Alan Marcero/<plugin>/Presets.
// Factory programs are not files and are never written here.
class PresetStore {
public:
    struct Entry {
        juce::String name;
        juce::File file;
    };

    explicit PresetStore(juce::File pluginRoot);

    static juce::File defaultRoot(const juce::String& pluginName);
    juce::File root() const { return pluginRoot; }
    juce::File presetsDirectory() const { return pluginRoot.getChildFile("Presets"); }

    std::vector<Entry> list() const;
    bool save(const juce::String& displayName, const juce::MemoryBlock& state, juce::String& error);
    bool rename(const juce::File& file, const juce::String& displayName, juce::String& error);
    bool remove(const juce::File& file);
    bool load(const juce::File& file, juce::MemoryBlock& state) const;

    static juce::String factoryKey(const juce::String& programName);
    static juce::String userKey(const juce::String& filename);
    bool isFavorite(const juce::String& key) const;
    void setFavorite(const juce::String& key, bool shouldFavorite);
    static bool matches(const juce::String& name, const juce::String& query);

private:
    juce::File favoritesFile() const { return pluginRoot.getChildFile("favorites.txt"); }
    juce::StringArray readFavorites() const;
    void writeFavorites(const juce::StringArray& keys) const;
    static juce::String safeStem(const juce::String& displayName);

    juce::File pluginRoot;
};

} // namespace suite
