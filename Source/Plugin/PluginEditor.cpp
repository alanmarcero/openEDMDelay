#include "PluginEditor.h"
#include "Theme.h"
#include <BinaryData.h>
#include <algorithm>
#include <cmath>

namespace oed {
namespace {

constexpr int kDesignWidth = 1200;
constexpr int kDesignHeight = 850;
constexpr float kInactiveAlpha = 0.40f;
const juce::Rectangle<int> kMeterBarBounds{925, 638, 235, 10};
const juce::Rectangle<int> kMeterPanelArea{920, 614, 246, 38};

void text(juce::Graphics& g, const juce::String& value, juce::Rectangle<int> bounds, float size,
          juce::Colour colour, bool bold = false, juce::Justification align = juce::Justification::centredLeft) {
    g.setColour(colour);
    g.setFont(juce::FontOptions(size, bold ? juce::Font::bold : juce::Font::plain));
    g.drawText(value, bounds, align);
}

void panel(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& title, juce::Colour accent) {
    const auto r = area.toFloat();
    g.setGradientFill(juce::ColourGradient(theme::kPanel.brighter(0.12f), r.getTopLeft(),
                                           theme::kPanel.darker(0.08f), r.getBottomRight(), false));
    g.fillRoundedRectangle(r, 10.0f);
    g.setColour(theme::kPanelEdge.withAlpha(0.65f));
    g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.0f);
    g.setColour(accent);
    g.fillRoundedRectangle(static_cast<float>(area.getX() + 16), static_cast<float>(area.getY() + 16), 3.0f, 15.0f, 1.5f);
    text(g, title, {area.getX() + 29, area.getY() + 12, area.getWidth() - 40, 24}, 13.0f, accent, true);
}

juce::Colour accent(int index) {
    return index % 2 == 0 ? theme::kAccentA : theme::kAccentB;
}

double effectiveTapStep(const Tap& tap, const Settings& s) {
    const double baseStep = static_cast<double>(std::clamp(tap.step, 1, 16));
    const double targetStep = static_cast<double>(std::clamp(tap.morphStep, 1, 16));
    const double morphed = baseStep + (targetStep - baseStep) * static_cast<double>(std::clamp(s.morph, 0.0f, 1.0f));
    const int index = std::max(0, static_cast<int>(std::round(morphed)) - 1);
    if (s.groove == 2) {
        constexpr double offsets[]{0.0, 0.12, -0.08, 0.18};
        return morphed + offsets[index % 4];
    }
    const double swingAmount = (s.groove == 1) ? 0.25 : std::clamp(static_cast<double>(s.swing), 0.0, 1.0) * 0.5;
    return morphed + (index % 2 != 0 ? swingAmount : 0.0);
}

void setControlActive(juce::Component* control, juce::Label* label, bool active,
                      const juce::String& activeTip, const juce::String& inactiveTip) {
    if (control == nullptr) return;
    const float alpha = active ? 1.0f : kInactiveAlpha;
    control->setAlpha(alpha);
    if (label != nullptr) label->setAlpha(alpha);
    if (auto* client = dynamic_cast<juce::SettableTooltipClient*>(control))
        client->setTooltip(active ? activeTip : (activeTip + " " + inactiveTip));
}

} // namespace

void TapGrid::paint(juce::Graphics& g) {
    const auto s = owner.settings();
    const float left = 32.0f;
    const float right = static_cast<float>(getWidth() - 14);
    const float step = (right - left) / 16.0f;

    for (int i = 0; i < 16; ++i) {
        const float x = left + (static_cast<float>(i) + 0.5f) * step;
        g.setColour(theme::kPanelEdge.withAlpha(i % 4 == 0 ? 0.9f : 0.35f));
        g.drawVerticalLine(static_cast<int>(x), 24.0f, static_cast<float>(getHeight() - 21));
        text(g, juce::String(i + 1), {static_cast<int>(x) - 10, 0, 20, 20}, 10.0f,
             i % 4 == 0 ? theme::kText : theme::kTextDim, false, juce::Justification::centred);
    }

    const auto phase = owner.displayedPhase.load() / beatRates[static_cast<std::size_t>(s.rate)];
    const float cursor = left + static_cast<float>(phase - std::floor(phase / 16.0) * 16.0) * step;
    g.setColour(theme::kAccentA.withAlpha(0.08f));
    g.fillRect(cursor - 4.0f, 23.0f, 8.0f, static_cast<float>(getHeight() - 44));
    g.setColour(theme::kAccentA.withAlpha(0.55f));
    g.drawVerticalLine(static_cast<int>(cursor), 24.0f, static_cast<float>(getHeight() - 21));

    double lastStep = 1.0;
    std::array<double, tapCount> soundedSteps{};
    for (int i = 0; i < tapCount; ++i) {
        const auto& tap = s.taps[static_cast<std::size_t>(i)];
        soundedSteps[static_cast<std::size_t>(i)] = effectiveTapStep(tap, s);
        if (tap.enabled && tap.level > 0.0f)
            lastStep = std::max(lastStep, soundedSteps[static_cast<std::size_t>(i)]);
    }

    for (int i = 0; i < tapCount; ++i) {
        const auto& tap = s.taps[static_cast<std::size_t>(i)];
        const float y = 30.0f + static_cast<float>(i) * 21.5f;
        const float chanceAlpha = tap.enabled ? (0.35f + 0.65f * std::clamp(tap.probability, 0.0f, 1.0f)) : 0.22f;
        const auto colour = accent(i).withAlpha(chanceAlpha);
        text(g, juce::String(i + 1).paddedLeft('0', 2), {0, static_cast<int>(y) - 8, 23, 17}, 10.0f, colour, true);
        g.setColour(theme::kBackground.withAlpha(0.2f));
        g.fillRoundedRectangle(left, y - 7.0f, right - left, 14.0f, 3.0f);

        const float tapStep = static_cast<float>(soundedSteps[static_cast<std::size_t>(i)]);
        const float x = left + (tapStep - 0.5f) * step;
        if (tap.enabled && tap.level > 0.0f) {
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.02f), left, y, colour.withAlpha(0.33f), x, y, false));
            g.fillRect(left, y - 3.0f, std::max(0.0f, x - left), 6.0f);
            g.setColour(colour.withAlpha(0.55f));
            g.drawLine(left, y, x, y, 1.0f);

            const int repeats = std::clamp(tap.ratchets, 1, 8);
            for (int r = 1; r < repeats; ++r) {
                const float rx = left + (tapStep + static_cast<float>(r) / static_cast<float>(repeats) - 0.5f) * step;
                if (rx > right) break;
                g.setColour(colour.withAlpha(0.65f));
                g.fillRoundedRectangle(rx - 1.5f, y - 4.0f, 3.0f, 8.0f, 1.0f);
            }

            if (s.feedback > 0.005f || s.freeze) {
                for (int echo = 1; echo <= 16; ++echo) {
                    const float echoX = left + (tapStep + static_cast<float>(echo) * static_cast<float>(lastStep) - 0.5f) * step;
                    if (echoX > right) break;
                    const float echoGain = s.freeze ? 0.6f : std::pow(s.feedback, static_cast<float>(echo));
                    const float dotAlpha = std::clamp(echoGain * 0.75f, 0.0f, 0.75f);
                    if (dotAlpha < 0.04f) break;
                    g.setColour(accent(i).withAlpha(dotAlpha));
                    g.fillEllipse(echoX - 2.5f, y - 2.5f, 5.0f, 5.0f);
                }
            }
        }
        const float radius = 3.0f + tap.level * 3.0f;
        g.setColour(colour.withAlpha(tap.enabled ? 0.12f : 0.04f));
        g.fillEllipse(x - radius - 4.0f, y - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f);
        g.setColour(colour);
        g.fillRoundedRectangle(x - 5.0f, y - radius, 10.0f, radius * 2.0f, 3.0f);
        if (tap.enabled && tap.probability < 0.99f) {
            g.setColour(theme::kText.withAlpha(0.55f));
            g.drawRoundedRectangle(x - 5.0f, y - radius, 10.0f, radius * 2.0f, 3.0f, 1.0f);
        }
    }
    text(g, "DRAG: time + level    /    RIGHT-CLICK: mute", {32, getHeight() - 19, getWidth() - 40, 18}, 10.0f, theme::kTextDim);
}

