#include "DelayLookAndFeel.h"
#include "Theme.h"

namespace oed {

namespace {

constexpr float kArcThickness = 3.5f;
constexpr float kCornerRadius = 4.0f;
constexpr float kControlFontSize = 14.0f;

} // namespace

DelayLookAndFeel::DelayLookAndFeel() {
    setColour(juce::TooltipWindow::backgroundColourId, theme::kKnobBody);
    setColour(juce::TooltipWindow::textColourId, theme::kText);
    setColour(juce::TooltipWindow::outlineColourId, theme::kPanelEdge);
    setColour(juce::ResizableWindow::backgroundColourId, theme::kBackground);
    setColour(juce::Label::textColourId, theme::kText);
    setColour(juce::Slider::textBoxTextColourId, theme::kTextDim);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxHighlightColourId, theme::kTrack);
    setColour(juce::ComboBox::backgroundColourId, theme::kKnobBody);
    setColour(juce::ComboBox::textColourId, theme::kText);
    setColour(juce::ComboBox::arrowColourId, theme::kTextDim);
    setColour(juce::PopupMenu::backgroundColourId, theme::kPanel);
    setColour(juce::PopupMenu::textColourId, theme::kText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, theme::kTrack);
    setColour(juce::PopupMenu::highlightedTextColourId, theme::kText);
    setColour(juce::TextButton::buttonColourId, theme::kKnobBody);
    setColour(juce::TextButton::textColourOffId, theme::kText);
    setColour(juce::TextButton::textColourOnId, theme::kBackground);
    setColour(juce::TextEditor::backgroundColourId, theme::kKnobBody);
    setColour(juce::TextEditor::textColourId, theme::kText);
    setColour(juce::TextEditor::highlightColourId, theme::kTrack);
    setColour(juce::CaretComponent::caretColourId, theme::kText);
    setColour(juce::ScrollBar::thumbColourId, theme::kPanelEdge.brighter(0.25f));
    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
}

void DelayLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float startAngle, float endAngle, juce::Slider& slider) {
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
    const auto accent = slider.findColour(juce::Slider::rotarySliderFillColourId).withMultipliedAlpha(alpha);
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(4.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float arcRadius = radius - kArcThickness;
    const float valueAngle = startAngle + sliderPos * (endAngle - startAngle);
    const bool isBipolar = juce::approximatelyEqual(slider.getMinimum(), -slider.getMaximum());
    const float originAngle = isBipolar ? (startAngle + endAngle) / 2.0f : startAngle;
    const auto stroke = juce::PathStrokeType(kArcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour(theme::kTrack.withMultipliedAlpha(alpha));
    g.strokePath(track, stroke);

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, originAngle, valueAngle, true);
    g.setColour(accent);
    g.strokePath(value, stroke);

    const float bodyRadius = arcRadius - kArcThickness - 2.0f;
    const auto body = juce::Rectangle<float>(bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre(centre);
    g.setColour(juce::Colours::black.withAlpha(0.5f * alpha));
    g.fillEllipse(body.translated(0, 2));
    g.setGradientFill(juce::ColourGradient(theme::kKnobBody.brighter(0.65f).withMultipliedAlpha(alpha), centre.x, body.getY(),
                                           theme::kKnobBody.darker(0.4f).withMultipliedAlpha(alpha), centre.x, body.getBottom(), false));
    g.fillEllipse(body);
    g.setColour(theme::kTextDim.withAlpha(0.65f * alpha));
    g.drawEllipse(body, 1.0f);
    g.setColour(theme::kBackground.withAlpha(0.6f * alpha));
    g.drawEllipse(body.reduced(2.0f), 1.0f);

    const auto tip = centre.getPointOnCircumference(bodyRadius - 3.0f, valueAngle);
    const auto tail = centre.getPointOnCircumference(bodyRadius * 0.35f, valueAngle);
    g.setColour(theme::kText.withMultipliedAlpha(alpha));
    g.drawLine({ tail, tip }, 2.0f);
}

void DelayLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float, float, juce::Slider::SliderStyle style, juce::Slider& slider) {
    const float alpha = slider.isEnabled() ? 1.0f : 0.35f;
    const auto accent = slider.findColour(juce::Slider::trackColourId).withMultipliedAlpha(alpha);
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

    if (style == juce::Slider::LinearBar) {
        const auto box = bounds.reduced(0.5f);
        g.setColour(theme::kKnobBody.withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(box, kCornerRadius);
        g.setColour(accent.withAlpha((slider.isMouseOver(true) ? 0.9f : 0.45f) * alpha));
        g.drawRoundedRectangle(box, kCornerRadius, 1.0f);
        g.setColour(theme::kTextDim.withMultipliedAlpha(alpha));
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("STEP", box.withHeight(13.0f).translated(0.0f, 1.0f), juce::Justification::centred);
        return;
    }

    const float trackHeight = 4.0f;
    const auto track = juce::Rectangle<float>(bounds.getX() + 4.0f, bounds.getCentreY() - trackHeight * 0.5f,
                                              juce::jmax(1.0f, bounds.getWidth() - 8.0f), trackHeight);
    g.setColour(theme::kTrack.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(track, 2.0f);

    const bool isBipolar = juce::approximatelyEqual(slider.getMinimum(), -slider.getMaximum());
    const float originX = isBipolar ? track.getCentreX() : track.getX();
    const float clampedPos = juce::jlimit(track.getX(), track.getRight(), sliderPos);
    const float fillLeft = juce::jmin(originX, clampedPos);
    const float fillWidth = std::abs(clampedPos - originX);
    if (fillWidth > 0.5f) {
        g.setColour(accent);
        g.fillRoundedRectangle(fillLeft, track.getY(), fillWidth, trackHeight, 2.0f);
    }

    if (isBipolar) {
        g.setColour(theme::kTextDim.withAlpha(0.5f * alpha));
        g.drawVerticalLine(static_cast<int>(track.getCentreX()), track.getY() - 2.0f, track.getBottom() + 2.0f);
    }

    const float thumbRadius = 5.0f;
    g.setColour(accent);
    g.fillEllipse(clampedPos - thumbRadius, track.getCentreY() - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f);
    g.setColour(theme::kText.withMultipliedAlpha(alpha));
    g.drawEllipse(clampedPos - thumbRadius, track.getCentreY() - thumbRadius, thumbRadius * 2.0f, thumbRadius * 2.0f, 1.0f);
}

void DelayLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int,
                                  juce::ComboBox& box) {
    const float alpha = box.isEnabled() ? 1.0f : 0.35f;
    const auto bounds = juce::Rectangle<int>(width, height).toFloat().reduced(0.5f);
    g.setColour(box.findColour(juce::ComboBox::backgroundColourId).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, kCornerRadius);
    const float edgeAlpha = (box.isMouseOver(true) || box.hasKeyboardFocus(true)) ? 0.95f : 0.45f;
    g.setColour(box.findColour(juce::ComboBox::outlineColourId).withAlpha(edgeAlpha * alpha));
    g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);

    const auto arrowArea = bounds.withLeft(bounds.getRight() - 18.0f).reduced(5.0f, bounds.getHeight() * 0.38f);
    juce::Path arrow;
    arrow.addTriangle(arrowArea.getX(), arrowArea.getY(), arrowArea.getRight(), arrowArea.getY(),
                      arrowArea.getCentreX(), arrowArea.getBottom());
    g.setColour(box.findColour(juce::ComboBox::arrowColourId).withMultipliedAlpha(alpha));
    g.fillPath(arrow);
}

void DelayLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label) {
    const auto* parent = label.getParentComponent();
    const bool parentDisabled = (dynamic_cast<const juce::Slider*>(parent) != nullptr ||
                                 dynamic_cast<const juce::ComboBox*>(parent) != nullptr) &&
                                !parent->isEnabled();
    if (!parentDisabled) {
        LookAndFeel_V4::drawLabel(g, label);
        return;
    }
    const float savedAlpha = label.getAlpha();
    label.setAlpha(savedAlpha * 0.35f);
    LookAndFeel_V4::drawLabel(g, label);
    label.setAlpha(savedAlpha);
}

void DelayLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label) {
    label.setBounds(6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont(getComboBoxFont(box));
}

void DelayLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                          bool isHighlighted, bool) {
    const float alpha = button.isEnabled() ? 1.0f : 0.35f;
    const auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
    const auto accent = button.findColour(juce::TextButton::buttonOnColourId);
    const bool isOn = button.getToggleState();
    g.setColour((isOn ? accent : theme::kKnobBody).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(bounds, kCornerRadius);
    const float edgeAlpha = (isHighlighted || isOn || button.hasKeyboardFocus(false)) ? 0.95f : 0.45f;
    g.setColour(accent.withAlpha(edgeAlpha * alpha));
    g.drawRoundedRectangle(bounds, kCornerRadius, 1.0f);
}

void DelayLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool) {
    const float alpha = button.isEnabled() ? 1.0f : 0.35f;
    const bool isOn = button.getToggleState();
    g.setColour(button.findColour(isOn ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId).withMultipliedAlpha(alpha));
    g.setFont(juce::FontOptions(kControlFontSize, juce::Font::bold));
    g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred);
}

juce::Font DelayLookAndFeel::getComboBoxFont(juce::ComboBox&) {
    return juce::FontOptions(kControlFontSize);
}

void DelayLookAndFeel::getIdealPopupMenuItemSize(const juce::String& text, bool separator, int standardHeight,
                                                 int& width, int& height) {
    LookAndFeel_V4::getIdealPopupMenuItemSize(text, separator, standardHeight, width, height);
    if (!separator) height = std::max(height, 36);
}

juce::Font DelayLookAndFeel::getPopupMenuFont() {
    return juce::FontOptions(14.0f);
}

juce::Label* DelayLookAndFeel::createSliderTextBox(juce::Slider& slider) {
    auto* label = LookAndFeel_V4::createSliderTextBox(slider);
    label->setFont(juce::FontOptions(11.5f));
    label->setColour(juce::Label::outlineColourId,juce::Colours::transparentBlack);
    label->setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);
    return label;
}

} // namespace oed
