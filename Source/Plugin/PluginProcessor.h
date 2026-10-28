#pragma once
#include "Parameters.h"
namespace oed {
class DelayProcessor final:public juce::AudioProcessor {
public:
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
    void setPlainValue(int,float);
    void applyPattern(int);
    Settings settings() const {return readSettings(values);}
    juce::AudioProcessorValueTreeState parameters;
    ParameterValues values{};
    std::atomic<double> displayedBpm{120},displayedPhase{0};
    std::atomic<bool> hostSync{false};
    std::atomic<float> duckMeter{1},outputPeak{0};
private:
    DelayEngine engine;
    std::atomic<int> program{0};
    std::atomic<bool> resetPending{false};
    bool wasBypassed=false;
    double internalPhase=0, currentSampleRate=48000;
};
const juce::StringArray& patternNames();
}