void TapGrid::mouseDown(const juce::MouseEvent& e) {
    dragging = juce::jlimit(0, 7, static_cast<int>((e.position.y - 19.0f) / 21.5f));
    const int id = baseCount + dragging * 4;
    if (onTapEdited) onTapEdited();
    if (e.mods.isRightButtonDown()) {
        owner.history.beginGesture();
        owner.setPlainValue(id, owner.values[static_cast<std::size_t>(id)]->load() > 0.5f ? 0.0f : 1.0f);
        owner.history.endGesture();
        dragging = -1;
        repaint();
        return;
    }
    startLevel = owner.values[static_cast<std::size_t>(id + 2)]->load();
    for (int p : {id + 1, id + 2})
        owner.parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(p)].id)->beginChangeGesture();
    owner.setPlainValue(id, 1.0f);
    mouseDrag(e);
}

void TapGrid::mouseDrag(const juce::MouseEvent& e) {
    if (dragging < 0) return;
    const int id = baseCount + dragging * 4;
    const float width = static_cast<float>(getWidth() - 46) / 16.0f;
    owner.setPlainValue(id + 1, static_cast<float>(juce::jlimit(1, 16, static_cast<int>((e.position.x - 32.0f) / width) + 1)));
    owner.setPlainValue(id + 2, juce::jlimit(0.0f, 100.0f, startLevel - static_cast<float>(e.getDistanceFromDragStartY())));
    repaint();
}

void TapGrid::mouseUp(const juce::MouseEvent&) {
    if (dragging < 0) return;
    const int id = baseCount + dragging * 4;
    for (int p : {id + 1, id + 2})
        owner.parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(p)].id)->endChangeGesture();
    dragging = -1;
}

