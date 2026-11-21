#include "PluginProcessor.h"
#include "suite/PresetBrowser.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
using namespace oed;
void require(bool x,const char* why){if(!x)throw std::runtime_error(why);}
struct Host:juce::AudioPlayHead{PositionInfo info;juce::Optional<PositionInfo> getPosition()const override{return info;}};
void clean(DelayProcessor& p){for(std::size_t i=0;i<parameterSpecs().size();++i)p.setPlainValue(static_cast<int>(i),parameterSpecs()[i].initial);p.setPlainValue(gridRate,7);p.setPlainValue(routing,0);p.setPlainValue(character,0);p.setPlainValue(mix,100);p.setPlainValue(feedback,0);p.setPlainValue(decay,0);p.setPlainValue(hpSlope,0);p.setPlainValue(lpSlope,0);p.setPlainValue(duck,0);for(int i=0;i<8;++i){p.setPlainValue(baseCount+i*4,i==0?1:0);p.setPlainValue(baseCount+i*4+1,1);p.setPlainValue(baseCount+i*4+2,100);p.setPlainValue(baseCount+i*4+3,0);}}
void audio(){
    DelayProcessor p;clean(p);Host host;host.info.setBpm(120);host.info.setIsPlaying(true);host.info.setPpqPosition(0);p.setPlayHead(&host);p.prepareToPlay(48000,256);
    juce::AudioBuffer<float> b(2,24001);b.clear();b.setSample(0,0,1);juce::MidiBuffer m;p.processBlock(b,m);
    require(std::abs(b.getSample(0,24000)-1)<.0001f,"Processor does not follow host beat");require(p.hostSync.load(),"Host sync status incorrect");
    p.reset();p.setPlayHead(nullptr);p.setPlainValue(tempo,90);b.setSize(2,32001);b.clear();b.setSample(0,0,1);p.processBlock(b,m);
    require(std::abs(b.getSample(0,32000)-1)<.0001f&&!p.hostSync.load(),"Internal fallback timing/status incorrect");
    p.setPlainValue(bypass,1);b.clear();b.setSample(0,0,.7f);p.processBlock(b,m);require(b.getSample(0,0)==.7f,"Bypass changes dry input");
    p.setPlainValue(bypass,0);b.clear();p.processBlock(b,m);require(b.getMagnitude(0,b.getNumSamples())==0,"Bypass resurrects stale echoes");
    require(p.getLatencySamples()==0,"Dry path latency");
    b.setSize(2,4800);b.clear();p.processBlock(b,m);
    const auto start=p.displayedPhase.load();p.processBlock(b,m);
    require(std::abs(p.displayedPhase.load()-start-.15)<1e-6,"Internal reel does not follow 90 BPM without a host");
}
void presetWhileBypassed(){
    DelayProcessor p;clean(p);p.prepareToPlay(8000,64);juce::MidiBuffer midi;juce::AudioBuffer<float> first(2,64);first.clear();
    p.setPlainValue(bypass,1);p.processBlock(first,midi);p.setCurrentProgram(1);clean(p);
    first.setSample(0,0,1);p.processBlock(first,midi);juce::AudioBuffer<float> tail(2,4000);tail.clear();p.processBlock(tail,midi);
    require(std::abs(tail.getSample(0,3936)-1)<.001f,"Preset while bypassed loses first active block");
}
void malformedState(){
    for(const juce::String bad:{"garbage","nan","1e1000","101","-1"}) {
        DelayProcessor p;p.setPlainValue(mix,73);auto tree=p.parameters.copyState();
        for(auto child:tree)if(child.getProperty("id").toString()=="mix")child.setProperty("value",bad,nullptr);
        juce::MemoryBlock block;auto xml=tree.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,block);p.setStateInformation(block.getData(),static_cast<int>(block.getSize()));
        require(p.values[mix]->load()==73,"Malformed parameter state was accepted");
    }
}
static void legacyAndPerformanceState(){
    DelayProcessor p;auto legacy=p.parameters.copyState();
    for(int i=legacy.getNumChildren();--i>=0;) {
        const auto id=legacy.getChild(i).getProperty("id").toString();
        bool original=false;for(int j=0;j<baseCount+tapCount*4;++j)original=original||parameterSpecs()[static_cast<std::size_t>(j)].id==id;
        if(!original)legacy.removeChild(i,nullptr);
        else if(id=="mix")legacy.getChild(i).setProperty("value",73,nullptr);
    }
    juce::MemoryBlock block;auto xml=legacy.createXml();juce::AudioProcessor::copyXmlToBinary(*xml,block);
    p.setPlainValue(mix,15);p.setStateInformation(block.getData(),static_cast<int>(block.getSize()));
    require(p.displayedProgram()==-1,"Legacy session is mislabeled with a replacement factory name");
    require(std::abs(p.values[mix]->load()-73)<.0001f,"Legacy state rejected after adding parameters");
    p.setPlainValue(swing,50);p.setPlainValue(patternMorph,75);p.setPlainValue(freeze,1);
    p.setPlainValue(diffusion,35);p.setPlainValue(diffusionSize,24);p.setPlainValue(wobbleRate,2.5f);p.setPlainValue(wobbleShape,3);
    p.setPlainValue(shimmer,45);p.setPlainValue(stereoOffset,8.5f);p.setPlainValue(duckHold,60);
    p.setPlainValue(tapExtraStart,35);p.setPlainValue(tapExtraStart+1,3);
    const auto settings=p.settings();
    require(settings.swing==.5f&&settings.morph==.75f&&settings.freeze,"Performance parameters disconnected");
    require(std::abs(settings.diffusion-.35f)<.0001f&&std::abs(settings.diffusionSize-24)<.0001f&&std::abs(settings.wobbleRate-2.5f)<.0001f&&settings.wobbleShape==WobbleShape::randomWalk,"Diffusion or wobble parameters disconnected");
    require(std::abs(settings.shimmer-.45f)<.0001f&&std::abs(settings.stereoOffset-8.5f)<.0001f&&std::abs(settings.duckHold-.6f)<.0001f,"Shimmer, stereo offset, or duck hold parameters disconnected");
    require(std::abs(settings.taps[0].probability-.35f)<.0001f&&settings.taps[0].ratchets==4,"Tap performance parameters disconnected");
    clean(p);p.setPlainValue(tapExtraStart+7,16);p.setPlainValue(patternMorph,100);
    require(p.getTailLengthSeconds()>=8,"Host tail excludes morphed pattern");
}
static void filteredTapTail() {
    DelayProcessor p;clean(p);p.setPlainValue(gridRate,0);
    p.setPlainValue(tapExtraStart+2,1);p.setPlainValue(tapExtraStart+4,20);
    p.prepareToPlay(8000,128);
    const int tail=static_cast<int>(std::ceil(p.getTailLengthSeconds()*8000));
    juce::AudioBuffer<float> audioBuffer(2,tail+8000);audioBuffer.clear();audioBuffer.setSample(0,0,1);
    juce::MidiBuffer midi;p.processBlock(audioBuffer,midi);
    require(audioBuffer.getMagnitude(0,tail,audioBuffer.getNumSamples()-tail)<1.e-5f,"Host tail excludes audible tap filter settling");
}
static void diffusionHostTail() {
    DelayProcessor p;clean(p);p.setPlainValue(gridRate,0);
    const double dryTail=p.getTailLengthSeconds();
    p.setPlainValue(diffusion,100);p.setPlainValue(diffusionSize,30);
    require(p.getTailLengthSeconds()>dryTail+.5,"Host tail excludes the diffusion network decay");
}
static void typedValues() {
    DelayProcessor p;
    for(auto* parameter:p.getParameters()) {
        if(dynamic_cast<juce::AudioParameterFloat*>(parameter)==nullptr) continue;
        for(const float position:{0.f,.13f,.5f,.77f,1.f,parameter->getDefaultValue()}) {
            const auto shown=parameter->getText(position,0);
            require(parameter->getText(parameter->getValueForText(shown),0)==shown,("Typed text does not round-trip: "+parameter->getName(64)+" "+shown).toRawUTF8());
        }
    }
    const auto valueFor=[&p](int id,const juce::String& typed){auto* param=p.parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(id)].id);return param->convertFrom0to1(param->getValueForText(typed));};
    const auto textFor=[&p](int id,float value){auto* param=p.parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(id)].id);return param->getText(param->convertTo0to1(value),0);};
    require(std::abs(valueFor(mix,"50")-50)<.01f&&std::abs(valueFor(mix,"50%")-50)<.01f,"Percent entry rejects the % sign");
    require(std::abs(valueFor(highPass,"5k")-5000)<1&&std::abs(valueFor(tapExtraStart+3,"5 kHz")-5000)<1,"Hz entry ignores k");
    require(std::abs(valueFor(output,"+3 dB")-3)<.01f&&std::abs(valueFor(diffusionSize,"0.012 s")-12)<.01f,"dB or s entry failed");
    require(std::abs(valueFor(baseCount+3,"40 L")+40)<.01f&&std::abs(valueFor(baseCount+3,"C"))<.01f,"Pan entry failed");
    require(textFor(highPass,850)=="850 Hz"&&textFor(highPass,6800)=="6.80 kHz"&&textFor(output,3)=="+3.0 dB"&&textFor(output,-.01f)=="0.0 dB","Frequency or gain format");
    require(textFor(diffusionSize,4.5f)=="4.5 ms"&&textFor(diffusionSize,16)=="16 ms"&&textFor(stereoOffset,4)=="+4.0 ms"&&textFor(baseCount+3,-40)=="40 L"&&textFor(baseCount+3,.3f)=="C","Time or pan format");
}
void recall(){legacyAndPerformanceState();DelayProcessor p;for(int i=0;i<parameterCount;++i){const auto& s=parameterSpecs()[static_cast<std::size_t>(i)];p.setPlainValue(i,s.min+(s.max-s.min)*.73f);}juce::MemoryBlock state;p.getStateInformation(state);DelayProcessor restored;restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));for(int i=0;i<parameterCount;++i)require(p.values[static_cast<std::size_t>(i)]->load()==restored.values[static_cast<std::size_t>(i)]->load(),"Session state loses parameter");restored.setStateInformation("invalid",7);require(restored.values[mix]->load()==p.values[mix]->load(),"Malformed state resets parameters");}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* c:root.getChildren())if(auto* result=find(*c,id))return result;return nullptr;}
static void checkNoOverlap(juce::Component& surface) {
    std::vector<juce::Rectangle<int>> areas;
    for(auto* c:surface.getChildren()) {
        if(!c->isVisible()) continue;
        const bool isControl=dynamic_cast<juce::Slider*>(c)||dynamic_cast<juce::ComboBox*>(c)||dynamic_cast<juce::TextButton*>(c);
        if(!isControl) continue;
        const auto b=c->getBounds();
        require(juce::Rectangle<int>(0,0,1200,850).contains(b),("Control outside design bounds: "+c->getComponentID()).toRawUTF8());
        if(dynamic_cast<juce::TextButton*>(c)||dynamic_cast<juce::ComboBox*>(c))
            require(b.getHeight()>=36,("Button/combo under 36px: "+c->getComponentID()).toRawUTF8());
        if(auto* s=dynamic_cast<juce::Slider*>(c); s && s->getSliderStyle()==juce::Slider::RotaryHorizontalVerticalDrag)
            require(b.getWidth()>=60&&b.getHeight()>=60,("Dial too small: "+c->getComponentID()).toRawUTF8());
        for(const auto& prev:areas)
            require(!prev.intersects(b),("Overlapping control: "+c->getComponentID()).toRawUTF8());
        areas.push_back(b);
    }
}
static void pagedLayoutAndScale() {
    DelayProcessor p;std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    require(editor->getWidth()==1200&&editor->getHeight()==850,("Default editor size is not 1200x850: "+juce::String(editor->getWidth())+"x"+juce::String(editor->getHeight())).toRawUTF8());
    auto* pageMain=dynamic_cast<juce::TextButton*>(find(*editor,"pageMain"));
    auto* pageGroove=dynamic_cast<juce::TextButton*>(find(*editor,"pageGroove"));
    auto* pageFx=dynamic_cast<juce::TextButton*>(find(*editor,"pageFx"));
    auto* tap=dynamic_cast<juce::ComboBox*>(find(*editor,"performanceTap"));
    require(pageMain&&pageGroove&&pageFx&&tap,"Page tabs or tap selector missing");
    auto* surface=pageMain->getParentComponent();
    require(surface!=nullptr,"Editor content surface missing");

    pageMain->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    checkNoOverlap(*surface);
    for(const int id:std::initializer_list<int>{gridRate,feedback,decay,tempo,highPass,lowPass,hpSlope,lpSlope,duckMode,duck,duckRate,duckHold,mix,width,stereoOffset,output})
        require(find(*editor,parameterSpecs()[static_cast<std::size_t>(id)].id)->isVisible(),"Main page control is hidden");

    pageGroove->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    for(int index=0;index<tapCount;++index) {
        tap->setSelectedId(index+1,juce::sendNotificationSync);
        checkNoOverlap(*surface);
        for(int field=0;field<tapExtraCount;++field)
            require(find(*editor,parameterSpecs()[static_cast<std::size_t>(tapExtraStart+index*tapExtraCount+field)].id)->isVisible(),"Selected tap control is hidden");
    }

    pageFx->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    checkNoOverlap(*surface);
    for(const int id:std::initializer_list<int>{diffusion,diffusionSize,shimmer,glitch,glitchAmount,glitchRate,reverse,reverseRate})
        require(find(*editor,parameterSpecs()[static_cast<std::size_t>(id)].id)->isVisible(),"FX page control is hidden");

    editor->setSize(840,595);
    juce::MemoryBlock state;p.getStateInformation(state);
    DelayProcessor restored;restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));
    std::unique_ptr<juce::AudioProcessorEditor> editor2(restored.createEditor());
    require(editor2->getWidth()==840&&editor2->getHeight()==595,"Saved editor scale was not restored");
}
static void bankAudio() {
    DelayProcessor p;Host host;host.info.setIsPlaying(true);host.info.setBpm(160);host.info.setPpqPosition(0);
    constexpr int rate=48000,frames=rate*6;
    p.setPlayHead(&host);p.prepareToPlay(rate,frames);
    juce::AudioBuffer<float> wave(2,frames);juce::MidiBuffer midi;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if(const char* path=std::getenv("OPENEDMDELAY_IMPULSE_RENDER")) {
        auto stream=juce::File(path).createOutputStream();
        require(stream!=nullptr,"Cannot open impulse render");
        stream->setPosition(0);stream->truncate();
        std::unique_ptr<juce::OutputStream> output=std::move(stream);
        const auto options=juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(2).withBitsPerSample(24);
        writer=juce::WavAudioFormat{}.createWriterFor(output,options);
        require(writer!=nullptr,"Cannot write impulse WAV");
    }
    std::set<uint64_t> signatures;double minimum=6.0,maximum=0;
    for(int i=0;i<p.getNumPrograms();++i) {
        p.setCurrentProgram(i);wave.clear();wave.setSample(0,0,.8f);wave.setSample(1,0,.5f);p.processBlock(wave,midi);
        int first=-1;uint64_t signature=1469598103934665603ULL;
        for(int s=32;s<wave.getNumSamples();++s) {
            for(int c=0;c<2;++c) {
                const float v=wave.getSample(c,s);
                require(std::isfinite(v),"Non-finite factory impulse response");
                if(first<0 && std::abs(v)>0.00005f)first=s;
                signature=(signature^static_cast<uint32_t>(std::lround(v*100000.0f)))*1099511628211ULL;
            }
        }
        require(first>=0,p.getProgramName(i).toRawUTF8());
        minimum=std::min(minimum,static_cast<double>(first)/rate);maximum=std::max(maximum,static_cast<double>(first)/rate);
        if(writer) require(writer->writeFromAudioSampleBuffer(wave,0,frames),"Impulse export failed");
        require(signatures.insert(signature).second,"Two factory programs produced identical wet echoes");
    }
    std::cout<<p.getNumPrograms()<<" distinct impulse responses at 160 BPM, first echoes "<<minimum<<" to "<<maximum<<" s\n";
    require(patternNames().size()==12,"Rhythm template count changed");
    std::set<std::array<int,tapCount>> shapes;
    p.setPlainValue(feedback,64);
    for(int i=0;i<patternNames().size();++i) {
        p.applyPattern(i);const auto sound=p.settings();std::array<int,tapCount> steps{};
        for(std::size_t t=0;t<steps.size();++t)steps[t]=sound.taps[t].enabled?sound.taps[t].step:0;
        require(shapes.insert(steps).second,"Duplicate rhythm template");
        require(std::abs(sound.feedback-.64f)<.001f,"Loading a rhythm changed echo feedback");
    }
    p.setPlayHead(nullptr);
}

