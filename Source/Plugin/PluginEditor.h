#pragma once
#include "PluginProcessor.h"
#include "DelayLookAndFeel.h"
namespace oed {
class TapGrid final:public juce::Component {
public:
    explicit TapGrid(DelayProcessor& p):owner(p){setComponentID("tapGrid");}
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
private:
    DelayProcessor& owner;
    int dragging=-1;
    float startLevel=0;
};
class DelayEditor final:public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit DelayEditor(DelayProcessor&);
    ~DelayEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void place(int,juce::Rectangle<int>,bool label=true);
    void makeControl(int);
    DelayProcessor& owner;
    DelayLookAndFeel look;
    TapGrid grid;
    juce::Image artwork;
    std::array<std::unique_ptr<juce::Component>,parameterCount> controls;
    std::array<std::unique_ptr<juce::Label>,parameterCount> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> combos;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttons;
    juce::ComboBox preset,pattern;
    juce::TooltipWindow tooltip{this,650};
    int shownProgram=-1;
};
}