DelayEditor::DelayEditor(DelayProcessor& p) : AudioProcessorEditor(p), owner(p), grid(p) {
    setLookAndFeel(&look);
    reel = juce::Drawable::createFromImageData(BinaryData::echoreel_svg, BinaryData::echoreel_svgSize);
    content.setSize(kDesignWidth, kDesignHeight);
    addAndMakeVisible(content);

    for (int i = 0; i < parameterCount; ++i) makeControl(i);
    content.addAndMakeVisible(grid);
    grid.onTapEdited = [this] { pattern.setSelectedId(0, juce::dontSendNotification); };
    content.addAndMakeVisible(preset);
    content.addAndMakeVisible(pattern);
    preset.setComponentID("preset");
    preset.setTitle("Preset");
    pattern.setComponentID("pattern");
    pattern.setTitle("Pattern");

    juce::String family;
    juce::PopupMenu sounds;
    int programId = 1;
    for (int i = 0; i < p.getNumPrograms(); ++i) {
        const auto name = p.getProgramName(i);
        const auto nextFamily = name.upToFirstOccurrenceOf(" / ", false, false);
        if (family != nextFamily && sounds.getNumItems() > 0) {
            preset.getRootMenu()->addSubMenu(family, sounds);
            sounds.clear();
        }
        family = nextFamily;
        sounds.addItem(programId++, name);
    }
    if (sounds.getNumItems() > 0) preset.getRootMenu()->addSubMenu(family, sounds);
    preset.setTextWhenNothingSelected("Custom sound");
    preset.setTooltip("Load a factory delay. Programs are grouped by the kind of echo.");
    preset.onChange = [this] {
        if (preset.getSelectedId() <= 0) return;
        owner.setCurrentProgram(preset.getSelectedId() - 1);
        pattern.setSelectedId(0, juce::dontSendNotification);
        syncPresetDisplay();
    };

    prevPreset.setComponentID("prevPreset");
    prevPreset.setTitle("Previous preset");
    prevPreset.setTooltip("Previous preset");
    prevPreset.onClick = [this] {
        if (browser) browser->stepPreset(-1);
        pattern.setSelectedId(0, juce::dontSendNotification);
        syncPresetDisplay();
    };
    content.addAndMakeVisible(prevPreset);

    nextPreset.setComponentID("nextPreset");
    nextPreset.setTitle("Next preset");
    nextPreset.setTooltip("Next preset");
    nextPreset.onClick = [this] {
        if (browser) browser->stepPreset(1);
        pattern.setSelectedId(0, juce::dontSendNotification);
        syncPresetDisplay();
    };
    content.addAndMakeVisible(nextPreset);

    pattern.addItemList(patternNames(), 1);
    pattern.setTextWhenNothingSelected("Pattern / Custom");
    pattern.setTooltip("Load tap positions, levels and stereo pans. Grid rate and effects are preserved.");
    pattern.onChange = [this] { owner.applyPattern(pattern.getSelectedId() - 1); };

    content.addAndMakeVisible(selectedTap);
    selectedTap.setComponentID("performanceTap");
    selectedTap.setTitle("Edit tap");
    for (int i = 0; i < tapCount; ++i) selectedTap.addItem("Edit tap " + juce::String(i + 1), i + 1);
    selectedTap.setTooltip("Choose which tap to edit below.");
    selectedTap.setSelectedId(1, juce::dontSendNotification);
    selectedTap.onChange = [this] { resized(); };

    addSuiteButton(undoButton, "undo", "Undo the last edit. Ctrl+Z or Cmd+Z.", "Undo");
    addSuiteButton(redoButton, "redo", "Redo. Shift+Ctrl+Z or Shift+Cmd+Z.", "Redo");
    addSuiteButton(slotA, "slotA", "Hear setup A.", "Slot A");
    addSuiteButton(slotB, "slotB", "Hear setup B.", "Slot B");
    addSuiteButton(copyAB, "copyAtoB", "Copy A into B.", "Copy A to B");
    addSuiteButton(copyBA, "copyBtoA", "Copy B into A.", "Copy B to A");
    slotA.setClickingTogglesState(false);
    slotB.setClickingTogglesState(false);
    slotA.setColour(juce::TextButton::buttonOnColourId, theme::kAccentA);
    slotB.setColour(juce::TextButton::buttonOnColourId, theme::kAccentB);
    undoButton.onClick = [this] { owner.history.undo(); pattern.setSelectedId(0, juce::dontSendNotification); };
    redoButton.onClick = [this] { owner.history.redo(); pattern.setSelectedId(0, juce::dontSendNotification); };
    slotA.onClick = [this] { owner.selectABSlot(0); pattern.setSelectedId(0, juce::dontSendNotification); };
    slotB.onClick = [this] { owner.selectABSlot(1); pattern.setSelectedId(0, juce::dontSendNotification); };
    copyAB.onClick = [this] { owner.copyABSlot(true); };
    copyBA.onClick = [this] { owner.copyABSlot(false); };

    addSuiteButton(pageMain, "pageMain", "Show Time, Echo Filters, and Duck controls.", "Main controls");
    addSuiteButton(pageGroove, "pageGroove", "Show Groove, Hold, and Tap Performance controls.", "Groove and taps");
    addSuiteButton(pageFx, "pageFx", "Show Diffusion, Shimmer, Glitch, and Reverse controls.", "FX and glitch");
    for (auto* tab : {&pageMain, &pageGroove, &pageFx})
        tab->setColour(juce::TextButton::buttonOnColourId, theme::kAccentA);
    pageMain.onClick = [this] { setLowerPage(0); };
    pageGroove.onClick = [this] { setLowerPage(1); };
    pageFx.onClick = [this] { setLowerPage(2); };

    browser = std::make_unique<suite::PresetBrowser>(suite::PresetStore::defaultRoot("openEDMDelay"), suite::PresetHandlers{
        .count = [this] { return owner.getNumPrograms(); },
        .name = [this](int index) { return owner.getProgramName(index); },
        .loadFactory = [this](int index) {
            owner.setCurrentProgram(index);
            pattern.setSelectedId(0, juce::dontSendNotification);
            syncPresetDisplay();
        },
        .capture = [this] { return owner.captureState(); },
        .loadUser = [this](const juce::MemoryBlock& block) {
            const auto name = browser ? browser->selectedDisplayName() : juce::String{};
            owner.applyUserPreset(block, name);
            pattern.setSelectedId(0, juce::dontSendNotification);
            syncPresetDisplay();
        },
        .onSaved = [this](const juce::String& name) {
            owner.markPresetSaved(name);
            syncPresetDisplay();
        }
    });
    content.addAndMakeVisible(*browser);

    scopeFrame.setComponentID("sidechainScope");
    scopeFrame.setInterceptsMouseClicks(false, false);
    content.addAndMakeVisible(scopeFrame);

    setLowerPage(0);
    syncPresetDisplay();
    setWantsKeyboardFocus(true);
    const int initialWidth = owner.savedEditorWidth();
    const int initialHeight = owner.savedEditorHeight();
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(static_cast<double>(kDesignWidth) / static_cast<double>(kDesignHeight));
    setResizeLimits(840, 595, 1920, 1360);
    setSize(initialWidth, initialHeight);
    startTimerHz(24);
}

DelayEditor::~DelayEditor() {
    stopTimer();
    setLookAndFeel(nullptr);
}

