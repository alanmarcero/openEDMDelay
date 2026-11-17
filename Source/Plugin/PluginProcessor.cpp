#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <sstream>
#include <locale>
namespace oed {
DelayProcessor::DelayProcessor():AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true)),parameters(*this,nullptr,"openEDMDelay",makeLayout()) {
    for(std::size_t i=0;i<values.size();++i)values[i]=parameters.getRawParameterValue(parameterSpecs()[i].id);
    setCurrentProgram(0);
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
    const auto transport=updateClock(buffer.getNumSamples());
    engine.process(buffer.getWritePointer(0),buffer.getNumChannels()>1?buffer.getWritePointer(1):nullptr,buffer.getNumSamples(),settings(),transport);
    duckMeter.store(engine.duckGain());outputPeak.store(buffer.getMagnitude(0,buffer.getNumSamples()));
}
Transport DelayProcessor::updateClock(int frames) {
    Transport clock{values[tempo]->load(),internalPhase,true};
    bool synced=false, playing=true;
    if(auto* head=getPlayHead(); head && wrapperType!=wrapperType_Standalone) {
        if(auto position=head->getPosition()) {
            playing=position->getIsPlaying();
            if(auto bpm=position->getBpm(); bpm && std::isfinite(*bpm) && *bpm>=20 && *bpm<=400) {
                clock.bpm=*bpm;
                synced=true;
            }
            if(playing) if(auto ppq=position->getPpqPosition(); ppq && std::isfinite(*ppq)) clock.ppq=*ppq;
        }
    }
    displayedBpm.store(clock.bpm);
    hostSync.store(synced);
    if(playing) displayedPhase.store(clock.ppq);
    internalPhase=clock.ppq+frames/currentSampleRate*clock.bpm/60;
    return clock;
}
void DelayProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer,juce::MidiBuffer&) {
    updateClock(buffer.getNumSamples());
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
    const double diffusionCoefficient=diffusionStageFeedback*s.diffusion;
    const double diffusionRepeats=diffusionCoefficient>0?std::ceil(std::log(.0001)/std::log(diffusionCoefficient)):0;
    const double diffusionTail=s.diffusion>0?s.diffusionSize*.001*diffusionStageLengths.back()*diffusionRepeats:0;
    return unit*(latest+last*repeats)+filterTail+diffusionTail;
}
juce::AudioProcessorParameter* DelayProcessor::getBypassParameter()const{return parameters.getParameter("bypass");}
juce::AudioProcessorEditor* DelayProcessor::createEditor(){return new DelayEditor(*this);}
void DelayProcessor::setPlainValue(int id,float value) {
    auto* p=parameters.getParameter(parameterSpecs()[static_cast<std::size_t>(id)].id);
    p->setValueNotifyingHost(p->convertTo0to1(value));
}
namespace {
struct Hit { int step=0, level=0, pan=0; };
struct Rhythm { const char* name; std::array<Hit,tapCount> taps; };
constexpr Rhythm rhythms[]{
    {"Centre Throw",    {{{1,100,0}}}},
    {"Left Answer",     {{{1,96,-65},{3,75,76},{7,36,-30}}}},
    {"Dotted Pair",     {{{2,95,-34},{5,81,58},{8,45,-65},{11,32,30}}}},
    {"Back Alley",      {{{2,94,20},{4,49,-44},{7,89,80},{10,44,-72},{15,61,14}}}},
    {"Three on Two",    {{{2,96,-75},{3,78,75},{6,58,-47},{9,56,48},{12,39,-25}}}},
    {"Crossfade",       {{{1,95,-92},{3,80,-66},{6,67,-31},{8,88,5},{11,61,34},{14,48,70},{16,38,94}}}},
    {"Late Pickup",     {{{3,88,-37},{7,74,64},{12,52,-68},{14,62,32},{15,74,-28},{16,90,15}}}},
    {"Eight Lanterns",  {{{1,96,-80},{3,61,41},{4,83,-34},{6,52,82},{9,77,-65},{10,51,21},{13,72,-17},{15,45,74}}}},
    {"Dub Holes",       {{{1,100,0},{4,71,-60},{10,64,71},{16,40,-23}}}},
    {"Cut and Return",  {{{1,98,53},{2,66,-50},{5,70,82},{7,40,-85},{12,81,-10},{13,45,70}}}},
    {"Triplet Fan",     {{{1,97,-65},{2,79,65},{3,69,-15},{7,72,75},{8,55,-80},{9,50,25}}}},
    {"End Roll",        {{{2,86,-52},{5,53,58},{9,65,-40},{12,72,44},{13,65,-65},{14,71,72},{15,80,-85},{16,95,85}}}}
};

struct Program {
    const char* name;
    int rhythm, grid;
    Routing stereo;
    Character texture;
    int loop, wet, hp, lp;
    std::initializer_list<std::pair<int,float>> controls;
};
constexpr int tapExtra(int tap,int field) {return tapExtraStart+(tap-1)*tapExtraCount+field;}
const Program bank[]{
    {"Lead / Signal Glass",    2,2,Routing::stereo,Character::digital,34,32,260,10800,{{duck,38},{duckMode,1},{decay,33},{width,92}}},
    {"Lead / Second Voice",    1,5,Routing::pingPong,Character::tape,42,29,240,7800,{{motion,22},{drive,19},{duck,43},{duckMode,1}}},
    {"Lead / Prism Row",       7,0,Routing::stereo,Character::digital,29,35,460,12600,{{decay,41},{diffusion,12},{tapExtra(5,1),1}}},
    {"Lead / Struck Silver",   0,8,Routing::stereo,Character::bbd,39,27,380,6400,{{drive,32},{diffusion,21},{diffusionSize,8}}},
    {"Lead / Overpass",        5,6,Routing::stereo,Character::tape,45,34,310,8900,{{motion,32},{wobbleRate,.72f},{patternMorph,12},{tapExtra(3,7),7}}},
    {"Lead / Answering Arc",   6,3,Routing::pingPong,Character::digital,33,31,330,9900,{{duck,46},{duckMode,1},{tapExtra(6,1),2}}},
    {"Groove / Back Door",     3,2,Routing::stereo,Character::bbd,25,28,210,5000,{{groove,1},{drive,39},{duck,16},{tapExtra(2,0),78}}},
    {"Groove / Ghost Step",    9,10,Routing::stereo,Character::loFi,21,30,420,6200,{{swing,32},{glitch,2},{glitchRate,0},{glitchAmount,30},{tapExtra(4,0),56}}},
    {"Groove / Cork Heel",     0,1,Routing::mono,Character::tape,17,23,180,4300,{{drive,48},{motion,9},{duck,10}}},
    {"Groove / Shuffle Bar",   7,2,Routing::stereo,Character::digital,26,31,340,8300,{{groove,1},{decay,38},{tapExtra(6,1),1},{tapExtra(8,0),61}}},
    {"Groove / Three Chairs",  4,4,Routing::pingPong,Character::bbd,32,29,230,6100,{{duck,32},{drive,34},{diffusion,14}}},
    {"Groove / Pocket Skip",  10,1,Routing::stereo,Character::tape,24,28,370,7350,{{groove,2},{motion,13},{tapExtra(3,0),65},{tapExtra(5,1),1}}},
    {"Dub / Send Spring",      8,4,Routing::stereo,Character::tape,66,39,145,3600,{{drive,53},{motion,37},{diffusion,46},{diffusionSize,17}}},
    {"Dub / Low Ceiling",      0,7,Routing::mono,Character::bbd,62,34,95,2600,{{drive,61},{lpSlope,3},{diffusion,23},{duck,14}}},
    {"Dub / Empty Stair",      1,9,Routing::pingPong,Character::digital,51,35,260,5600,{{decay,45},{diffusion,31},{diffusionSize,21}}},
    {"Dub / Brass Rail",       4,11,Routing::stereo,Character::bbd,55,33,190,4150,{{drive,52},{tapExtra(3,2),1},{tapExtra(3,4),2350},{tapExtra(5,5),1}}},
    {"Dub / Flaking Paint",    3,5,Routing::stereo,Character::loFi,47,36,270,4800,{{decay,35},{diffusion,19},{tapExtra(4,0),72}}},
    {"Dub / Far Corner",       8,12,Routing::pingPong,Character::tape,58,39,205,3720,{{motion,50},{wobbleRate,.24f},{diffusion,52},{diffusionSize,23}}},
    {"Breaks / Sidecut",       9,0,Routing::stereo,Character::digital,22,31,660,9800,{{duck,45},{duckMode,1},{tapExtra(4,1),2}}},
    {"Breaks / Snare Relay",   4,3,Routing::pingPong,Character::digital,31,30,520,10500,{{glitch,1},{glitchRate,1},{glitchAmount,29},{tapExtra(2,1),1}}},
    {"Breaks / Ghost Rim",     3,10,Routing::stereo,Character::bbd,23,26,440,6700,{{groove,2},{drive,28},{tapExtra(2,0),65},{tapExtra(5,0),70}}},
    {"Breaks / Brake Roll",   11,2,Routing::stereo,Character::loFi,19,34,480,7300,{{glitch,2},{glitchAmount,40},{glitchRate,1},{tapExtra(7,1),2}}},
    {"Breaks / Needle Drop",   0,11,Routing::mono,Character::digital,26,25,750,9400,{{decay,18},{duck,56},{duckMode,1}}},
    {"Breaks / Switch Hitter",10,6,Routing::pingPong,Character::tape,28,32,410,7700,{{wobbleRate,1.8f},{motion,21},{tapExtra(4,7),6},{patternMorph,28}}},
    {"Glitch / Slice Index",   9,1,Routing::stereo,Character::loFi,37,44,370,7900,{{glitch,2},{glitchRate,2},{glitchAmount,74},{tapExtra(6,1),2}}},
    {"Glitch / Fold Back",     6,7,Routing::pingPong,Character::digital,43,43,285,8350,{{glitch,3},{glitchRate,4},{glitchAmount,65},{diffusion,20}}},
    {"Glitch / Shutter",       7,0,Routing::stereo,Character::digital,27,40,620,11600,{{glitch,1},{glitchRate,10},{glitchAmount,87},{duck,15}}},
    {"Glitch / Torn Splice",  11,3,Routing::stereo,Character::tape,32,39,340,6600,{{glitch,2},{glitchRate,1},{glitchAmount,55},{motion,56},{wobbleShape,4}}},
    {"Glitch / Carbon Copy",   1,5,Routing::mono,Character::loFi,46,38,310,4900,{{glitch,3},{glitchRate,6},{glitchAmount,51},{tapExtra(3,1),1}}},
    {"Glitch / Crosscut",      5,10,Routing::stereo,Character::bbd,33,42,490,8100,{{glitch,1},{glitchRate,3},{glitchAmount,67},{drive,49},{tapExtra(5,0),80}}},
    {"Space / Double Horizon", 1,13,Routing::pingPong,Character::tape,52,38,260,6300,{{motion,34},{wobbleRate,.18f},{diffusion,64},{diffusionSize,25}}},
    {"Space / Long Exposure",  0,15,Routing::stereo,Character::digital,48,46,360,10000,{{diffusion,76},{diffusionSize,29},{duck,56},{duckMode,1}}},
    {"Space / Eight Windows",  7,4,Routing::stereo,Character::bbd,44,36,230,6100,{{drive,30},{diffusion,56},{decay,39},{tapExtra(6,2),1},{tapExtra(6,4),3500}}},
    {"Space / Half Light",     2,9,Routing::stereo,Character::tape,55,40,220,4700,{{motion,47},{wobbleShape,1},{diffusion,49},{diffusionSize,18}}},
    {"Space / Night Ferry",    8,14,Routing::mono,Character::tape,49,35,185,3900,{{drive,45},{motion,38},{diffusion,61},{diffusionSize,22}}},
    {"Space / Vanishing Line", 5,8,Routing::stereo,Character::digital,42,43,440,9300,{{glitch,3},{glitchRate,7},{glitchAmount,26},{diffusion,55},{decay,52}}}
};
}

