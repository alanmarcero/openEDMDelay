#include "PluginEditor.h"
#include "Theme.h"
#include <BinaryData.h>
#include <cmath>
namespace oed {
namespace {
void text(juce::Graphics& g,const juce::String& value,juce::Rectangle<int> bounds,float size,juce::Colour colour,bool bold=false,juce::Justification align=juce::Justification::centredLeft) {
    g.setColour(colour);g.setFont(juce::FontOptions(size,bold?juce::Font::bold:juce::Font::plain));g.drawText(value,bounds,align);
}
void panel(juce::Graphics& g,juce::Rectangle<int> area,const juce::String& title,juce::Colour accent) {
    const auto r=area.toFloat();g.setGradientFill(juce::ColourGradient(juce::Colour(0xff222d31),r.getTopLeft(),juce::Colour(0xff182125),r.getBottomRight(),false));g.fillRoundedRectangle(r,10);
    g.setColour(theme::kPanelEdge.withAlpha(.65f));g.drawRoundedRectangle(r.reduced(.5f),10,1);
    g.setColour(accent);g.fillRoundedRectangle(static_cast<float>(area.getX()+16),static_cast<float>(area.getY()+18),3,15,1.5f);
    text(g,title,{area.getX()+29,area.getY()+14,area.getWidth()-40,24},13,accent,true);
}
juce::Colour accent(int index) {return index%2==0?theme::kAccentA:theme::kAccentB;}
}
void TapGrid::paint(juce::Graphics& g) {
    const auto s=owner.settings();const float left=32,right=static_cast<float>(getWidth()-14),step=(right-left)/16;
    for(int i=0;i<16;++i) {
        const float x=left+(static_cast<float>(i)+.5f)*step;
        g.setColour(theme::kPanelEdge.withAlpha(i%4==0?.9f:.35f));g.drawVerticalLine(static_cast<int>(x),24.f,static_cast<float>(getHeight()-21));
        text(g,juce::String(i+1),{static_cast<int>(x)-10,0,20,20},10,i%4==0?theme::kText:theme::kTextDim,false,juce::Justification::centred);
    }
    const auto phase=owner.displayedPhase.load()/beatRates[static_cast<std::size_t>(s.rate)];
    const float cursor=left+static_cast<float>(phase-std::floor(phase/16)*16)*step;
    g.setColour(theme::kAccentA.withAlpha(.08f));g.fillRect(cursor-4,23.f,8.f,static_cast<float>(getHeight()-44));
    g.setColour(theme::kAccentA.withAlpha(.55f));g.drawVerticalLine(static_cast<int>(cursor),24.f,static_cast<float>(getHeight()-21));
    for(int i=0;i<tapCount;++i) {
        const auto& tap=s.taps[static_cast<std::size_t>(i)];const float y=30+static_cast<float>(i)*22;
        const auto colour=accent(i).withAlpha(tap.enabled?1.f:.22f);
        text(g,juce::String(i+1).paddedLeft('0',2),{0,static_cast<int>(y)-8,23,17},10,colour,true);
        g.setColour(theme::kBackground.withAlpha(.2f));g.fillRoundedRectangle(left,y-7,right-left,15,3);
        const float x=left+(static_cast<float>(tap.step)-.5f)*step;
        if(tap.enabled) {
            g.setGradientFill(juce::ColourGradient(colour.withAlpha(.02f),left,y,colour.withAlpha(.33f),x,y,false));g.fillRect(left,y-3,x-left,6.f);
            g.setColour(colour.withAlpha(.55f));g.drawLine(left,y,x,y,1);
            for(int echo=1;echo<4;++echo) {
                const float echoX=x+static_cast<float>(echo)*step*2;if(echoX>right)break;
                g.setColour(colour.withAlpha(.13f/static_cast<float>(echo)));g.fillEllipse(echoX-2,y-2,4,4);
            }
        }
        const float radius=3+tap.level*3;g.setColour(colour.withAlpha(tap.enabled?.12f:.04f));g.fillEllipse(x-radius-4,y-radius-4,(radius+4)*2,(radius+4)*2);
        g.setColour(colour);g.fillRoundedRectangle(x-5,y-radius,10,radius*2,3);
    }
    text(g,"DRAG: time + level    /    RIGHT-CLICK: mute",{32,getHeight()-19,getWidth()-40,18},10,theme::kTextDim);
}
void TapGrid::mouseDown(const juce::MouseEvent& e) {
    dragging=juce::jlimit(0,7,static_cast<int>((e.position.y-19)/22));
    const int id=baseCount+dragging*4;
    if(e.mods.isRightButtonDown()){owner.setPlainValue(id,owner.values[static_cast<std::size_t>(id)]->load()>.5f?0.f:1.f);dragging=-1;repaint();return;}
    startLevel=owner.values[static_cast<std::size_t>(id+2)]->load();
    for(int p:{id+1,id+2})owner.parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(p)].id)->beginChangeGesture();
    owner.setPlainValue(id,1);mouseDrag(e);
}
void TapGrid::mouseDrag(const juce::MouseEvent& e) {
    if(dragging<0)return;const int id=baseCount+dragging*4;
    const float width=static_cast<float>(getWidth()-46)/16;
    owner.setPlainValue(id+1,static_cast<float>(juce::jlimit(1,16,static_cast<int>((e.position.x-32)/width)+1)));
    owner.setPlainValue(id+2,juce::jlimit(0.f,100.f,startLevel-static_cast<float>(e.getDistanceFromDragStartY())));repaint();
}
void TapGrid::mouseUp(const juce::MouseEvent&) {
    if(dragging<0)return;const int id=baseCount+dragging*4;
    for(int p:{id+1,id+2})owner.parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(p)].id)->endChangeGesture();dragging=-1;
}
DelayEditor::DelayEditor(DelayProcessor& p):AudioProcessorEditor(p),owner(p),grid(p) {
    setLookAndFeel(&look);artwork=juce::ImageCache::getFromMemory(BinaryData::tapeecho_png,BinaryData::tapeecho_pngSize);
    for(int i=0;i<parameterCount;++i)makeControl(i);
    addAndMakeVisible(grid);addAndMakeVisible(preset);addAndMakeVisible(pattern);
    preset.setComponentID("preset");pattern.setComponentID("pattern");
    for(int i=0;i<p.getNumPrograms();++i)preset.addItem(p.getProgramName(i),i+1);
    preset.setSelectedId(p.getCurrentProgram()+1,juce::dontSendNotification);
    preset.onChange=[this]{owner.setCurrentProgram(preset.getSelectedId()-1);pattern.setSelectedId(0,juce::dontSendNotification);};
    pattern.addItemList(patternNames(),1);pattern.setTextWhenNothingSelected("Pattern / Custom");pattern.setTooltip("Load tap positions, levels and stereo pans. Grid rate and effects are preserved.");
    pattern.onChange=[this]{owner.applyPattern(pattern.getSelectedId()-1);};
    setSize(1200,890);startTimerHz(24);
}
DelayEditor::~DelayEditor(){stopTimer();setLookAndFeel(nullptr);}
void DelayEditor::makeControl(int i) {
    const auto& spec=parameterSpecs()[static_cast<std::size_t>(i)];
    const auto colour=i>=baseCount?accent((i-baseCount)/4):(i==highPass||i==hpSlope?theme::kAccentA:theme::kAccentB);
    auto label=std::make_unique<juce::Label>();label->setText(spec.name,juce::dontSendNotification);label->setJustificationType(juce::Justification::centred);label->setFont(juce::FontOptions(11.5f));label->setColour(juce::Label::textColourId,theme::kTextDim);addAndMakeVisible(*label);labels[static_cast<std::size_t>(i)]=std::move(label);
    if(i==bypass||(i>=baseCount&&(i-baseCount)%4==0)) {
        auto c=std::make_unique<juce::TextButton>(i==bypass?"BYPASS":"TAP "+juce::String((i-baseCount)/4+1).paddedLeft('0',2));
        c->setClickingTogglesState(true);c->setColour(juce::TextButton::buttonOnColourId,i==bypass?theme::kAccentB:colour);
        c->setComponentID(spec.id);addAndMakeVisible(*c);buttons.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(owner.parameters,spec.id,*c));controls[static_cast<std::size_t>(i)]=std::move(c);
    } else if(spec.choices.isNotEmpty()) {
        auto c=std::make_unique<juce::ComboBox>();c->addItemList(juce::StringArray::fromTokens(spec.choices,"|",""),1);c->setColour(juce::ComboBox::outlineColourId,colour);c->setComponentID(spec.id);addAndMakeVisible(*c);combos.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(owner.parameters,spec.id,*c));controls[static_cast<std::size_t>(i)]=std::move(c);
    } else {
        auto c=std::make_unique<juce::Slider>();c->setLookAndFeel(&look);c->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);c->setTextBoxStyle(juce::Slider::TextBoxBelow,false,86,22);c->setColour(juce::Slider::rotarySliderFillColourId,colour);c->setComponentID(spec.id);c->setDoubleClickReturnValue(true,spec.initial);
        const bool step=i>=baseCount&&(i-baseCount)%4==1,pan=i>=baseCount&&(i-baseCount)%4==3;
        if(step){c->setSliderStyle(juce::Slider::IncDecButtons);c->setTextBoxStyle(juce::Slider::TextBoxLeft,false,32,24);c->setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);}
        if(i==drive||i==motion){c->setSliderStyle(juce::Slider::LinearHorizontal);c->setTextBoxStyle(juce::Slider::TextBoxRight,false,38,22);c->setColour(juce::Slider::trackColourId,colour);c->setColour(juce::Slider::thumbColourId,colour);}
        if(pan){c->setSliderStyle(juce::Slider::LinearHorizontal);c->setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);c->setColour(juce::Slider::trackColourId,colour);c->setColour(juce::Slider::thumbColourId,colour);}
        addAndMakeVisible(*c);sliders.push_back(std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(owner.parameters,spec.id,*c));
        c->textFromValueFunction=[i,step,pan,unit=spec.unit](double v){if(step)return juce::String(static_cast<int>(v));if(pan)return std::abs(v)<.01?juce::String("C"):juce::String(static_cast<int>(std::round(std::abs(v))))+(v<0?" L":" R");if(i==highPass||i==lowPass)return v>=1000?juce::String(v/1000,2)+" kHz":juce::String(static_cast<int>(std::round(v)))+" Hz";return (i==output?juce::String(std::abs(v)<.005?0:v,1):juce::String(static_cast<int>(std::round(v))))+(unit=="%"?"%":" "+unit);};
        c->valueFromTextFunction=[i](const juce::String& t){const double value=t.getDoubleValue();return (i==highPass||i==lowPass)&&t.containsIgnoreCase("k")?value*1000:value;};
        c->updateText();
        c->setTooltip(spec.name+". Drag to adjust; double-click to reset. Host automatable.");controls[static_cast<std::size_t>(i)]=std::move(c);
    }
    auto* tooltipClient=dynamic_cast<juce::SettableTooltipClient*>(controls[static_cast<std::size_t>(i)].get());
    if(tooltipClient) {
        if(i==decay)tooltipClient->setTooltip("Progressive level falloff across the tap pattern. Feedback controls phrase repeats.");
        if(i==feedback)tooltipClient->setTooltip("Repeat the full phrase from the last active tap. Maximum 97%.");
        if(i==gridRate)tooltipClient->setTooltip("Each grid step is this note length. T = triplet; D = dotted. Always tempo-synced.");
        if(i==tempo)tooltipClient->setTooltip("Internal tempo is used only when the host does not provide a valid BPM.");
        if(i==motion)tooltipClient->setTooltip("Beat-related wow and flutter in Tape character.");
        if(i==drive)tooltipClient->setTooltip("Saturation amount for Tape and BBD characters.");
        if(i==duckMode)tooltipClient->setTooltip("Beat: rhythmic wet ducking. Input: duck echoes while the incoming audio is loud. Rate sets recovery.");
        if(i==routing)tooltipClient->setTooltip("Stereo uses tap pan. Ping Pong alternates taps and crossfeeds each phrase. Mono collapses the wet branch.");
        if(i==glitch)tooltipClient->setTooltip("Wet effects only: rhythmic Gate, previous-slice Stutter, or Reverse. Off preserves the echoes.");
        if(i>=baseCount&&(i-baseCount)%4==3)tooltipClient->setTooltip("Tap balance in Stereo routing. Ping Pong positions taps automatically; Mono is centered.");
    }
}
void DelayEditor::place(int id,juce::Rectangle<int> r,bool showLabel) {
    controls[static_cast<std::size_t>(id)]->setBounds(r);auto& label=*labels[static_cast<std::size_t>(id)];label.setVisible(showLabel);label.setBounds(r.getX()-6,r.getBottom()+1,r.getWidth()+12,19);
}
void DelayEditor::resized() {
    preset.setBounds(850,26,230,35);place(bypass,{1093,26,86,35},false);pattern.setBounds(645,109,195,29);grid.setBounds(39,149,801,220);
    place(routing,{896,267,132,28},false);place(character,{1040,267,124,28},false);
    place(drive,{901,321,119,25});place(motion,{1038,321,119,25});
    for(int i=0;i<tapCount;++i) {
        const int x=31+i*145,p=baseCount+i*4;
        place(p,{x+2,405,70,26},false);place(p+1,{x+79,405,57,26},false);
        place(p+2,{x+26,438,88,99},false);place(p+3,{x+13,545,116,18},false);
    }
    place(gridRate,{37,639,128,30},false);
    place(feedback,{30,688,88,119});place(decay,{122,688,88,119});place(tempo,{213,688,78,119});
    place(highPass,{338,659,102,117});place(lowPass,{466,659,102,117});
    place(hpSlope,{332,807,117,28},false);place(lpSlope,{460,807,117,28},false);
    place(duckMode,{627,639,113,29},false);place(glitch,{754,639,113,29},false);
    place(duck,{634,680,100,108});place(glitchAmount,{760,680,100,108});
    place(duckRate,{627,807,113,28},false);place(glitchRate,{754,807,113,28},false);
    place(mix,{916,688,87,119});place(width,{1007,688,77,119});place(output,{1090,688,77,119});
}
void DelayEditor::paint(juce::Graphics& g) {
    g.fillAll(theme::kBackground);
    g.setGradientFill(juce::ColourGradient(theme::kAccentA.withAlpha(.12f),0,0,theme::kAccentB.withAlpha(.03f),1200,890,false));g.fillAll();
    g.setColour(theme::kAccentA.withAlpha(.75f));g.fillRect(20,0,290,2);g.setColour(theme::kAccentB.withAlpha(.6f));g.fillRect(880,0,300,2);
    text(g,"openEDM",{26,20,163,37},29,theme::kText,true);text(g,"Delay",{151,20,124,37},29,theme::kAccentA,true);
    text(g,"RHYTHM IN REPEAT",{28,61,270,17},10.5f,theme::kTextDim);
    const bool synced=owner.hostSync.load();
    g.setColour(synced?theme::kAccentA:theme::kAccentB);g.fillEllipse(595,36,6,6);
    text(g,juce::String(owner.displayedBpm.load(),1)+" BPM  /  "+(synced?"HOST":"INTERNAL"),{612,26,222,29},12.5f,theme::kTextDim);
    panel(g,{20,96,840,278},"TAP FIELD",theme::kAccentA);text(g,"16 STEPS  /  8 VOICES",{344,113,235,20},10.5f,theme::kTextDim);
    panel(g,{880,96,300,278},"",theme::kAccentB);
    {juce::Graphics::ScopedSaveState state(g);juce::Path clip;clip.addRoundedRectangle(891,104,278,151,6);g.reduceClipRegion(clip);g.drawImage(artwork,{891.f,91.f,278.f,185.3f},juce::RectanglePlacement::fillDestination);}
    text(g,"ROUTING",{899,246,120,17},9,theme::kTextDim,true);text(g,"CHARACTER",{1042,246,120,17},9,theme::kTextDim,true);
    g.setColour(theme::kPanel);g.fillRoundedRectangle(20,392,1160,184,10);
    for(int i=0;i<8;++i) {
        const int x=31+i*145;
        if(i>0){g.setColour(theme::kPanelEdge.withAlpha(.6f));g.drawVerticalLine(x-6,406,562);}
        text(g,"L",{x+2,544,13,20},9,theme::kTextDim);text(g,"R",{x+128,544,13,20},9,theme::kTextDim);
    }
    panel(g,{20,596,280,250},"TIME & REPEATS",theme::kAccentA);
    text(g,"STEP LENGTH",{177,642,106,22},10,theme::kTextDim);
    panel(g,{315,596,280,250},"ECHO FILTERS",theme::kAccentB);
    text(g,"IN THE FEEDBACK LOOP",{335,634,236,17},10,theme::kTextDim);
    panel(g,{610,596,280,250},"DUCK & DISRUPT",theme::kAccentFx);
    panel(g,{905,596,275,250},"OUTPUT",theme::kAccentOut);
    const float peak=juce::jlimit(0.f,1.f,owner.outputPeak.load());
    g.setColour(theme::kTrack);g.fillRoundedRectangle(925,648,235,5,2);
    g.setColour(theme::kAccentA);g.fillRoundedRectangle(925,648,235*peak,5,2);
    text(g,"WET",{926,624,80,18},10,theme::kTextDim);text(g,"0 dB",{1109,624,49,18},10,theme::kTextDim,false,juce::Justification::centredRight);
    const float reduction=1-owner.duckMeter.load();
    text(g,"Duck "+juce::String(static_cast<int>(std::round(reduction*100)))+"%",{628,854,150,20},10,theme::kAccentFx);
    text(g,"BEAT SYNC ONLY   /   T = triplet   /   D = dotted",{25,854,470,20},10,theme::kTextDim);
    text(g,"ALAN MARCERO  /  openEDM",{910,854,267,20},10,theme::kTextDim,false,juce::Justification::centredRight);
}
void DelayEditor::timerCallback() {
    const int current=owner.getCurrentProgram();if(current!=shownProgram){shownProgram=current;preset.setSelectedId(current+1,juce::dontSendNotification);}
    const auto s=owner.settings();for(int i=0;i<8;++i)controls[static_cast<std::size_t>(baseCount+i*4+3)]->setEnabled(s.routing==Routing::stereo);
    controls[tempo]->setEnabled(!owner.hostSync.load());controls[motion]->setEnabled(s.character==Character::tape);
    controls[drive]->setEnabled(s.character==Character::tape||s.character==Character::bbd);grid.repaint();repaint();
}
}