void DelayEditor::makeControl(int i) {
    const auto& spec = parameterSpecs()[static_cast<std::size_t>(i)];
    const auto colour = i >= baseCount ? accent((i - baseCount) / 4) : (i == highPass || i == hpSlope ? theme::kAccentA : theme::kAccentB);
    auto label = std::make_unique<juce::Label>();
    label->setText(spec.name, juce::dontSendNotification);
    label->setJustificationType(juce::Justification::centred);
    label->setFont(juce::FontOptions(11.5f));
    label->setColour(juce::Label::textColourId, theme::kTextDim);
    content.addAndMakeVisible(*label);
    labels[static_cast<std::size_t>(i)] = std::move(label);

    if (i == bypass || (i >= baseCount && i < swing && (i - baseCount) % 4 == 0)) {
        const int tapNumber = (i - baseCount) / 4 + 1;
        auto c = std::make_unique<juce::TextButton>(i == bypass ? "BYPASS" : "TAP " + juce::String(tapNumber).paddedLeft('0', 2));
        c->setClickingTogglesState(true);
        c->setColour(juce::TextButton::buttonOnColourId, i == bypass ? theme::kAccentB : colour);
        c->setComponentID(spec.id);
        c->setTitle(i == bypass ? "Bypass" : "Tap " + juce::String(tapNumber) + " enable");
        c->setTooltip(i == bypass ? "Bypass the delay effect." : "Enable or bypass tap " + juce::String(tapNumber) + ".");
        content.addAndMakeVisible(*c);
        buttons.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters, spec.id, *c));
        controls[static_cast<std::size_t>(i)] = std::move(c);
        return;
    }

    if (spec.choices.isNotEmpty()) {
        auto c = std::make_unique<juce::ComboBox>();
        c->setTitle(spec.name);
        c->setTooltip(spec.name);
        c->addItemList(juce::StringArray::fromTokens(spec.choices, "|", ""), 1);
        c->setColour(juce::ComboBox::outlineColourId, colour);
        c->setComponentID(spec.id);
        content.addAndMakeVisible(*c);
        combos.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters, spec.id, *c));
        controls[static_cast<std::size_t>(i)] = std::move(c);
        return;
    }

    auto c = std::make_unique<juce::Slider>();
    c->setLookAndFeel(&look);
    c->setTitle(spec.name);
    c->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 86, 20);
    c->setColour(juce::Slider::rotarySliderFillColourId, colour);
    c->setComponentID(spec.id);
    const bool step = spec.id.endsWith("Step");
    const bool pan = i >= baseCount && i < swing && (i - baseCount) % 4 == 3;
    c->setDoubleClickReturnValue(true, pan ? 0.0 : spec.initial);
    if (step) {
        c->setSliderStyle(juce::Slider::LinearBar);
        c->setTextBoxIsEditable(true);
        c->setColour(juce::Slider::trackColourId, colour);
        c->setColour(juce::Slider::thumbColourId, colour);
        c->setMouseDragSensitivity(180);
    }
    if (i == drive || i == motion || i == wobbleRate) {
        c->setSliderStyle(juce::Slider::LinearHorizontal);
        c->setTextBoxStyle(juce::Slider::TextBoxRight, false, i == wobbleRate ? 58 : 38, 22);
        c->setColour(juce::Slider::trackColourId, colour);
        c->setColour(juce::Slider::thumbColourId, colour);
    }
    if (pan) {
        c->setSliderStyle(juce::Slider::LinearHorizontal);
        c->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 16);
        c->setColour(juce::Slider::trackColourId, colour);
        c->setColour(juce::Slider::thumbColourId, colour);
    }
    content.addAndMakeVisible(*c);
    sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters, spec.id, *c));
    c->setTooltip(spec.name + ". Drag to adjust; double-click to reset. Host automatable.");
    controls[static_cast<std::size_t>(i)] = std::move(c);

    auto* tooltipClient = dynamic_cast<juce::SettableTooltipClient*>(controls[static_cast<std::size_t>(i)].get());
    if (tooltipClient == nullptr) return;
    if (i == decay) tooltipClient->setTooltip("Progressive level falloff across the tap pattern. Feedback controls phrase repeats.");
    if (i == feedback) tooltipClient->setTooltip("Repeat the full phrase from the last active tap. Maximum 97%.");
    if (i == gridRate) tooltipClient->setTooltip("Each grid step is this note length. T = triplet; D = dotted. Always tempo-synced.");
    if (i == diffusion) tooltipClient->setTooltip("Smears each echo inside the feedback loop. Higher values make repeats denser.");
    if (i == shimmer) tooltipClient->setTooltip("Octave-up pitch-shifted recirculation inside the feedback loop for lush shimmer echoes.");
    if (i == duckMode) tooltipClient->setTooltip("Beat: rhythmic wet ducking. Input: duck echoes while incoming audio is loud. Sidechain: duck from another track.");
    if (i == routing) tooltipClient->setTooltip("Stereo uses tap pan. Ping Pong crossfeeds each repeat so echoes bounce left and right. Mono sums the wet signal.");
    if (i == glitch) tooltipClient->setTooltip("Chops the wet signal after the delay: Gate, Stutter, or a backwards slice.");
    if (i == reverse) tooltipClient->setTooltip("Plays each echo backwards in tempo-synced chunks, with click-free crossfades.");
}

void DelayEditor::place(int id, juce::Rectangle<int> r, bool showLabel) {
    if (dynamic_cast<juce::ComboBox*>(controls[static_cast<std::size_t>(id)].get()) != nullptr)
        r.setHeight(36);
    controls[static_cast<std::size_t>(id)]->setBounds(r);
    auto& label = *labels[static_cast<std::size_t>(id)];
    label.setVisible(showLabel);
    label.setBounds(r.getX() - 6, r.getBottom() + 1, r.getWidth() + 12, 18);
}

void DelayEditor::setLowerPage(int page) {
    lowerPage = juce::jlimit(0, 2, page);
    pageMain.setToggleState(lowerPage == 0, juce::dontSendNotification);
    pageGroove.setToggleState(lowerPage == 1, juce::dontSendNotification);
    pageFx.setToggleState(lowerPage == 2, juce::dontSendNotification);
    resized();
    content.repaint(20, 598, 875, 234);
}