const juce::StringArray& patternNames() {
    static const auto names=[] {juce::StringArray list; for(const auto& rhythm:rhythms) list.add(rhythm.name); return list;}();
    return names;
}
void DelayProcessor::applyPattern(int index) {
    if(!juce::isPositiveAndBelow(index,static_cast<int>(std::size(rhythms))))return;
    const auto& pattern=rhythms[index];
    for(int i=0;i<tapCount;++i) {
        const auto hit=pattern.taps[static_cast<std::size_t>(i)];
        const int id=baseCount+i*4;
        setPlainValue(id,hit.step>0?1.f:0.f);
        setPlainValue(id+1,static_cast<float>(std::max(1,hit.step)));
        setPlainValue(id+2,static_cast<float>(hit.step?hit.level:70));
        setPlainValue(id+3,static_cast<float>(hit.pan));
    }
}
int DelayProcessor::getNumPrograms(){return static_cast<int>(std::size(bank));}
const juce::String DelayProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index,getNumPrograms()) ? bank[index].name : juce::String{};
}
void DelayProcessor::setCurrentProgram(int index) {
    index=juce::jlimit(0,getNumPrograms()-1,index);
    const auto& sound=bank[index];
    for(int i=0;i<parameterCount;++i) setPlainValue(i,parameterSpecs()[static_cast<std::size_t>(i)].initial);
    applyPattern(sound.rhythm);
    setPlainValue(gridRate,static_cast<float>(sound.grid));
    setPlainValue(routing,static_cast<float>(sound.stereo));
    setPlainValue(character,static_cast<float>(sound.texture));
    setPlainValue(feedback,static_cast<float>(sound.loop));
    setPlainValue(mix,static_cast<float>(sound.wet));
    setPlainValue(highPass,static_cast<float>(sound.hp));
    setPlainValue(lowPass,static_cast<float>(sound.lp));
    for(auto [id,value]:sound.controls) setPlainValue(id,value);
    program.store(index);
    oldBank.store(false);
    resetPending.store(true);
}
void DelayProcessor::getStateInformation(juce::MemoryBlock& block) {
    auto state=parameters.copyState();state.setProperty("program",program.load(),nullptr);state.setProperty("programBank",oldBank.load()?0:1,nullptr);
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
        parameters.replaceState(state);oldBank.store(static_cast<int>(state.getProperty("programBank",0))!=1);program.store(juce::jlimit(0,35,static_cast<int>(state.getProperty("program",0))));resetPending.store(true);
    }
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new oed::DelayProcessor;}
