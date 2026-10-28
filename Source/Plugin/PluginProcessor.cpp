#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <sstream>
#include <locale>
namespace oed {
DelayProcessor::DelayProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"openEDMDelay",makeLayout()) {
    for(std::size_t i=0;i<values.size();++i)values[i]=parameters.getRawParameterValue(parameterSpecs()[i].id);
}
void DelayProcessor::prepareToPlay(double sr,int){engine.prepare(sr);currentSampleRate=sr;internalPhase=0;wasBypassed=false;}
void DelayProcessor::reset(){engine.reset();internalPhase=0;}
bool DelayProcessor::isBusesLayoutSupported(const BusesLayout& l)const {
    return l.getMainInputChannelSet()==l.getMainOutputChannelSet()&&(l.getMainOutputChannelSet()==juce::AudioChannelSet::mono()||l.getMainOutputChannelSet()==juce::AudioChannelSet::stereo());
}
void DelayProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    for(int c=getTotalNumInputChannels();c<buffer.getNumChannels();++c)buffer.clear(c,0,buffer.getNumSamples());
    if(buffer.getNumChannels()==0)return;
    if(values[bypass]->load()>.5f){processBlockBypassed(buffer,midi);return;}
    const bool requested=resetPending.exchange(false);
    if(wasBypassed||requested){engine.reset();wasBypassed=false;}
    Transport transport;transport.bpm=values[tempo]->load();transport.ppq=internalPhase;transport.hasPpq=true;
    bool synced=false;
    if(auto* playhead=getPlayHead())if(auto position=playhead->getPosition()) {
        if(auto bpm=position->getBpm();bpm&&std::isfinite(*bpm)&&*bpm>=20&&*bpm<=400) {
            transport.bpm=*bpm;synced=true;
            if(position->getIsPlaying())if(auto ppq=position->getPpqPosition();ppq&&std::isfinite(*ppq))transport.ppq=*ppq;
        }
    }
    displayedBpm.store(transport.bpm);hostSync.store(synced);displayedPhase.store(transport.ppq);
    engine.process(buffer.getWritePointer(0),buffer.getNumChannels()>1?buffer.getWritePointer(1):nullptr,buffer.getNumSamples(),settings(),transport);
    internalPhase=transport.ppq+buffer.getNumSamples()/currentSampleRate*transport.bpm/60;
    duckMeter.store(engine.duckGain());outputPeak.store(buffer.getMagnitude(0,buffer.getNumSamples()));
}
void DelayProcessor::processBlockBypassed(juce::AudioBuffer<float>&,juce::MidiBuffer&) {
    if(!wasBypassed){engine.reset();wasBypassed=true;}duckMeter.store(1);outputPeak.store(0);
}
double DelayProcessor::getTailLengthSeconds()const {
    const auto s=settings();int last=0;for(const auto& tap:s.taps)if(tap.enabled&&tap.level>0)last=std::max(last,tap.step);
    if(last==0)return 0;
    const double seconds=last*beatRates[static_cast<std::size_t>(s.rate)]*60/std::max(20.,displayedBpm.load());
    return seconds*(1+(s.feedback>0?std::ceil(std::log(.0001)/std::log(s.feedback)):0));
}
juce::AudioProcessorParameter* DelayProcessor::getBypassParameter()const{return parameters.getParameter("bypass");}
juce::AudioProcessorEditor* DelayProcessor::createEditor(){return new DelayEditor(*this);}
void DelayProcessor::setPlainValue(int id,float value) {
    auto* p=parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(id)].id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}