void DelayEditor::resized() {
    const float scale = (getWidth() > 0 && getHeight() > 0)
        ? std::min(static_cast<float>(getWidth()) / static_cast<float>(kDesignWidth),
                   static_cast<float>(getHeight()) / static_cast<float>(kDesignHeight))
        : 1.0f;
    content.setBounds(0, 0, kDesignWidth, kDesignHeight);
    content.setTransform(juce::AffineTransform::scale(scale));
    if (getWidth() > 0 && getHeight() > 0) owner.setSavedEditorSize(getWidth(), getHeight());

    prevPreset.setBounds(641, 16, 40, 40);
    preset.setBounds(685, 16, 355, 40);
    nextPreset.setBounds(1044, 16, 40, 40);
    place(bypass, {1094, 16, 86, 40}, false);

    undoButton.setBounds(16, 60, 48, 36);
    redoButton.setBounds(68, 60, 48, 36);
    slotA.setBounds(124, 60, 44, 36);
    slotB.setBounds(172, 60, 44, 36);
    copyAB.setBounds(220, 60, 52, 36);
    copyBA.setBounds(276, 60, 52, 36);
    if (browser) browser->setBounds(336, 60, 544, 36);

    pattern.setBounds(645, 106, 195, 36);
    grid.setBounds(39, 149, 801, 218);

    place(routing, {896, 202, 132, 36}, false);
    place(character, {1040, 202, 124, 36}, false);
    place(drive, {901, 250, 119, 24});
    place(motion, {1038, 250, 119, 24});
    place(wobbleRate, {901, 304, 119, 30});
    place(wobbleShape, {1038, 300, 124, 36});

    for (int i = 0; i < tapCount; ++i) {
        const int x = 31 + i * 145;
        const int p = baseCount + i * 4;
        place(p, {x + 2, 386, 66, 36}, false);
        place(p + 1, {x + 70, 386, 74, 36}, false);
        place(p + 2, {x + 26, 424, 88, 88}, false);
        place(p + 3, {x + 13, 514, 116, 34}, false);
    }

    pageMain.setBounds(20, 558, 130, 36);
    pageGroove.setBounds(156, 558, 150, 36);
    pageFx.setBounds(312, 558, 140, 36);

    // Pinned OUTPUT panel.
    place(mix, {914, 692, 62, 100});
    place(width, {978, 692, 62, 100});
    place(stereoOffset, {1042, 692, 62, 100});
    place(output, {1106, 692, 62, 100});

    // Page 0: Main (Time & Repeats, Echo Filters, Duck).
    const bool showMain = (lowerPage == 0);
    for (int id : std::initializer_list<int>{gridRate, feedback, decay, tempo, highPass, lowPass, hpSlope, lpSlope, duckMode, duck, duckRate, duckHold}) {
        controls[static_cast<std::size_t>(id)]->setVisible(showMain);
        labels[static_cast<std::size_t>(id)]->setVisible(false);
    }
    scopeFrame.setVisible(showMain);
    if (showMain) {
        place(gridRate, {37, 636, 128, 36}, false);
        place(feedback, {30, 686, 88, 114});
        place(decay, {122, 686, 88, 114});
        place(tempo, {213, 686, 78, 114});
        place(highPass, {338, 648, 102, 104});
        place(lowPass, {466, 648, 102, 104});
        place(hpSlope, {332, 782, 117, 36}, false);
        place(lpSlope, {460, 782, 117, 36}, false);
        place(duckMode, {622, 644, 124, 36}, false);
        place(duckRate, {754, 644, 124, 36}, false);
        place(duck, {634, 686, 98, 74});
        place(duckHold, {764, 686, 98, 74});
        scopeFrame.setBounds(622, 786, 256, 36);
    }

    // Page 1: Groove & Taps.
    const bool showGroove = (lowerPage == 1);
    for (int id : {swing, groove, freeze, patternMorph}) {
        controls[static_cast<std::size_t>(id)]->setVisible(showGroove);
        labels[static_cast<std::size_t>(id)]->setVisible(false);
    }
    selectedTap.setVisible(showGroove);
    if (showGroove) {
        selectedTap.setBounds(702, 610, 176, 36);
        place(swing, {32, 654, 80, 104});
        place(groove, {118, 654, 96, 36});
        place(freeze, {118, 722, 96, 36});
        place(patternMorph, {220, 654, 80, 104});
    }
    for (int tap = 0; tap < tapCount; ++tap) {
        for (int field = 0; field < tapExtraCount; ++field) {
            const int id = tapExtraStart + tap * tapExtraCount + field;
            const bool visible = showGroove && (tap == selectedTap.getSelectedId() - 1);
            controls[static_cast<std::size_t>(id)]->setVisible(visible);
            labels[static_cast<std::size_t>(id)]->setVisible(visible);
            if (visible) {
                const bool isChoiceOrLinear = (field == 1 || field == 2 || field == 5 || field == 7);
                place(id, {330 + field * 69, isChoiceOrLinear ? 690 : 656, 65, isChoiceOrLinear ? 36 : 100});
            }
        }
    }

    // Page 2: FX & Glitch.
    const bool showFx = (lowerPage == 2);
    for (int id : std::initializer_list<int>{diffusion, diffusionSize, shimmer, glitch, glitchAmount, glitchRate, reverse, reverseRate}) {
        controls[static_cast<std::size_t>(id)]->setVisible(showFx);
        labels[static_cast<std::size_t>(id)]->setVisible(false);
    }
    if (showFx) {
        place(diffusion, {34, 666, 86, 112});
        place(diffusionSize, {127, 666, 86, 112});
        place(shimmer, {220, 666, 86, 112});
        place(glitch, {352, 664, 104, 36}, false);
        place(glitchAmount, {464, 654, 88, 112});
        place(glitchRate, {352, 734, 104, 36}, false);
        place(reverse, {574, 654, 96, 112});
        place(reverseRate, {684, 664, 140, 36});
    }
}

