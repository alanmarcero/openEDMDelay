#pragma once
#include "PluginProcessor.h"
#include "DelayLookAndFeel.h"
#include "suite/PresetBrowser.h"
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
    bool keyPressed(const juce::KeyPress&) override;
private:
    void timerCallback() override;
    void paintContent(juce::Graphics&);
    struct Content final:juce::Component {
        explicit Content(DelayEditor& editor):owner(editor) {}
        void paint(juce::Graphics& g) override {owner.paintContent(g);}
        DelayEditor& owner;
    };
    void place(int,juce::Rectangle<int>,bool label=true);
    void makeControl(int);
    void addSuiteButton(juce::TextButton&,const juce::String& id,const juce::String& tip);
    DelayProcessor& owner;
    DelayLookAndFeel look;
    TapGrid grid;
    std::unique_ptr<juce::Drawable> reel;
    Content content{*this};
    juce::Viewport viewport;
    std::array<std::unique_ptr<juce::Component>,parameterCount> controls;
    std::array<std::unique_ptr<juce::Label>,parameterCount> labels;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>> sliders;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> combos;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> buttons;
    juce::ComboBox preset,pattern,selectedTap;
    juce::TextButton prevPreset{"Previous"}, nextPreset{"Next"};
    juce::TextButton undoButton{"Undo"}, redoButton{"Redo"}, slotA{"A"}, slotB{"B"}, copyAB{"A>B"}, copyBA{"B>A"};
    std::unique_ptr<suite::PresetBrowser> browser;
    juce::Component scopeFrame;
    juce::TooltipWindow tooltip{this,650};
    int shownProgram=-1;
};
}