const juce::StringArray& patternNames() {
    static const juce::StringArray names{"Single","Pattern 1+1","Pattern 2+1","Pattern 3+2","Pattern 3+4","Pattern 3+5","Pattern 4+3","Pattern 5+3","Eight Steps","Broken Orbit","Trance Cascade","Late Bloom"};return names;
}
void DelayProcessor::applyPattern(int index) {
    constexpr int patterns[][8]={{1,0,0,0,0,0,0,0},{1,1,0,0,0,0,0,0},{2,1,0,0,0,0,0,0},{3,2,0,0,0,0,0,0},{3,4,0,0,0,0,0,0},{3,5,0,0,0,0,0,0},{4,3,0,0,0,0,0,0},{5,3,0,0,0,0,0,0},{1,2,3,4,5,6,7,8},{2,3,7,8,10,11,14,16},{3,5,8,11,13,14,15,16},{4,6,8,10,12,14,15,16}};
    index=juce::jlimit(0,11,index);
    for(int i=0;i<tapCount;++i) {
        const int p=baseCount+i*4,step=patterns[index][i];setPlainValue(p,step>0?1.f:0.f);setPlainValue(p+1,static_cast<float>(std::max(1,step)));
        setPlainValue(p+2,i==0?100.f:85.f-static_cast<float>(i)*5);setPlainValue(p+3,i%2?70.f:-70.f);
    }
}
int DelayProcessor::getNumPrograms(){return 12;}
const juce::String DelayProcessor::getProgramName(int index) {
    static const juce::StringArray names{"01  /  Neon Orbit","02  /  Clean Quarter","03  /  Dotted Glass","04  /  Analog Afterglow","05  /  Pattern 3+5","06  /  Mono Dub","07  /  Trance Cascade","08  /  Broken Transmission","09  /  Reverse Bloom","10  /  Ducking Lead","11  /  Lo-fi Satellites","12  /  Tape Wash"};
    return names[juce::jlimit(0,11,index)];
}
void DelayProcessor::setCurrentProgram(int index) {
    index=juce::jlimit(0,11,index);program.store(index);
    for(int i=0;i<parameterCount;++i)setPlainValue(i,parameterSpecs()[static_cast<std::size_t>(i)].initial);
    if(index==1){applyPattern(0);setPlainValue(gridRate,7);setPlainValue(routing,0);setPlainValue(character,0);setPlainValue(duck,0);}
    if(index==2){applyPattern(0);setPlainValue(gridRate,5);setPlainValue(character,0);setPlainValue(feedback,48);}
    if(index==3){applyPattern(4);setPlainValue(character,2);setPlainValue(lowPass,4200);setPlainValue(feedback,58);setPlainValue(drive,55);}
    if(index==4){applyPattern(5);setPlainValue(routing,0);setPlainValue(character,0);setPlainValue(decay,0);}
    if(index==5){applyPattern(0);setPlainValue(gridRate,5);setPlainValue(routing,2);setPlainValue(feedback,68);setPlainValue(lowPass,2300);setPlainValue(drive,60);}
    if(index==6){applyPattern(10);setPlainValue(character,0);setPlainValue(feedback,42);setPlainValue(duck,60);}
    if(index==7){applyPattern(9);setPlainValue(character,3);setPlainValue(glitch,2);setPlainValue(glitchAmount,65);setPlainValue(glitchRate,1);setPlainValue(duck,38);}
    if(index==8){applyPattern(11);setPlainValue(glitch,3);setPlainValue(glitchAmount,80);setPlainValue(glitchRate,4);setPlainValue(feedback,58);}
    if(index==9){applyPattern(5);setPlainValue(duck,85);setPlainValue(duckMode,1);setPlainValue(feedback,45);}
    if(index==10){applyPattern(8);setPlainValue(character,3);setPlainValue(lowPass,4500);setPlainValue(glitch,1);setPlainValue(glitchAmount,40);}
    if(index==11){applyPattern(11);setPlainValue(feedback,72);setPlainValue(motion,65);setPlainValue(lowPass,3200);setPlainValue(decay,48);setPlainValue(mix,45);}
    resetPending.store(true);
}
void DelayProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto state=parameters.copyState();state.setProperty("program",program.load(),nullptr);
    if(auto xml=state.createXml())copyXmlToBinary(*xml,block);
}
void DelayProcessor::setStateInformation(const void* data,int size) {
    if(auto xml=getXmlFromBinary(data,size);xml&&xml->hasTagName(parameters.state.getType())) {
        auto state=juce::ValueTree::fromXml(*xml);if(!state.isValid())return;
        for(const auto& spec:parameterSpecs()) {
            int matches=0;
            for(const auto& child:state)if(child.getProperty("id").toString()==spec.id) {
                ++matches;
                const auto value=child.getProperty("value").toString().trim().toStdString();double number=0;
                std::istringstream parser(value);parser.imbue(std::locale::classic());parser>>std::noskipws>>number;
                if(!parser||!parser.eof()||!std::isfinite(number)||number<spec.min||number>spec.max)return;
                if((spec.choices.isNotEmpty()||spec.id.endsWith("Step"))&&std::abs(number-std::round(number))>1.e-6)return;
            }
            if(matches!=1)return;
        }
        parameters.replaceState(state);program.store(juce::jlimit(0,11,static_cast<int>(state.getProperty("program",0))));resetPending.store(true);
    }
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new oed::DelayProcessor;}
