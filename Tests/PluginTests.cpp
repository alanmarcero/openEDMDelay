#include "PluginProcessor.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <stdexcept>
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
void recall(){legacyAndPerformanceState();DelayProcessor p;for(int i=0;i<parameterCount;++i){const auto& s=parameterSpecs()[static_cast<std::size_t>(i)];p.setPlainValue(i,s.min+(s.max-s.min)*.73f);}juce::MemoryBlock state;p.getStateInformation(state);DelayProcessor restored;restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));for(int i=0;i<parameterCount;++i)require(p.values[static_cast<std::size_t>(i)]->load()==restored.values[static_cast<std::size_t>(i)]->load(),"Session state loses parameter");restored.setStateInformation("invalid",7);require(restored.values[mix]->load()==p.values[mix]->load(),"Malformed state resets parameters");}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* c:root.getChildren())if(auto* result=find(*c,id))return result;return nullptr;}
static void scrollableControls() {
    DelayProcessor p;std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    require(editor->getHeight()<=850,"Default editor exceeds common display height");
    auto* viewport=dynamic_cast<juce::Viewport*>(find(*editor,"editorViewport"));
    require(viewport!=nullptr,"Editor has no scrollable control surface");
    auto* tap=dynamic_cast<juce::ComboBox*>(find(*editor,"performanceTap"));
    require(tap!=nullptr,"Performance tap selector missing");
    for(const auto bounds:{juce::Point<int>(1216,850),juce::Point<int>(900,600)}) {
        editor->setSize(bounds.x,bounds.y);
        for(int index=0;index<tapCount;++index) {
            tap->setSelectedId(index+1,juce::sendNotificationSync);
            for(int field=0;field<tapExtraCount;++field) {
                auto* control=find(*editor,parameterSpecs()[static_cast<std::size_t>(tapExtraStart+index*tapExtraCount+field)].id);
                require(control!=nullptr&&control->isVisible(),"Selected tap control is hidden");
                viewport->setViewPosition(control->getX(),control->getY());
                require(viewport->getViewArea().contains(control->getBounds()),"New tap control cannot scroll fully into view");
            }
        }
        for(const int id:std::array<int,11>{swing,groove,freeze,patternMorph,diffusion,diffusionSize,wobbleRate,wobbleShape,shimmer,stereoOffset,duckHold}) {
            auto* control=find(*editor,parameterSpecs()[static_cast<std::size_t>(id)].id);
            viewport->setViewPosition(control->getX(),control->getY());
            require(viewport->getViewArea().contains(control->getBounds()),"Performance control cannot scroll fully into view");
        }
    }
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
    std::cout<<"64 distinct impulse responses at 160 BPM, first echoes "<<minimum<<" to "<<maximum<<" s\n";
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
            require(area.getWidth()>=44&&area.getHeight()>=36,c->getComponentID().toRawUTF8());
            auto* hit=surface.getComponentAt(area.getCentre());
            require(hit==c||c->isParentOf(hit),"A control is covered by another component");
        }
        checkHitTargets(surface,*c);
    }
}

