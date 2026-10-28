#include "PluginProcessor.h"
#include <iostream>
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
void recall(){DelayProcessor p;for(int i=0;i<parameterCount;++i){const auto& s=parameterSpecs()[static_cast<std::size_t>(i)];p.setPlainValue(i,s.min+(s.max-s.min)*.73f);}juce::MemoryBlock state;p.getStateInformation(state);DelayProcessor restored;restored.setStateInformation(state.getData(),static_cast<int>(state.getSize()));for(int i=0;i<parameterCount;++i)require(p.values[static_cast<std::size_t>(i)]->load()==restored.values[static_cast<std::size_t>(i)]->load(),"Session state loses parameter");restored.setStateInformation("invalid",7);require(restored.values[mix]->load()==p.values[mix]->load(),"Malformed state resets parameters");}
juce::Component* find(juce::Component& root,const juce::String& id){if(root.getComponentID()==id)return &root;for(auto* c:root.getChildren())if(auto* result=find(*c,id))return result;return nullptr;}
void ui(const juce::File& destination){
    DelayProcessor p;p.prepareToPlay(48000,256);std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor());
    auto* initialMix=find(*e,"mix");require(initialMix!=nullptr,"Initial mix control missing");bool initialUnit=false;
    for(auto* child:initialMix->getChildren())if(auto* label=dynamic_cast<juce::Label*>(child))initialUnit=label->getText()=="32%";
    require(initialUnit,"Initial knob value is missing its unit");
    for(int i=0;i<parameterCount;++i){const auto& spec=parameterSpecs()[static_cast<std::size_t>(i)];auto* c=find(*e,spec.id);require(c!=nullptr,("Missing control: "+spec.id).toRawUTF8());if(auto* s=dynamic_cast<juce::Slider*>(c))s->setValue(spec.min+(spec.max-spec.min)*.6,juce::sendNotificationSync);else if(auto* box=dynamic_cast<juce::ComboBox*>(c))box->setSelectedId(box->getNumItems(),juce::sendNotificationSync);else if(auto* button=dynamic_cast<juce::Button*>(c))button->setToggleState(true,juce::sendNotificationSync);else require(false,"Unknown control");}
    require(p.values[mix]->load()==60,"Mix knob disconnected");require(p.values[baseCount+1]->load()==10,"Tap step disconnected");require(p.values[hpSlope]->load()==4,"Slope selector disconnected");
    require(p.getNumPrograms()>=10,"Missing factory programs");
    require(destination.createDirectory().wasOk(),"Screenshot directory failed");
    const auto save=[&](const juce::String& name){auto out=destination.getChildFile(name).createOutputStream();require(out!=nullptr,"Screenshot open failed");out->setPosition(0);out->truncate();require(juce::PNGImageFormat{}.writeImageToStream(e->createComponentSnapshot(e->getLocalBounds(),true,1.5f),*out),"Screenshot failed");};
    Host host;host.info.setBpm(128);host.info.setPpqPosition(1.35);host.info.setIsPlaying(true);p.setPlayHead(&host);juce::AudioBuffer<float> b(2,256);b.clear();juce::MidiBuffer midi;
    p.setCurrentProgram(0);p.processBlock(b,midi);juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay.png");
    p.setCurrentProgram(7);p.processBlock(b,midi);juce::MessageManager::getInstance()->runDispatchLoopUntil(60);save("openEDMDelay-glitch.png");p.setPlayHead(nullptr);
}
int main(int argc,char** argv){juce::ScopedJuceInitialiser_GUI gui;int errors=0;for(const auto& [name,test]:std::initializer_list<std::pair<const char*,void(*)()>>{{"host audio/bypass",audio},{"session recall",recall},{"malformed parameter state",malformedState},{"preset while bypassed",presetWhileBypassed}})try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';++errors;}try{ui(juce::File::getCurrentWorkingDirectory().getChildFile(argc>1?argv[1]:"docs/screenshots"));std::cout<<"PASS native controls and actual editor screenshots\n";}catch(const std::exception& e){std::cerr<<"FAIL UI: "<<e.what()<<'\n';++errors;}return errors?1:0;}
