#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace oed {

class DelayLookAndFeel : public juce::LookAndFeel_V4 {
public:
    DelayLookAndFeel();

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float startAngle, float endAngle, juce::Slider& slider) override;
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style,
                          juce::Slider& slider) override;
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                      int buttonW, int buttonH, juce::ComboBox& box) override;
    void drawLabel(juce::Graphics& g, juce::Label& label) override;
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool isHighlighted, bool isDown) override;
    void drawButtonText(juce::Graphics& g, juce::TextButton& button, bool isHighlighted, bool isDown) override;
    juce::Font getComboBoxFont(juce::ComboBox& box) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize(const juce::String&, bool, int, int&, int&) override;
    juce::Label* createSliderTextBox(juce::Slider& slider) override;
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override;
    void fillTextEditorBackground(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
    void drawTextEditorOutline(juce::Graphics& g, int width, int height, juce::TextEditor& editor) override;
};

} // namespace oed