static void checkHitTargets(juce::Component& surface,juce::Component& parent) {
    for(auto* c:parent.getChildren()) {
        if(!c->isVisible())continue;
        if(dynamic_cast<juce::TextButton*>(c)||dynamic_cast<juce::ComboBox*>(c)) {
            const auto area=surface.getLocalArea(c,c->getLocalBounds());
            require(area.getWidth()>=40&&area.getHeight()>=36,c->getComponentID().toRawUTF8());
            auto* hit=surface.getComponentAt(area.getCentre());
            require(hit==c||c->isParentOf(hit),"A control is covered by another component");
        }
        checkHitTargets(surface,*c);
    }
}

void ui(const juce::File& destination){
    DelayProcessor p;p.prepareToPlay(48000,256);std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
    e->setVisible(true);
    auto* pageMain=dynamic_cast<juce::TextButton*>(find(*e,"pageMain"));
    auto* pageGroove=dynamic_cast<juce::TextButton*>(find(*e,"pageGroove"));
    auto* pageFx=dynamic_cast<juce::TextButton*>(find(*e,"pageFx"));
    require(pageMain&&pageGroove&&pageFx,"Lower page tabs missing");
    auto* surface=pageMain->getParentComponent();
    require(surface!=nullptr,"Editor content surface missing");
    checkHitTargets(*surface,*surface);
    int menuWidth=0,menuHeight=0;e->getLookAndFeel().getIdealPopupMenuItemSize("Signal Glass",false,0,menuWidth,menuHeight);
    require(menuHeight>=36,"Preset menu rows are too small");
    auto* browser=dynamic_cast<juce::ComboBox*>(find(*e,"preset"));
    auto* previous=dynamic_cast<juce::TextButton*>(find(*e,"prevPreset"));
    auto* next=dynamic_cast<juce::TextButton*>(find(*e,"nextPreset"));
    require(browser&&previous&&next,"Missing preset browser controls");
    require(previous->getWidth()==40&&previous->getHeight()==40&&next->getWidth()==40&&next->getHeight()==40,"Preset step buttons must be 40x40");
    require(browser->getRootMenu()->getNumItems()==12,"Preset menu should group programs by delay type");
    const auto startName=p.currentPresetName();
    previous->onClick();require(p.currentPresetName()!=startName,"Previous failed to step");
    next->onClick();require(p.currentPresetName()==startName,"Next failed to wrap back");
    browser->setSelectedId(13,juce::sendNotificationSync);
    require(p.getCurrentProgram()==12,"Preset menu did not update the processor");
    browser->setSelectedId(1,juce::sendNotificationSync);
    auto* saveBtn=dynamic_cast<juce::TextButton*>(find(*e,"presetSave"));
    require(saveBtn!=nullptr&&saveBtn->getWidth()>=72,"Save button is too narrow for Replace? confirm");
    for(int tap=0;tap<tapCount;++tap){
        auto* panSlider=dynamic_cast<juce::Slider*>(find(*e,parameterSpecs()[static_cast<std::size_t>(baseCount+tap*4+3)].id));
        require(panSlider&&panSlider->isDoubleClickReturnEnabled()&&std::abs(panSlider->getDoubleClickReturnValue())<1e-6,"Pan double-click does not reset to centre");
    }
    auto* patternBox=dynamic_cast<juce::ComboBox*>(find(*e,"pattern"));
    auto* slotB=dynamic_cast<juce::TextButton*>(find(*e,"slotB"));
    auto* copyAB=dynamic_cast<juce::TextButton*>(find(*e,"copyAtoB"));
    auto* slotA=dynamic_cast<juce::TextButton*>(find(*e,"slotA"));
    require(patternBox&&slotA&&slotB&&copyAB,"A/B or pattern controls missing");
    slotB->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    browser->setSelectedId(13,juce::sendNotificationSync);
    patternBox->setSelectedId(2,juce::sendNotificationSync);
    copyAB->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    require(patternBox->getSelectedId()==0&&browser->getSelectedId()==1,"Copy A>B did not sync pattern and preset display");
    slotA->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    p.outputPeak.store(2.0f,std::memory_order_relaxed);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);
    auto countClipRed=[&]{
        const auto shot=e->createComponentSnapshot({925,638,235,10});
        int red=0;
        for(int y=0;y<shot.getHeight();++y) for(int x=0;x<shot.getWidth();++x){
            const auto c=shot.getPixelAt(x,y);
            if(c.getRed()>200&&c.getGreen()<100&&c.getBlue()<100) ++red;
        }
        return red;
    };
    require(countClipRed()>20,"Output meter did not latch clip state");
    const juce::Point<int> meterPt{1040,630};
    auto* meterHit=e->getComponentAt(meterPt);
    require(meterHit==e.get(),"Content intercepts mouse clicks above output meter");
    meterHit->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(),meterPt.toFloat(),juce::ModifierKeys(),0.0f,0.0f,0.0f,0.0f,0.0f,meterHit,meterHit,juce::Time::getCurrentTime(),meterPt.toFloat(),juce::Time::getCurrentTime(),1,false));
    require(countClipRed()==0,"Clicking output meter did not clear clip latch");
    auto* initialMix=find(*e,"mix");require(initialMix!=nullptr,"Initial mix control missing");bool initialUnit=false;
    for(auto* child:initialMix->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))initialUnit=label->getText()=="28%";
    require(initialUnit,"Initial knob value is missing its unit");
    for(int i=0;i<parameterCount;++i){const auto& spec=parameterSpecs()[static_cast<std::size_t>(i)];auto* c=find(*e,spec.id);require(c!=nullptr,("Missing control: "+spec.id).toRawUTF8());if(auto* s=dynamic_cast<juce::Slider*>(c))s->setValue(spec.min+(spec.max-spec.min)*.6,juce::sendNotificationSync);else if(auto* box=dynamic_cast<juce::ComboBox*>(c))box->setSelectedId(box->getNumItems(),juce::sendNotificationSync);else if(auto* button=dynamic_cast<juce::Button*>(c))button->setToggleState(true,juce::sendNotificationSync);else require(false,"Unknown control");}
    require(p.values[mix]->load()==60,"Mix knob disconnected");require(p.values[baseCount+1]->load()==10,"Tap step disconnected");require(p.values[hpSlope]->load()==4,"Slope selector disconnected");
    require(p.getNumPrograms()==72,"Factory bank must contain 72 programs");
    require(rateNames().size()==16,"EDM grid must expose 16 host-synced divisions");
    juce::AudioBuffer<float> b(2,256);b.clear();juce::MidiBuffer midi;
    Host host;host.info.setBpm(128);host.info.setPpqPosition(1.35);host.info.setIsPlaying(true);p.setPlayHead(&host);
    std::array<bool,16> usedDivisions{};
    for(int index=0;index<p.getNumPrograms();++index) {
        p.setCurrentProgram(index);p.processBlock(b,midi);
        require(p.hostSync.load(),"A factory preset lost host sync");
        require(p.values[gridRate]->load()>=0&&p.values[gridRate]->load()<rateNames().size(),"Preset uses an invalid grid division");
        usedDivisions[static_cast<std::size_t>(p.values[gridRate]->load())]=true;
        bool activeTap=false;for(int i=0;i<tapCount;++i)activeTap=activeTap||p.values[static_cast<std::size_t>(baseCount+i*4)]->load()>.5f;
        require(activeTap,"EDM factory preset has no active taps");
    }
    require(std::all_of(usedDivisions.begin(),usedDivisions.end(),[](bool used){return used;}),"Factory bank does not use every synchronized grid division");
    require(destination.createDirectory().wasOk(),"Screenshot directory failed");
    const auto save=[&](const juce::String& name){auto out=destination.getChildFile(name).createOutputStream();require(out!=nullptr,"Screenshot open failed");out->setPosition(0);out->truncate();require(juce::PNGImageFormat{}.writeImageToStream(e->createComponentSnapshot(e->getLocalBounds(),true,1.5f),*out),"Screenshot failed");};
    const auto reelImage=[&]{p.processBlock(b,midi);return e->createComponentSnapshot({918,110,68,68});};
    host.info.setPpqPosition(-2);const auto downbeat=reelImage();
    host.info.setBpm(141);host.info.setPpqPosition(7.5);const auto offbeat=reelImage();
    int changed=0;for(int y=0;y<68;++y)for(int x=0;x<68;++x)if(downbeat.getPixelAt(x,y)!=offbeat.getPixelAt(x,y))++changed;
    require(changed>80&&p.displayedBpm.load()==141,"SVG reel does not follow host seeks and tempo");
    host.info.setIsPlaying(false);host.info.setPpqPosition(8);const auto frozen=reelImage();
    for(int y=0;y<68;++y)for(int x=0;x<68;++x)require(frozen.getPixelAt(x,y)==offbeat.getPixelAt(x,y),"Stopped SVG reel keeps advancing");
    host.info.setIsPlaying(true);host.info.setPpqPosition(1.35);host.info.setBpm(128);
    p.setCurrentProgram(0);pageMain->triggerClick();p.processBlock(b,midi);juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay.png");
    p.setCurrentProgram(0);p.setPlainValue(diffusion,65);p.setPlainValue(diffusionSize,20);pageGroove->triggerClick();p.processBlock(b,midi);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay-performance.png");
    p.setCurrentProgram(43);pageFx->triggerClick();p.processBlock(b,midi);juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay-glitch.png");pageMain->triggerClick();p.setPlayHead(nullptr);
}
static void previousFactoryBank() {
    DelayProcessor saved; saved.setCurrentProgram(4);
    juce::MemoryBlock block; saved.getStateInformation(block);
    auto xml=juce::AudioProcessor::getXmlFromBinary(block.getData(),static_cast<int>(block.getSize()));
    require(xml!=nullptr,"Factory state did not serialize");
    xml->setAttribute("programBank",2);
    juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*xml,legacy);
    DelayProcessor loaded; loaded.setStateInformation(legacy.getData(),static_cast<int>(legacy.getSize()));
    require(loaded.displayedProgram()==-1,"A v1.5 factory session is labeled with a replacement preset name");
    require(std::abs(loaded.values[mix]->load()-saved.values[mix]->load())<.001f,"A v1.5 factory session lost its saved mix");
}
static void factoryVoices() {
    DelayProcessor p; Host host; host.info.setIsPlaying(true); host.info.setBpm(120); host.info.setPpqPosition(0);
    p.setPlayHead(&host);
    constexpr int sr=16000;
    p.prepareToPlay(sr,256);
    const juce::StringArray banned{"Uplifting","Psy","Rolling","Progressive","Tech","Bass","Classic","Trance","House","Garage","Dubstep"};
    int piano=0, pianoSlap=0, pianoQuarter=0, pianoDotted=0, pianoReverse=0, reversePrograms=0;
    std::set<juce::String> families;
    juce::String previousFamily;
    for(int i=0;i<p.getNumPrograms();++i) {
        const auto name=p.getProgramName(i);
        const auto family=name.upToFirstOccurrenceOf(" / ",false,false);
        require(family.isNotEmpty()&&name.contains(" / "),"Preset name is not Category / Sound");
        if(family!=previousFamily){require(!families.contains(family),"Delay categories are interleaved"); families.insert(family); previousFamily=family;}
        for(const auto& word:banned) require(!name.contains(word),"Preset name still uses a genre label");
        p.setCurrentProgram(i);
        const auto s=p.settings();
        require(s.rate>=0&&s.rate<static_cast<int>(beatRates.size()),"Preset grid is not a tempo division");
        if(family=="Piano") {
            ++piano;
            if(name.contains("Slap")&&(s.rate==0||s.rate==2)) ++pianoSlap;
            if(name.contains("Quarter")&&s.rate==7) ++pianoQuarter;
            if(s.rate==5) ++pianoDotted;
            if(s.reverse>.9f) ++pianoReverse;
        }
        if(s.reverse>.9f) ++reversePrograms;
        const bool ping=s.routing==Routing::pingPong;
        if(ping) {
            require(s.diffusion<.05f&&s.shimmer<.05f,"Ping-pong preset is diffused into a wash");
            int taps=0; for(const auto& tap:s.taps) taps+=tap.enabled&&tap.level>0?1:0;
            require(taps==1,"Ping-pong preset uses more than one tap");
            p.setPlainValue(mix,100);
            constexpr int frames=sr*4;
            juce::AudioBuffer<float> audio(2,frames); audio.clear(); audio.setSample(0,0,1.f); audio.setSample(1,0,1.f);
            juce::MidiBuffer midi; p.processBlock(audio,midi);
            std::string sequence; char last=' ';
            const int hop=sr/100;
            for(int pos=hop; pos+hop<frames; pos+=hop) {
                float ml=0, mr=0;
                for(int k=0;k<hop;++k){ml=std::max(ml,std::abs(audio.getSample(0,pos+k))); mr=std::max(mr,std::abs(audio.getSample(1,pos+k)));}
                if(std::max(ml,mr)<.04f) continue;
                const char ch=ml>mr*1.8f?'L':mr>ml*1.8f?'R':'B';
                if(ch!=last){sequence.push_back(ch); last=ch;}
            }
            require(sequence.find('B')==std::string::npos, (name+" ping-pong smears both channels: "+sequence).toRawUTF8());
            require(sequence.find('L')!=std::string::npos && sequence.find('R')!=std::string::npos, (name+" does not alternate left and right: "+sequence).toRawUTF8());
        }
        if(family=="Reverse"||name.startsWith("Piano / Reverse")) require(s.reverse>.9f,"Reverse preset is not actually reversing");
    }
    require(families.size()==12,"Expected 12 delay-type categories");
    require(piano>=6&&pianoSlap>=2&&pianoQuarter>=2&&pianoDotted>=1&&pianoReverse>=1,"Piano set is missing slap, quarter, dotted, or reverse");
    require(reversePrograms>=6,"Reverse delay needs a set of tempo-synced presets");
    // Levels, clipping, and DC on a chord-like burst through every factory preset.
    p.prepareToPlay(sr,256);
    for(int i=0;i<p.getNumPrograms();++i) {
        p.setCurrentProgram(i);
        constexpr int frames=sr*2;
        juce::AudioBuffer<float> audio(2,frames); audio.clear();
        for(int n=0;n<frames;++n) {
            const float t=n/static_cast<float>(sr);
            const float env=n<static_cast<int>(.4f*sr)?std::exp(-t*3.f):0.f;
            float s=0; for(float f:{220.f,277.f,330.f,440.f}) s+=std::sin(2.f*3.14159265f*f*t);
            audio.setSample(0,n,s*env*.2f); audio.setSample(1,n,s*env*.18f);
        }
        juce::MidiBuffer midi; p.processBlock(audio,midi);
        float peak=0, dc=0;
        for(int n=0;n<frames;++n){peak=std::max(peak,std::abs(audio.getSample(0,n))); peak=std::max(peak,std::abs(audio.getSample(1,n))); dc+=audio.getSample(0,n)+audio.getSample(1,n);}
        require(std::isfinite(peak)&&peak<=1.f,(p.getProgramName(i)+" clips").toRawUTF8());
        require(std::abs(dc/(2.f*frames))<.01f,(p.getProgramName(i)+" has DC").toRawUTF8());
    }
}
static void sidechainAndSuite() {
    auto layoutOf=[](juce::AudioChannelSet main,juce::AudioChannelSet side) {
        juce::AudioProcessor::BusesLayout layout;
        layout.inputBuses.add(main);layout.inputBuses.add(side);layout.outputBuses.add(main);return layout;
    };
    DelayProcessor defaults;
    require(defaults.getBusCount(true)==2,"Sidechain input bus is missing");
    require(defaults.getBus(true,1)->getName()=="Sidechain","Sidechain bus has the wrong name");
    require(!defaults.getBus(true,1)->isEnabled(),"Sidechain bus is on before a host connects it");
    require(defaults.isBusesLayoutSupported(defaults.getBusesLayout()),"Default layout is rejected");
    const auto stereo=juce::AudioChannelSet::stereo(),mono=juce::AudioChannelSet::mono(),off=juce::AudioChannelSet::disabled();
    require(defaults.isBusesLayoutSupported(layoutOf(stereo,off)),"Stereo without a sidechain is rejected");
    require(defaults.isBusesLayoutSupported(layoutOf(stereo,stereo)),"Stereo sidechain is rejected");
    require(defaults.isBusesLayoutSupported(layoutOf(stereo,mono)),"Mono sidechain on a stereo track is rejected");
    require(defaults.isBusesLayoutSupported(layoutOf(mono,mono)),"Mono main and sidechain is rejected");
    require(defaults.isBusesLayoutSupported(layoutOf(mono,off)),"Mono without a sidechain is rejected");
    auto mismatch=layoutOf(stereo,stereo);mismatch.outputBuses.getReference(0)=mono;
    require(!defaults.isBusesLayoutSupported(mismatch),"Mismatched main channels were accepted");
    auto quad=layoutOf(stereo,juce::AudioChannelSet::quadraphonic());
    require(!defaults.isBusesLayoutSupported(quad),"Surround sidechain was accepted");

    constexpr int rate=8000,frames=6000;
    auto echoAt=[&](DelayProcessor& p,bool sidechain,bool driveSidechain) {
        if(sidechain) require(p.setBusesLayout(layoutOf(stereo,stereo)),"Could not enable the sidechain");
        p.prepareToPlay(rate,256);
        const int channels=p.getTotalNumInputChannels();
        juce::AudioBuffer<float> audio(channels,frames);audio.clear();audio.setSample(0,0,1.f);
        if(sidechain&&driveSidechain) for(int i=0;i<frames;++i){audio.setSample(2,i,.9f);if(channels>3) audio.setSample(3,i,.9f);}
        juce::MidiBuffer midi;p.processBlock(audio,midi);
        return audio.getSample(0,4000);
    };
    DelayProcessor fallback;clean(fallback);fallback.setPlainValue(duck,100);fallback.setPlainValue(duckMode,2);fallback.setPlainValue(mix,100);fallback.setPlainValue(feedback,0);
    require(std::abs(echoAt(fallback,false,false)-1.f)<.05f,"Sidechain ducking closes the echoes when nothing is connected");
    DelayProcessor quiet;clean(quiet);quiet.setPlainValue(duck,100);quiet.setPlainValue(duckMode,2);quiet.setPlainValue(mix,100);quiet.setPlainValue(feedback,0);
    require(std::abs(echoAt(quiet,true,false)-1.f)<.05f,"A connected but silent sidechain still ducks");
    DelayProcessor kicked;clean(kicked);kicked.setPlainValue(duck,100);kicked.setPlainValue(duckMode,2);kicked.setPlainValue(mix,100);kicked.setPlainValue(feedback,0);
    require(kicked.setBusesLayout(layoutOf(stereo,stereo)),"Kick layout failed");
    kicked.prepareToPlay(rate,128);
    juce::AudioBuffer<float> kick(4,frames);kick.clear();kick.setSample(0,0,1.f);
    for(int i=3200;i<5000;++i){const float k=std::sin(2.f*3.14159265f*60.f*i/rate)*std::exp(-(i-3200)/800.f);kick.setSample(2,i,k);kick.setSample(3,i,k);}
    juce::MidiBuffer midi;kicked.processBlock(kick,midi);
    require(std::abs(kick.getSample(0,4000))<.25f,"Sidechain kick did not duck the echo");
    require(std::abs(kick.getSample(0,200))<.02f,"Sidechain kick leaked into the dry output");
    DelayProcessor inputMode;clean(inputMode);inputMode.setPlainValue(duck,100);inputMode.setPlainValue(duckMode,1);inputMode.setPlainValue(mix,100);
    const float withKick=echoAt(inputMode,true,true);
    DelayProcessor inputOnly;clean(inputOnly);inputOnly.setPlainValue(duck,100);inputOnly.setPlainValue(duckMode,1);inputOnly.setPlainValue(mix,100);
    const float noKick=echoAt(inputOnly,false,false);
    require(std::abs(withKick-noKick)<.02f,"Input ducking followed the sidechain");
    require(kicked.settings().duckSource==DuckSource::sidechain&&!kicked.settings().inputDuck,"Sidechain is stored as input ducking");
    DelayProcessor recalled;recalled.setPlainValue(duckMode,1);
    juce::MemoryBlock saved;recalled.getStateInformation(saved);
    recalled.setPlainValue(duckMode,2);
    recalled.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
    require(recalled.settings().duckSource==DuckSource::input&&recalled.settings().inputDuck,"An old Input duck setting now reads as Sidechain");

    DelayProcessor edits;
    const float mix0=edits.values[mix]->load(),feedback0=edits.values[feedback]->load();
    edits.history.beginGesture();edits.setPlainValue(mix,12);edits.setPlainValue(feedback,46);edits.history.endGesture();
    require(edits.history.canUndo(),"A grouped edit was not recorded");
    edits.history.undo();
    require(std::abs(edits.values[mix]->load()-mix0)<.02f&&std::abs(edits.values[feedback]->load()-feedback0)<.02f,"Undo restored only one control in the gesture");
    edits.history.redo();
    require(std::abs(edits.values[mix]->load()-12)<.02f&&std::abs(edits.values[feedback]->load()-46)<.02f,"Redo did not restore the grouped edit");
    edits.setCurrentProgram(0);const float programMix=edits.values[mix]->load();
    edits.setCurrentProgram(3);
    require(edits.getCurrentProgram()==3&&std::abs(edits.values[mix]->load()-programMix)>.5f,"Program change did not move");
    edits.history.undo();
    require(edits.getCurrentProgram()==0&&std::abs(edits.values[mix]->load()-programMix)<.02f,"Undo did not restore the previous program");
    edits.history.redo();
    require(edits.getCurrentProgram()==3,"Redo did not restore the program");
    std::unique_ptr<juce::AudioProcessorEditor> keys(edits.createEditor());
    const float beforeKey=edits.values[mix]->load();
    edits.history.beginGesture();edits.setPlainValue(mix,70);edits.history.endGesture();
    require(keys->keyPressed(juce::KeyPress('Z',juce::ModifierKeys::commandModifier,0)),"Undo shortcut was ignored");
    require(std::abs(edits.values[mix]->load()-beforeKey)<.02f,"Ctrl/Cmd+Z did not undo");
    require(keys->keyPressed(juce::KeyPress('Z',juce::ModifierKeys::commandModifier|juce::ModifierKeys::shiftModifier,0)),"Redo shortcut was ignored");
    require(std::abs(edits.values[mix]->load()-70)<.02f,"Shift+Ctrl/Cmd+Z did not redo");

    DelayProcessor slots;slots.setPlainValue(mix,80);slots.copyABSlot(true);slots.setPlainValue(mix,15);
    juce::MemoryBlock slotState;slots.getStateInformation(slotState);
    DelayProcessor restored;restored.setStateInformation(slotState.getData(),static_cast<int>(slotState.getSize()));
    require(restored.ab.active()==0&&std::abs(restored.values[mix]->load()-15)<.02f,"A/B recall lost the active slot");
    restored.selectABSlot(1);
    require(std::abs(restored.values[mix]->load()-80)<.02f,"A/B slot B did not keep the copied setup");
    restored.history.beginGesture();restored.setPlainValue(mix,33);restored.history.endGesture();
    require(restored.isPresetModified(),"User edit did not mark preset as modified");
    restored.selectABSlot(0);
    restored.history.beginGesture();restored.setPlainValue(mix,22);restored.history.endGesture();
    restored.selectABSlot(1);
    restored.history.undo();
    require(std::abs(restored.values[mix]->load()-80)<.02f,"Per-slot undo did not restore slot B");
    restored.selectABSlot(0);
    require(std::abs(restored.values[mix]->load()-22)<.02f,"Slot B undo leaked into slot A");
    restored.selectABSlot(1);restored.setPlainValue(mix,50);restored.copyABSlot(false);restored.selectABSlot(0);
    require(std::abs(restored.values[mix]->load()-50)<.02f&&!restored.history.canUndo(),"Copy B to A did not replace A or clear A undo");

    auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("oed-preset-suite");
    root.deleteRecursively();root.createDirectory();
    const auto presetDir=suite::PresetStore::defaultRoot("openEDMDelay").getChildFile("Presets");
    require(presetDir.getFileName()=="Presets"&&presetDir.getParentDirectory().getFileName()=="openEDMDelay"&&presetDir.getParentDirectory().getParentDirectory().getFileName()=="Alan Marcero","User presets are not stored in Documents/Alan Marcero/openEDMDelay/Presets");
    DelayProcessor presets;
    suite::PresetBrowser browser(root,suite::PresetHandlers{
        .count=[&]{return presets.getNumPrograms();},
        .name=[&](int index){return presets.getProgramName(index);},
        .loadFactory=[&](int index){presets.setCurrentProgram(index);},
        .capture=[&]{return presets.captureState();},
        .loadUser=[&](const juce::MemoryBlock& block){presets.applyUserPreset(block);}
    });
    browser.setBounds(0,0,544,36);
    require(browser.visibleNames().size()==presets.getNumPrograms(),"The browser dropped a factory preset");
    browser.setSearch("piano");
    require(browser.visibleNames().size()>=6,"Piano programs are missing from search");
    for(const auto& name:browser.visibleNames()) require(name.containsIgnoreCase("piano"),"Search showed a preset outside the query");
    browser.setSearch("zzznone");require(browser.visibleNames().isEmpty(),"Search kept non-matching presets");
    browser.setSearch("");
    const auto originalName=presets.getProgramName(0);
    browser.selectFactory(0);
    require(browser.selectionIsFactory(),"Factory row was not selected");
    require(!browser.renameSelected()&&!browser.deleteSelected(),"Factory preset was renamed or deleted");
    require(presets.getProgramName(0)==originalName&&presets.getNumPrograms()==72,"Factory bank changed");
    presets.setPlainValue(mix,61);
    browser.setSearch("Night Kick");
    dynamic_cast<juce::Button*>(find(browser,"presetSave"))->triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    browser.setSearch("");
    require(browser.visibleNames().contains("Night Kick"),"Saved user preset is not in the browser");
    presets.setPlainValue(mix,10);
    auto* list=dynamic_cast<juce::ComboBox*>(find(browser,"presetList"));
    int userId=0;for(int i=0;i<list->getNumItems();++i) if(list->getItemText(i).contains("Night Kick")) userId=list->getItemId(i);
    require(userId>0,"Saved preset has no menu row");
    list->setSelectedId(0,juce::dontSendNotification);
    list->setSelectedId(userId,juce::sendNotificationSync);
    require(std::abs(presets.values[mix]->load()-61)<.02f,"Loading a user preset did not restore the sound");
    require(!browser.selectionIsFactory(),"User preset is marked read-only");
    browser.setSearch("Morning Kick");
    require(browser.renameSelected(),"User preset rename failed");
    browser.setSearch("");
    require(browser.visibleNames().contains("Morning Kick")&&!browser.visibleNames().contains("Night Kick"),"Rename left the old user preset in the list");
    browser.toggleFavorite();
    require(browser.visibleNames()[0].contains("Morning Kick"),"Favorite is not listed first");
    auto* deleteBtn=dynamic_cast<juce::TextButton*>(find(browser,"presetDelete"));
    require(deleteBtn!=nullptr,"Delete button missing");
    deleteBtn->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    require(deleteBtn->getButtonText()=="Delete?","Delete did not ask for two-step confirm");
    deleteBtn->triggerClick();juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    browser.setSearch("");
    require(!browser.visibleNames().contains("Morning Kick"),"Deleted user preset is still listed");
    require(presets.getNumPrograms()==72,"Deleting a user preset removed a factory program");

    if(juce::File("/opt/cursor/artifacts").isDirectory()) {
        DelayProcessor demo;clean(demo);demo.setPlainValue(mix,45);demo.setPlainValue(feedback,35);demo.setPlainValue(duck,100);demo.setPlainValue(duckMode,2);demo.setPlainValue(gridRate,7);demo.setPlainValue(duckRate,7);
        require(demo.setBusesLayout(layoutOf(stereo,stereo)),"Demo layout failed");
        constexpr int demoRate=44100,demoFrames=demoRate*4,block=256;
        demo.prepareToPlay(demoRate,block);
        juce::AudioBuffer<float> song(4,demoFrames);song.clear();
        const double beat=demoRate*60.0/120.0;
        for(int i=0;i<demoFrames;++i) {
            const double phase=std::fmod(i/beat,1.0),beatIndex=std::floor(i/beat);
            if(phase<0.09){const float env=std::exp(-static_cast<float>(phase*beat)/900.f);const float kick=std::sin(2.f*3.14159265f*52.f*i/demoRate)*env;song.setSample(2,i,kick);song.setSample(3,i,kick*.9f);}
            if(static_cast<int>(beatIndex)%2==0&&phase<0.16) {
                const float env=std::exp(-static_cast<float>(phase*8.f));
                float chord=0.f;for(float f:{196.f,247.f,294.f,392.f}) chord+=std::sin(2.f*3.14159265f*f*i/demoRate);
                song.setSample(0,i,chord*env*.18f);song.setSample(1,i,chord*env*.16f);
            }
        }
        juce::MidiBuffer songMidi;
        for(int offset=0;offset<demoFrames;offset+=block) {
            const int count=std::min(block,demoFrames-offset);
            juce::AudioBuffer<float> slice(4,count);
            for(int c=0;c<4;++c) slice.copyFrom(c,0,song,c,offset,count);
            demo.processBlock(slice,songMidi);
            for(int c=0;c<2;++c) song.copyFrom(c,offset,slice,c,0,count);
        }
        auto wav=juce::File("/opt/cursor/artifacts/sidechain-duck.wav");
        auto stream=wav.createOutputStream();
        require(stream!=nullptr,"Could not create the demo wav");
        stream->setPosition(0);stream->truncate();
        std::unique_ptr<juce::OutputStream> output=std::move(stream);
        const auto options=juce::AudioFormatWriterOptions{}.withSampleRate(demoRate).withNumChannels(2).withBitsPerSample(16);
        auto writer=juce::WavAudioFormat{}.createWriterFor(output,options);
        require(writer!=nullptr,"Could not write a 16-bit wav");
        require(writer->writeFromAudioSampleBuffer(song,0,demoFrames),"Demo wav write failed");
        writer.reset();
        require(wav.getSize()<12*1024*1024,"Demo wav is over 12 MB");
        std::unique_ptr<juce::AudioProcessorEditor> editor(demo.createEditor());
        editor->setVisible(true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        auto png=juce::File("/opt/cursor/artifacts/ui.png");
        std::unique_ptr<juce::OutputStream> image=png.createOutputStream();
        require(image!=nullptr&&juce::PNGImageFormat{}.writeImageToStream(editor->createComponentSnapshot(editor->getLocalBounds(),true,1.5f),*image),"UI screenshot failed");
        auto* frame=find(*editor,"sidechainScope");
        require(frame!=nullptr,"Sidechain scope is missing");
        const auto area=editor->getLocalArea(frame,frame->getLocalBounds());
        const auto shot=editor->createComponentSnapshot(area);
        int warm=0;for(int y=0;y<shot.getHeight();++y) for(int x=0;x<shot.getWidth();++x){const auto c=shot.getPixelAt(x,y);if(c.getRed()>70&&c.getRed()>c.getBlue()+25&&c.getGreen()>45) ++warm;}
        require(warm>30,"Sidechain kick is not drawn behind the duck curve");
    }
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;int errors=0;for(const auto& [name,test]:std::initializer_list<std::pair<const char*,void(*)()>>{{"sidechain buses, ducking, undo, A/B, presets",sidechainAndSuite},{"factory voices",factoryVoices},{"previous factory bank",previousFactoryBank},{"factory impulse responses and rhythms",bankAudio},{"filtered tap host tail",filteredTapTail},{"diffusion host tail",diffusionHostTail},{"paged layout and scale",pagedLayoutAndScale},{"host audio/bypass",audio},{"session recall",recall},{"typed values",typedValues},{"malformed parameter state",malformedState},{"preset while bypassed",presetWhileBypassed}})try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';++errors;}try{ui(juce::File::getCurrentWorkingDirectory().getChildFile(argc>1?argv[1]:"docs/screenshots"));std::cout<<"PASS native controls and actual editor screenshots\n";}catch(const std::exception& e){std::cerr<<"FAIL UI: "<<e.what()<<'\n';++errors;}return errors?1:0;}