void ui(const juce::File& destination){
    DelayProcessor p;p.prepareToPlay(48000,256);std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
    e->setVisible(true);
    auto* surface=dynamic_cast<juce::Viewport*>(find(*e,"editorViewport"))->getViewedComponent();
    checkHitTargets(*surface,*surface);
    int menuWidth=0,menuHeight=0;e->getLookAndFeel().getIdealPopupMenuItemSize("Signal Glass",false,0,menuWidth,menuHeight);
    require(menuHeight>=36,"Preset menu rows are too small");
    auto* browser=dynamic_cast<juce::ComboBox*>(find(*e,"preset"));
    auto* previous=dynamic_cast<juce::TextButton*>(find(*e,"prevPreset"));
    auto* next=dynamic_cast<juce::TextButton*>(find(*e,"nextPreset"));
    require(browser&&previous&&next,"Missing preset browser controls");
    require(browser->getRootMenu()->getNumItems()==7,"Preset menu should group presets into 7 dance styles");
    previous->onClick();require(p.getCurrentProgram()==63,"Previous failed to wrap");
    next->onClick();require(p.getCurrentProgram()==0,"Next failed to wrap");
    browser->setSelectedId(13,juce::sendNotificationSync);
    require(p.getCurrentProgram()==12,"Preset menu did not update the processor");
    browser->setSelectedId(1,juce::sendNotificationSync);
    auto* initialMix=find(*e,"mix");require(initialMix!=nullptr,"Initial mix control missing");bool initialUnit=false;
    for(auto* child:initialMix->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))initialUnit=label->getText()=="32%";
    require(initialUnit,"Initial knob value is missing its unit");
    for(int i=0;i<parameterCount;++i){const auto& spec=parameterSpecs()[static_cast<std::size_t>(i)];auto* c=find(*e,spec.id);require(c!=nullptr,("Missing control: "+spec.id).toRawUTF8());if(auto* s=dynamic_cast<juce::Slider*>(c))s->setValue(spec.min+(spec.max-spec.min)*.6,juce::sendNotificationSync);else if(auto* box=dynamic_cast<juce::ComboBox*>(c))box->setSelectedId(box->getNumItems(),juce::sendNotificationSync);else if(auto* button=dynamic_cast<juce::Button*>(c))button->setToggleState(true,juce::sendNotificationSync);else require(false,"Unknown control");}
    require(p.values[mix]->load()==60,"Mix knob disconnected");require(p.values[baseCount+1]->load()==10,"Tap step disconnected");require(p.values[hpSlope]->load()==4,"Slope selector disconnected");
    require(p.getNumPrograms()==64,"EDM factory bank must contain 64 programs");
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
    const auto reelImage=[&]{p.processBlock(b,midi);return e->createComponentSnapshot({918,113,68,68});};
    host.info.setPpqPosition(-2);const auto downbeat=reelImage();
    host.info.setBpm(141);host.info.setPpqPosition(7.5);const auto offbeat=reelImage();
    int changed=0;for(int y=0;y<68;++y)for(int x=0;x<68;++x)if(downbeat.getPixelAt(x,y)!=offbeat.getPixelAt(x,y))++changed;
    require(changed>80&&p.displayedBpm.load()==141,"SVG reel does not follow host seeks and tempo");
    host.info.setIsPlaying(false);host.info.setPpqPosition(8);const auto frozen=reelImage();
    for(int y=0;y<68;++y)for(int x=0;x<68;++x)require(frozen.getPixelAt(x,y)==offbeat.getPixelAt(x,y),"Stopped SVG reel keeps advancing");
    host.info.setIsPlaying(true);host.info.setPpqPosition(1.35);host.info.setBpm(128);
    p.setCurrentProgram(0);p.processBlock(b,midi);juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay.png");
    auto* viewport=dynamic_cast<juce::Viewport*>(find(*e,"editorViewport"));
    require(viewport!=nullptr,"Editor viewport missing during capture");
    p.setCurrentProgram(0);p.setPlainValue(diffusion,65);p.setPlainValue(diffusionSize,20);p.processBlock(b,midi);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(60);viewport->setViewPosition(0,1100);save("openEDMDelay-performance.png");viewport->setViewPosition(0,0);
    p.setCurrentProgram(43);p.processBlock(b,midi);juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay-glitch.png");p.setPlayHead(nullptr);
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;int errors=0;for(const auto& [name,test]:std::initializer_list<std::pair<const char*,void(*)()>>{{"factory impulse responses and rhythms",bankAudio},{"filtered tap host tail",filteredTapTail},{"diffusion host tail",diffusionHostTail},{"scrollable control access",scrollableControls},{"host audio/bypass",audio},{"session recall",recall},{"malformed parameter state",malformedState},{"preset while bypassed",presetWhileBypassed}})try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';++errors;}try{ui(juce::File::getCurrentWorkingDirectory().getChildFile(argc>1?argv[1]:"docs/screenshots"));std::cout<<"PASS native controls and actual editor screenshots\n";}catch(const std::exception& e){std::cerr<<"FAIL UI: "<<e.what()<<'\n';++errors;}return errors?1:0;}
