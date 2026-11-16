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
    const auto s=settings();double last=0,latest=0,filterTail=0;
    for(const auto& tap:s.taps)if(tap.enabled&&tap.level>0) {
        double step=static_cast<double>(tap.step)+(static_cast<double>(tap.morphStep)-tap.step)*s.morph;
        const int index=static_cast<int>(std::round(step))-1;
        if(s.groove==2) {constexpr double offsets[]{0,.12,-.08,.18};step+=offsets[index%4];}
        else if(index%2)step+=s.groove==1?.25:s.swing*.5;
        last=std::max(last,step);latest=std::max(latest,step+1.-1./tap.ratchets);
        // Both tap IIRs can keep ringing after the last delay read (including at zero feedback).
        if(tap.filter)filterTail=std::max(filterTail,12./std::max(20.f,std::min(tap.highPass,tap.lowPass)));
    }
    if(last<=0)return 0;
    if(s.freeze)return 86400.;
    const double unit=beatRates[static_cast<std::size_t>(s.rate)]*60/std::max(20.,displayedBpm.load());
    const double repeats=s.feedback>0?std::ceil(std::log(.0001)/std::log(s.feedback)):0;
    return unit*(latest+last*repeats)+filterTail;
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
int DelayProcessor::getNumPrograms(){return 36;}
const juce::String DelayProcessor::getProgramName(int index) {
    static const juce::StringArray names{
        "01  /  Melodic Techno: Neon Orbit", "02  /  Tech House: Clean Quarter", "03  /  Progressive House: Dotted Glass", "04  /  Progressive Trance: Analog Afterglow",
        "05  /  Minimal Techno: Pattern 3+5", "06  /  Dub Techno: Mono Dub", "07  /  Trance: Trance Cascade", "08  /  Electro: Broken Transmission",
        "09  /  Melodic Techno: Reverse Bloom", "10  /  Festival House: Ducking Lead", "11  /  Breakbeat: Lo-fi Satellites", "12  /  Progressive House: Tape Wash",
        "13  /  Festival Drop Lead", "14  /  Tech House Offbeat", "15  /  Dubstep Stutter Pulse", "16  /  Trance Euphoria 1/8d",
        "17  /  Future Rave Laser", "18  /  Bass House Skank", "19  /  Drum & Bass Rapidfire", "20  /  Hardstyle Toxic Tail",
        "21  /  Deep Melodic Echo", "22  /  Electro Glitch Cloud", "23  /  Trap Triplet Bounce", "24  /  Acid Slapback",
        "25  /  Trance: Gate Lift", "26  /  Progressive: Long Bloom", "27  /  Techno: Ping Pong Dots", "28  /  Psytrance: Stutter Pulse",
        "29  /  Drum & Bass: Double-Time Spray", "30  /  Hardstyle: Reverse Impact", "31  /  Future Bass: Gate Chop", "32  /  Electro: BBD Haze",
        "33  /  Melodic Techno: Wide Bloom", "34  /  Peak-Time Techno: Tape Puncture", "35  /  UK Garage: Triplet Bounce", "36  /  Rave: Dub Feedback Cloud"
    };
    return names[juce::jlimit(0, 35, index)];
}
void DelayProcessor::setCurrentProgram(int index) {
    index=juce::jlimit(0,35,index);program.store(index);
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
    if(index==12){applyPattern(10);setPlainValue(gridRate,6);setPlainValue(feedback,52);setPlainValue(duck,65);setPlainValue(duckMode,1);}
    if(index==13){applyPattern(0);setPlainValue(gridRate,4);setPlainValue(routing,0);setPlainValue(lowPass,3500);setPlainValue(feedback,40);}
    if(index==14){applyPattern(8);setPlainValue(character,3);setPlainValue(glitch,2);setPlainValue(glitchAmount,75);setPlainValue(glitchRate,2);setPlainValue(feedback,60);}
    if(index==15){applyPattern(10);setPlainValue(gridRate,5);setPlainValue(feedback,62);setPlainValue(motion,55);setPlainValue(lowPass,5500);setPlainValue(mix,42);}
    if(index==16){applyPattern(11);setPlainValue(character,1);setPlainValue(glitch,3);setPlainValue(glitchAmount,85);setPlainValue(drive,45);}
    if(index==17){applyPattern(6);setPlainValue(gridRate,4);setPlainValue(routing,1);setPlainValue(feedback,44);setPlainValue(duck,50);}
    if(index==18){applyPattern(8);setPlainValue(gridRate,2);setPlainValue(character,2);setPlainValue(feedback,38);setPlainValue(lowPass,4800);setPlainValue(decay,30);}
    if(index==19){applyPattern(1);setPlainValue(gridRate,9);setPlainValue(character,3);setPlainValue(drive,70);setPlainValue(feedback,65);setPlainValue(highPass,120);}
    if(index==20){applyPattern(3);setPlainValue(gridRate,7);setPlainValue(feedback,68);setPlainValue(decay,50);setPlainValue(motion,45);setPlainValue(lowPass,3800);}
    if(index==21){applyPattern(9);setPlainValue(gridRate,8);setPlainValue(glitch,1);setPlainValue(glitchAmount,60);setPlainValue(glitchRate,3);setPlainValue(feedback,55);}
    if(index==22){applyPattern(7);setPlainValue(gridRate,3);setPlainValue(routing,2);setPlainValue(feedback,46);setPlainValue(duck,40);}
    if(index==23){applyPattern(0);setPlainValue(gridRate,1);setPlainValue(routing,0);setPlainValue(feedback,20);setPlainValue(drive,35);setPlainValue(mix,30);}
    if(index==24){applyPattern(10);setPlainValue(gridRate,10);setPlainValue(glitch,1);setPlainValue(glitchRate,2);setPlainValue(glitchAmount,72);setPlainValue(duck,55);setPlainValue(duckMode,0);setPlainValue(highPass,260);}
    if(index==25){applyPattern(11);setPlainValue(gridRate,15);setPlainValue(character,1);setPlainValue(feedback,72);setPlainValue(motion,68);setPlainValue(lowPass,6800);setPlainValue(mix,52);}
    if(index==26){applyPattern(5);setPlainValue(gridRate,13);setPlainValue(routing,1);setPlainValue(character,0);setPlainValue(feedback,58);setPlainValue(highPass,420);setPlainValue(lowPass,7800);setPlainValue(width,82);}
    if(index==27){applyPattern(9);setPlainValue(gridRate,12);setPlainValue(character,3);setPlainValue(glitch,2);setPlainValue(glitchRate,3);setPlainValue(glitchAmount,84);setPlainValue(duck,35);setPlainValue(duckMode,1);}
    if(index==28){applyPattern(8);setPlainValue(gridRate,0);setPlainValue(character,0);setPlainValue(routing,1);setPlainValue(feedback,32);setPlainValue(highPass,650);setPlainValue(duck,72);setPlainValue(duckMode,1);setPlainValue(duckRate,8);}
    if(index==29){applyPattern(11);setPlainValue(gridRate,14);setPlainValue(glitch,3);setPlainValue(glitchRate,7);setPlainValue(glitchAmount,92);setPlainValue(feedback,62);setPlainValue(highPass,120);setPlainValue(lowPass,5200);}
    if(index==30){applyPattern(6);setPlainValue(gridRate,11);setPlainValue(character,0);setPlainValue(glitch,1);setPlainValue(glitchRate,3);setPlainValue(glitchAmount,78);setPlainValue(duck,48);setPlainValue(mix,45);}
    if(index==31){applyPattern(3);setPlainValue(gridRate,12);setPlainValue(character,2);setPlainValue(drive,62);setPlainValue(feedback,48);setPlainValue(lowPass,4100);setPlainValue(motion,32);}
    if(index==32){applyPattern(11);setPlainValue(gridRate,15);setPlainValue(routing,0);setPlainValue(character,1);setPlainValue(feedback,58);setPlainValue(motion,72);setPlainValue(duck,32);setPlainValue(lowPass,6200);}
    if(index==33){applyPattern(9);setPlainValue(gridRate,13);setPlainValue(character,1);setPlainValue(drive,74);setPlainValue(motion,86);setPlainValue(feedback,64);setPlainValue(highPass,180);setPlainValue(lowPass,3600);}
    if(index==34){applyPattern(10);setPlainValue(gridRate,10);setPlainValue(routing,1);setPlainValue(character,0);setPlainValue(feedback,42);setPlainValue(glitch,2);setPlainValue(glitchRate,6);setPlainValue(glitchAmount,52);setPlainValue(duck,48);}
    if(index==35){applyPattern(11);setPlainValue(gridRate,14);setPlainValue(character,2);setPlainValue(drive,58);setPlainValue(feedback,78);setPlainValue(decay,35);setPlainValue(lowPass,5000);setPlainValue(mix,58);}
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
            if(matches==0) {
                const auto index=static_cast<int>(&spec-parameterSpecs().data());
                if(index<swing)return;
                juce::ValueTree child("PARAM");child.setProperty("id",spec.id,nullptr);child.setProperty("value",spec.initial,nullptr);state.appendChild(child,nullptr);
            } else if(matches!=1)return;
        }
        parameters.replaceState(state);program.store(juce::jlimit(0,35,static_cast<int>(state.getProperty("program",0))));resetPending.store(true);
    }
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new oed::DelayProcessor;}
