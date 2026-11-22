#pragma once
#include "Parameters.h"
#include "suite/ABCompare.h"
#include "suite/ParameterUndo.h"
#include <atomic>
#include <array>
namespace oed {
class DelayProcessor final:public juce::AudioProcessor {
public:
    static constexpr int scopeBinCount=64;
    DelayProcessor();
    const juce::String getName() const override {return "openEDMDelay";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    double getTailLengthSeconds() const override;
    void prepareToPlay(double,int) override;
    void releaseResources() override {}
    void reset() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;
    using juce::AudioProcessor::processBlockBypassed;
    juce::AudioProcessorParameter* getBypassParameter() const override;
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    int getNumPrograms() override;
    int getCurrentProgram() override {return program.load();}
    void setCurrentProgram(int) override;
    const juce::String getProgramName(int) override;
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    int displayedProgram() const {return oldBank.load() ? -1 : program.load();}
    juce::String currentPresetName() const {return presetName;}
    bool isPresetModified() const {return presetModified;}
    void markPresetSaved(const juce::String& name) {presetName=name;presetModified=false;}
    int savedEditorWidth() const {return editorWidth.load(std::memory_order_relaxed);}
    int savedEditorHeight() const {return editorHeight.load(std::memory_order_relaxed);}
    void setSavedEditorSize(int w,int h) {editorWidth.store(w,std::memory_order_relaxed);editorHeight.store(h,std::memory_order_relaxed);}
    void selectABSlot(int slot);
    void copyABSlot(bool aToB);
    void setPlainValue(int,float);
    void applyPattern(int);
    void recallProgram(int index,bool legacyBank);
    juce::MemoryBlock captureState();
    bool restoreParameters(const void*,int,bool preserveLocked=false);
    void applyUserPreset(const juce::MemoryBlock&,const juce::String& name={});
    bool isParamLocked(const juce::String& paramId) const;
    void setParamLocked(const juce::String& paramId,bool locked);
    void unlockAllParams();
    bool hasAnyLockedParam() const;
    Settings settings() const {return readSettings(values);}
    float scopeSidechain(int bin) const {return sideScope[static_cast<std::size_t>(juce::jlimit(0,scopeBinCount-1,bin))].load(std::memory_order_relaxed);}
    float scopeDuck(int bin) const {return duckScope[static_cast<std::size_t>(juce::jlimit(0,scopeBinCount-1,bin))].load(std::memory_order_relaxed);}
    int scopePlayhead() const {return scopeHead.load(std::memory_order_relaxed);}
    juce::AudioProcessorValueTreeState parameters;
    suite::ParameterUndo history;
    suite::ABCompare ab;
    ParameterValues values{};
    std::atomic<double> displayedBpm{120},displayedPhase{0};
    std::atomic<bool> hostSync{false};
    std::atomic<float> duckMeter{1},outputPeak{0};
private:
    Transport updateClock(int frames);
    void captureScope(const float* left,const float* right,int frames,double ppq,double bpm,float duck);
    void setPlainValueIfUnlocked(int id,float value);
    DelayEngine engine;
    std::array<std::atomic<bool>,parameterCount> paramLocks{};
    std::atomic<bool> oldBank{false};
    std::atomic<int> program{0};
    std::atomic<bool> resetPending{false};
    std::atomic<int> editorWidth{1200},editorHeight{850};
    juce::String presetName;
    bool presetModified=false;
    bool wasBypassed=false;
    double internalPhase=0, currentSampleRate=48000;
    std::array<std::atomic<float>,scopeBinCount> sideScope{},duckScope{};
    std::atomic<int> scopeHead{0};
};
const juce::StringArray& patternNames();
}