void DelayEditor::mouseDown(const juce::MouseEvent& e) {
    const float scale = (getWidth() > 0 && getHeight() > 0)
        ? std::min(static_cast<float>(getWidth()) / static_cast<float>(kDesignWidth),
                   static_cast<float>(getHeight()) / static_cast<float>(kDesignHeight))
        : 1.0f;
    const auto designPoint = (e.position / std::max(0.1f, scale)).toInt();
    if (kMeterPanelArea.contains(designPoint)) {
        meterClipped = false;
        meterPeakHoldDb = meterDb;
        content.repaint(kMeterPanelArea);
    }
}

void DelayEditor::paint(juce::Graphics& g) {
    g.fillAll(theme::kBackground);
}

void DelayEditor::paintContent(juce::Graphics& g) {
    g.fillAll(theme::kBackground);
    g.setGradientFill(juce::ColourGradient(theme::kAccentA.withAlpha(0.12f), 0.0f, 0.0f,
                                           theme::kAccentB.withAlpha(0.03f), 1200.0f, 850.0f, false));
    g.fillAll();

    g.setColour(theme::kAccentA.withAlpha(0.75f));
    g.fillRect(20, 0, 290, 2);
    g.setColour(theme::kAccentB.withAlpha(0.6f));
    g.fillRect(880, 0, 300, 2);

    text(g, "openEDM", {26, 16, 163, 37}, 29.0f, theme::kText, true);
    text(g, "Delay", {151, 16, 124, 37}, 29.0f, theme::kAccentA, true);

    const bool synced = owner.hostSync.load();
    g.setColour(synced ? theme::kAccentA : theme::kAccentB);
    g.fillEllipse(443.0f, 32.0f, 6.0f, 6.0f);
    text(g, juce::String(owner.displayedBpm.load(), 1) + " BPM  /  " + (synced ? "HOST" : "INTERNAL"),
         {458, 22, 175, 29}, 12.5f, theme::kTextDim);

    panel(g, {20, 100, 840, 274}, "TAP FIELD", theme::kAccentA);
    text(g, "16 STEPS  /  8 VOICES", {344, 114, 235, 20}, 10.5f, theme::kTextDim);

    panel(g, {880, 100, 300, 274}, "", theme::kAccentB);
    g.setColour(theme::kBackground);
    g.fillRoundedRectangle(894.0f, 108.0f, 272.0f, 74.0f, 6.0f);
    if (reel) {
        const float angle = static_cast<float>(std::remainder(owner.displayedPhase.load(), 2.0)) * juce::MathConstants<float>::pi;
        g.setColour(theme::kAccentB.withAlpha(0.85f));
        juce::Path tape;
        tape.startNewSubPath(952.0f, 169.0f);
        tape.lineTo(986.0f, 177.0f);
        tape.lineTo(1074.0f, 177.0f);
        tape.lineTo(1108.0f, 169.0f);
        g.strokePath(tape, juce::PathStrokeType(2.0f));
        for (float x : {952.0f, 1108.0f}) {
            const auto turn = juce::AffineTransform::translation(-32.0f, -32.0f).rotated(angle).translated(x, 144.0f);
            reel->draw(g, 1.0f, turn);
        }
        text(g, "TAPE ECHO", {990, 131, 80, 26}, 11.0f, theme::kTextDim, true, juce::Justification::centred);
    }
    text(g, "ROUTING", {899, 186, 120, 15}, 10.5f, theme::kTextDim, true);
    text(g, "CHARACTER", {1042, 186, 120, 15}, 10.5f, theme::kTextDim, true);

    g.setColour(theme::kPanel);
    g.fillRoundedRectangle(20.0f, 382.0f, 1160.0f, 170.0f, 10.0f);
    for (int i = 0; i < 8; ++i) {
        const int x = 31 + i * 145;
        if (i > 0) {
            g.setColour(theme::kPanelEdge.withAlpha(0.6f));
            g.drawVerticalLine(x - 6, 394, 540);
        }
        text(g, "L", {x + 2, 514, 13, 18}, 9.0f, theme::kTextDim);
        text(g, "R", {x + 128, 514, 13, 18}, 9.0f, theme::kTextDim);
    }

    const auto s = owner.settings();

    if (lowerPage == 0) {
        panel(g, {20, 600, 280, 230}, "TIME & REPEATS", theme::kAccentA);
        text(g, "STEP LENGTH", {177, 642, 106, 22}, 10.0f, theme::kTextDim);

        panel(g, {315, 600, 280, 230}, "ECHO FILTERS", theme::kAccentB);
        text(g, "IN THE FEEDBACK LOOP", {335, 628, 236, 16}, 10.0f, theme::kTextDim);
        text(g, "SLOPE", {332, 766, 117, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);
        text(g, "SLOPE", {460, 766, 117, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);

        panel(g, {610, 600, 280, 230}, "DUCK", theme::kAccentFx);
        const float duckGain = juce::jlimit(1.0e-4f, 1.0f, owner.duckMeter.load());
        const float grDb = 20.0f * std::log10(duckGain);
        const juce::String grText = (std::abs(grDb) < 0.05f) ? "GR 0.0 dB" : ("GR " + juce::String(grDb, 1) + " dB");
        text(g, grText, {768, 612, 110, 20}, 10.5f, theme::kAccentFx, true, juce::Justification::centredRight);
        text(g, "SOURCE", {622, 628, 124, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);
        const juce::String duckRateCaption = (s.duckSource == DuckSource::beat) ? "RATE" : "RELEASE";
        text(g, duckRateCaption, {754, 628, 124, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);

        const auto area = scopeFrame.getBounds().toFloat();
        g.setColour(theme::kBackground.withAlpha(0.55f));
        g.fillRoundedRectangle(area, 4.0f);
        g.setColour(theme::kPanelEdge.withAlpha(0.8f));
        g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(theme::kPanelEdge.withAlpha(0.45f));
        g.drawVerticalLine(static_cast<int>(area.getCentreX()), area.getY() + 2.0f, area.getBottom() - 2.0f);
        juce::Path kick, curve;
        for (int i = 0; i < DelayProcessor::scopeBinCount; ++i) {
            const float x = area.getX() + (static_cast<float>(i) + 0.5f) / DelayProcessor::scopeBinCount * area.getWidth();
            const float side = juce::jlimit(0.0f, 1.0f, owner.scopeSidechain(i));
            const float gain = juce::jlimit(0.0f, 1.0f, owner.scopeDuck(i));
            const float yKick = area.getBottom() - 2.0f - side * (area.getHeight() - 4.0f);
            const float yDuck = area.getY() + 2.0f + (1.0f - gain) * (area.getHeight() - 4.0f);
            if (i == 0) {
                kick.startNewSubPath(area.getX(), area.getBottom());
                kick.lineTo(x, yKick);
                curve.startNewSubPath(x, yDuck);
                continue;
            }
            kick.lineTo(x, yKick);
            curve.lineTo(x, yDuck);
        }
        kick.lineTo(area.getRight(), area.getBottom());
        kick.closeSubPath();
        g.setColour(theme::kAccentB.withAlpha(0.45f));
        g.fillPath(kick);
        g.setColour(theme::kAccentA);
        g.strokePath(curve, juce::PathStrokeType(1.6f));
        const float headX = area.getX() + (static_cast<float>(owner.scopePlayhead()) + 0.5f) / DelayProcessor::scopeBinCount * area.getWidth();
        g.setColour(theme::kText.withAlpha(0.7f));
        g.drawVerticalLine(static_cast<int>(headX), area.getY() + 1.0f, area.getBottom() - 1.0f);
        text(g, "SIDECHAIN", {static_cast<int>(area.getX()) + 6, static_cast<int>(area.getY()) + 1, 78, 12}, 9.0f, theme::kTextDim);
    }
    if (lowerPage == 1) {
        panel(g, {20, 600, 290, 230}, "GROOVE & HOLD", theme::kAccentA);
        panel(g, {320, 600, 570, 230}, "TAP PERFORMANCE", theme::kAccentB);
    }
    if (lowerPage == 2) {
        panel(g, {20, 600, 300, 230}, "DIFFUSION & SHIMMER", theme::kAccentA);
        panel(g, {335, 600, 555, 230}, "GLITCH & REVERSE", theme::kAccentFx);
        text(g, "GLITCH", {352, 648, 104, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);
        text(g, "RATE", {352, 718, 104, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);
        text(g, "RATE", {684, 648, 140, 14}, 10.0f, theme::kTextDim, true, juce::Justification::centred);
    }

    // Pinned OUTPUT panel and dBFS meter with 1 s peak hold + click-to-clear clip latch.
    panel(g, {905, 600, 275, 230}, "OUTPUT", theme::kAccentOut);
    const juce::String meterLabel = meterClipped ? "CLIP" : (meterDb <= -59.5f ? "-inf dB" : juce::String(meterDb, 1) + " dB");
    text(g, meterLabel, {1060, 616, 100, 18}, 10.0f, meterClipped ? juce::Colour(0xffff5544) : theme::kTextDim,
         meterClipped, juce::Justification::centredRight);

    const float meterNorm = juce::jlimit(0.0f, 1.0f, (meterDb + 60.0f) / 60.0f);
    const float holdNorm = juce::jlimit(0.0f, 1.0f, (meterPeakHoldDb + 60.0f) / 60.0f);
    const auto bar = kMeterBarBounds.toFloat();
    g.setColour(theme::kPanelEdge.withAlpha(0.7f));
    g.fillRoundedRectangle(bar, 2.5f);
    if (meterNorm > 0.0f) {
        g.setColour(meterClipped ? juce::Colour(0xffff5544) : theme::kAccentA);
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * meterNorm), 2.5f);
    }
    if (holdNorm > 0.01f) {
        const float holdX = bar.getX() + bar.getWidth() * holdNorm;
        g.setColour(meterClipped ? juce::Colour(0xffff5544) : theme::kAccentOut);
        g.fillRect(holdX - 1.0f, bar.getY() - 1.0f, 2.0f, bar.getHeight() + 2.0f);
    }

    text(g, "BEAT SYNC ONLY   /   T = triplet   /   D = dotted", {25, 832, 470, 18}, 10.0f, theme::kTextDim);
    text(g, "ALAN MARCERO  /  openEDM  /  v" JucePlugin_VersionString, {880, 832, 297, 18}, 10.0f, theme::kTextDim,
         false, juce::Justification::centredRight);
}

void DelayEditor::updateControlStates(const Settings& s) {
    const auto set = [&](int id, bool active, const juce::String& activeTip, const juce::String& inactiveTip) {
        setControlActive(controls[static_cast<std::size_t>(id)].get(), labels[static_cast<std::size_t>(id)].get(),
                         active, activeTip, inactiveTip);
    };

    for (int i = 0; i < tapCount; ++i)
        set(baseCount + i * 4 + 3, s.routing == Routing::stereo,
            "Tap balance in Stereo routing.", "Select Stereo routing to enable per-tap pan.");

    set(tempo, !owner.hostSync.load(),
        "Internal tempo is used when the host does not provide a valid BPM.",
        "Disconnect host transport sync to use internal BPM.");

    set(drive, s.character != Character::digital,
        "Saturation and character drive for Tape, BBD, and Lo-fi.",
        "Select Tape, BBD, or Lo-fi character to enable.");

    const bool hasMod = (s.character == Character::tape || s.character == Character::bbd);
    set(motion, hasMod, "Wow and flutter pitch-modulation depth for Tape and BBD. Wobble Rate sets its speed.",
        "Select Tape or BBD character to enable.");
    set(wobbleRate, hasMod, "Pitch-modulation speed for Tape and BBD. Motion sets its depth.",
        "Select Tape or BBD character to enable.");
    set(wobbleShape, hasMod, "Wobble waveform for Tape and BBD: smooth, stepped, or deterministic random movement.",
        "Select Tape or BBD character to enable.");

    set(highPass, s.hpSlope > 0, "High-pass cutoff inside the feedback loop.", "Set HP Slope above Off to enable.");
    set(lowPass, s.lpSlope > 0, "Low-pass cutoff inside the feedback loop.", "Set LP Slope above Off to enable.");

    set(duckRate, s.duck > 0.0f, "Beat duck period, or release time in Input and Sidechain modes.",
        "Raise Duck amount above 0% to enable.");
    set(duckHold, s.duck > 0.0f, "Holds and extends the ducking envelope before blooming.",
        "Raise Duck amount above 0% to enable.");

    set(swing, s.groove == 0, "Swing timing on off-beat steps.", "Set Groove to Manual to enable Swing.");

    const bool hasGlitch = (s.glitch != Glitch::off);
    set(glitchAmount, hasGlitch, "Wet glitch effect mix.", "Choose Gate, Stutter, or Reverse in Glitch to enable.");
    set(glitchRate, hasGlitch, "Tempo division for the glitch chop.", "Choose Gate, Stutter, or Reverse in Glitch to enable.");

    const bool isStereoPath = (s.routing != Routing::mono);
    set(width, isStereoPath, "Stereo width of the wet signal.", "Select Stereo or Ping Pong routing to enable.");
    set(stereoOffset, isStereoPath, "Sub-millisecond to 20 ms inter-channel Haas timing offset.",
        "Select Stereo or Ping Pong routing to enable.");

    set(diffusionSize, s.diffusion > 0.0f, "Time spacing of the diffusion stages.", "Raise Diffusion above 0% to enable.");
    set(reverseRate, s.reverse > 0.01f, "Length of each reversed chunk, as a host-tempo note division.",
        "Raise Reverse above 0% to enable.");

    const int tapIdx = std::clamp(selectedTap.getSelectedId() - 1, 0, tapCount - 1);
    const auto& tap = s.taps[static_cast<std::size_t>(tapIdx)];
    const int tapBase = tapExtraStart + tapIdx * tapExtraCount;
    set(tapBase + 3, tap.filter, "Per-tap high-pass cutoff.", "Turn on Tap Filter to enable.");
    set(tapBase + 4, tap.filter, "Per-tap low-pass cutoff.", "Turn on Tap Filter to enable.");
    set(tapBase + 6, tap.character == 1 || tap.character == 2, "Per-tap saturation drive.",
        "Select Tape or BBD tap color to enable.");
    set(tapBase + 7, s.morph > 0.0f, "Target step when Pattern Morph is raised.",
        "Raise Pattern Morph above 0% to enable.");
}

void DelayEditor::syncPresetDisplay() {
    const int current = owner.displayedProgram();
    const auto name = owner.currentPresetName();
    const bool modified = owner.isPresetModified();
    if (current == shownProgram && name == shownPresetName && modified == shownModified) return;
    shownProgram = current;
    shownPresetName = name;
    shownModified = modified;
    if (current >= 0 && !modified && name == owner.getProgramName(current)) {
        preset.setSelectedId(current + 1, juce::dontSendNotification);
        if (browser) browser->syncSelection(name, modified);
        return;
    }
    preset.setTextWhenNothingSelected(name.isNotEmpty() ? (name + (modified ? " *" : "")) : juce::String("Custom sound"));
    preset.setSelectedId(0, juce::dontSendNotification);
    if (browser) browser->syncSelection(name, modified);
}

void DelayEditor::timerCallback() {
    syncPresetDisplay();
    const auto s = owner.settings();
    updateControlStates(s);

    undoButton.setEnabled(owner.history.canUndo());
    redoButton.setEnabled(owner.history.canRedo());
    slotA.setToggleState(owner.ab.active() == 0, juce::dontSendNotification);
    slotB.setToggleState(owner.ab.active() == 1, juce::dontSendNotification);

    const float peakLin = owner.outputPeak.exchange(0.0f, std::memory_order_relaxed);
    const float instantDb = (peakLin > 1.0e-6f) ? 20.0f * std::log10(peakLin) : -60.0f;
    if (instantDb > 0.0f) meterClipped = true;
    constexpr float dt = 1.0f / 24.0f;
    meterDb = std::max(instantDb, meterDb - 24.0f * dt);
    if (instantDb >= meterPeakHoldDb) {
        meterPeakHoldDb = instantDb;
        meterPeakHoldTimer = 1.0f;
    }
    if (instantDb < meterPeakHoldDb) {
        meterPeakHoldTimer -= dt;
        if (meterPeakHoldTimer <= 0.0f)
            meterPeakHoldDb = std::max(meterDb, meterPeakHoldDb - 18.0f * dt);
    }

    grid.repaint();
    content.repaint(440, 20, 195, 32);
    content.repaint(894, 108, 272, 74);
    content.repaint(kMeterPanelArea);
    if (lowerPage == 0) {
        content.repaint(scopeFrame.getBounds());
        content.repaint(765, 610, 115, 22);
    }
}

void DelayEditor::addSuiteButton(juce::TextButton& button, const juce::String& id, const juce::String& tip,
                                 const juce::String& title) {
    button.setComponentID(id);
    button.setTooltip(tip);
    button.setTitle(title);
    content.addAndMakeVisible(button);
}

bool DelayEditor::keyPressed(const juce::KeyPress& key) {
    if (key.getModifiers().isCommandDown() &&
        (key.getKeyCode() == 'Z' || key.getTextCharacter() == 'z' || key.getTextCharacter() == 'Z')) {
        if (key.getModifiers().isShiftDown()) owner.history.redo();
        else owner.history.undo();
        pattern.setSelectedId(0, juce::dontSendNotification);
        return true;
    }
    return false;
}

} // namespace oed
