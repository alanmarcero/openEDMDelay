#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <algorithm>
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
    const int reverseIndex=std::clamp(s.reverseRate,0,static_cast<int>(beatRates.size())-1);
    const double reverseTail=s.reverse>0.01f?beatRates[static_cast<std::size_t>(reverseIndex)]*60/std::max(20.,displayedBpm.load()):0;
    return unit*(latest+last*repeats)+filterTail+diffusionTail+reverseTail;
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
    // Slapback: one short tempo-synced repeat.
    {"Slapback / Thirty Second",   0, 0, Routing::mono,     Character::tape,    10, 28, 160,  6200, {{duck,0},{drive,22},{motion,8},{wobbleRate,.55f},{lpSlope,2}}},
    {"Slapback / Sixteenth",       0, 2, Routing::mono,     Character::bbd,     12, 30, 180,  5400, {{duck,0},{drive,30},{lpSlope,2}}},
    {"Slapback / Triplet",         0, 1, Routing::stereo,   Character::tape,    14, 26, 170,  6400, {{duck,0},{drive,20},{motion,10},{stereoOffset,4.f},{lpSlope,2}}},
    {"Slapback / Dotted 32nd",     0,10, Routing::mono,     Character::digital,  8, 32, 200,  8000, {{duck,0},{lpSlope,2},{hpSlope,2}}},
    {"Slapback / Tape Double",     0, 2, Routing::stereo,   Character::tape,    18, 24, 140,  4800, {{duck,0},{drive,38},{motion,16},{wobbleRate,.7f},{lpSlope,2}}},
    {"Slapback / Snare",           0, 0, Routing::mono,     Character::bbd,      6, 34, 280,  7200, {{duck,0},{drive,42},{lpSlope,2},{hpSlope,2}}},

    // Quarter-note echoes.
    {"Quarter / Straight",         0, 7, Routing::stereo,   Character::digital, 36, 32, 140, 12000, {{duck,0},{lpSlope,2}}},
    {"Quarter / Analog",           0, 7, Routing::mono,     Character::bbd,     40, 30, 120,  4000, {{duck,0},{drive,38},{lpSlope,3}}},
    {"Quarter / Wide",             0, 7, Routing::stereo,   Character::digital, 32, 30, 180, 11000, {{duck,0},{stereoOffset,11.f},{lpSlope,2}}},
    {"Quarter / Throw",            0, 7, Routing::stereo,   Character::tape,    55, 28, 160,  4600, {{duck,0},{drive,32},{motion,16},{wobbleRate,.4f},{lpSlope,3}}},
    {"Quarter / Soft",             0, 7, Routing::stereo,   Character::tape,    26, 24, 200,  6800, {{duck,0},{drive,14},{motion,8},{wobbleRate,.3f},{lpSlope,2}}},
    {"Quarter / Band",             0, 7, Routing::stereo,   Character::digital, 34, 30, 420,  5200, {{duck,0},{hpSlope,3},{lpSlope,3}}},

    // Dotted grid: the push against the beat.
    {"Dotted / Eighth",            0, 5, Routing::stereo,   Character::digital, 36, 32, 180, 11000, {{duck,0},{lpSlope,2}}},
    {"Dotted / Eighth Tape",       0, 5, Routing::stereo,   Character::tape,    40, 30, 160,  5000, {{duck,0},{drive,28},{motion,18},{wobbleRate,.42f},{lpSlope,3}}},
    {"Dotted / Quarter",           0, 8, Routing::stereo,   Character::digital, 34, 30, 150, 10000, {{duck,0},{lpSlope,2}}},
    {"Dotted / Sixteenth",         0,11, Routing::stereo,   Character::digital, 22, 28, 220, 12000, {{duck,0},{lpSlope,2}}},
    {"Dotted / Dub Eighth",        0, 5, Routing::stereo,   Character::tape,    62, 26, 210,  3000, {{duck,0},{drive,44},{motion,22},{wobbleRate,.33f},{lpSlope,3}}},
    {"Dotted / Bright",            0, 5, Routing::stereo,   Character::digital, 30, 30, 380, 14000, {{duck,0},{hpSlope,2},{lpSlope,1}}},

    // Triplet grid.
    {"Triplet / Eighth",           0, 3, Routing::stereo,   Character::digital, 34, 32, 200, 11000, {{duck,0},{lpSlope,2}}},
    {"Triplet / Quarter",          0, 6, Routing::stereo,   Character::digital, 38, 30, 180, 10000, {{duck,0},{lpSlope,2}}},
    {"Triplet / Sixteenth",        0, 1, Routing::stereo,   Character::digital, 26, 28, 280, 12000, {{duck,0},{lpSlope,2}}},
    {"Triplet / Half",             0,12, Routing::stereo,   Character::tape,    42, 28, 160,  5800, {{duck,0},{drive,24},{motion,12},{wobbleRate,.28f},{lpSlope,2}}},
    {"Triplet / Roll",             0, 3, Routing::stereo,   Character::bbd,     30, 30, 180,  4600, {{duck,0},{drive,34},{lpSlope,3}}},
    {"Triplet / Open",             0, 6, Routing::stereo,   Character::digital, 28, 26, 120, 13000, {{duck,0},{lpSlope,1},{stereoOffset,-6.f}}},

    // Ping-pong: one tap, crossfed repeats, no diffusion. Echoes alternate left and right.
    {"Ping-Pong / Eighth",         0, 4, Routing::pingPong, Character::digital, 40, 34, 250, 11000, {{duck,0},{lpSlope,2},{width,100}}},
    {"Ping-Pong / Dotted Eighth",  0, 5, Routing::pingPong, Character::digital, 38, 34, 240, 10500, {{duck,0},{lpSlope,2},{width,100}}},
    {"Ping-Pong / Quarter",        0, 7, Routing::pingPong, Character::digital, 42, 32, 200, 10000, {{duck,0},{lpSlope,2},{width,100}}},
    {"Ping-Pong / Quarter Triplet",0, 6, Routing::pingPong, Character::digital, 40, 32, 220, 10000, {{duck,0},{lpSlope,2},{width,100}}},
    {"Ping-Pong / Sixteenth",      0, 2, Routing::pingPong, Character::digital, 28, 30, 300, 12000, {{duck,0},{lpSlope,2},{width,100}}},
    {"Ping-Pong / Half",           0, 9, Routing::pingPong, Character::tape,    45, 30, 180,  5200, {{duck,0},{drive,18},{motion,10},{wobbleRate,.35f},{lpSlope,2},{width,100}}},

    // Tape and dub: dark feedback, saturation, slow wow.
    {"Tape Dub / Quarter",         0, 7, Routing::stereo,   Character::tape,    60, 30, 180,  2800, {{duck,0},{drive,42},{motion,24},{wobbleRate,.38f},{lpSlope,3}}},
    {"Tape Dub / Dotted",          0, 5, Routing::stereo,   Character::tape,    66, 28, 220,  2400, {{duck,0},{drive,48},{motion,28},{wobbleRate,.32f},{lpSlope,3}}},
    {"Tape Dub / Half",            0, 9, Routing::stereo,   Character::tape,    58, 26, 160,  3000, {{duck,0},{drive,36},{motion,20},{wobbleRate,.25f},{lpSlope,3}}},
    {"Tape Dub / Flutter",         0, 4, Routing::stereo,   Character::tape,    52, 28, 200,  3600, {{duck,0},{drive,30},{motion,48},{wobbleRate,1.1f},{wobbleShape,1},{lpSlope,3}}},
    {"Tape Dub / Dark Loop",       0, 8, Routing::stereo,   Character::tape,    70, 24, 250,  1700, {{duck,0},{drive,55},{motion,16},{wobbleRate,.22f},{lpSlope,4},{hpSlope,2}}},
    {"Tape Dub / Chord",           0,13, Routing::stereo,   Character::tape,    48, 28, 140,  3200, {{duck,0},{drive,34},{motion,16},{wobbleRate,.3f},{lpSlope,3}}},

    // Filtering inside the feedback loop is the sound.
    {"Filtered / Telephone",       0, 4, Routing::stereo,   Character::digital, 42, 32, 850,  2300, {{duck,0},{hpSlope,3},{lpSlope,3}}},
    {"Filtered / Darkening",       0, 7, Routing::stereo,   Character::digital, 55, 30,  80,  3400, {{duck,0},{hpSlope,1},{lpSlope,4}}},
    {"Filtered / Highpass",        0, 5, Routing::stereo,   Character::digital, 36, 30, 720, 16000, {{duck,0},{hpSlope,4},{lpSlope,0}}},
    {"Filtered / Band",            0, 3, Routing::stereo,   Character::digital, 40, 30, 500,  3100, {{duck,0},{hpSlope,3},{lpSlope,3}}},
    {"Filtered / Lo-fi",           0, 2, Routing::stereo,   Character::loFi,    34, 28, 300,  4200, {{duck,0},{hpSlope,2},{lpSlope,2}}},
    {"Filtered / Thin Triplet",    0, 6, Routing::stereo,   Character::digital, 32, 28, 1200, 6000, {{duck,0},{hpSlope,3},{lpSlope,3}}},

    // Echoes tucked under the dry hit, then blooming in the gap.
    {"Ducked / Quarter",           0, 7, Routing::stereo,   Character::digital, 40, 36, 180, 10000, {{duck,55},{duckMode,1},{duckRate,7},{lpSlope,2}}},
    {"Ducked / Eighth",            0, 4, Routing::stereo,   Character::digital, 34, 34, 200, 11000, {{duck,60},{duckMode,1},{duckRate,4},{lpSlope,2}}},
    {"Ducked / Dotted",            0, 5, Routing::stereo,   Character::tape,    38, 32, 180,  7000, {{duck,50},{duckMode,1},{duckRate,5},{drive,18},{motion,10},{lpSlope,2}}},
    {"Ducked / Beat",              0, 7, Routing::stereo,   Character::digital, 36, 34, 160, 10000, {{duck,70},{duckMode,0},{duckRate,7},{duckHold,28},{lpSlope,2}}},
    {"Ducked / Piano",             0, 7, Routing::stereo,   Character::digital, 32, 30, 220,  9000, {{duck,48},{duckMode,1},{duckRate,7},{lpSlope,2}}},
    {"Ducked / Triplet",           0, 3, Routing::stereo,   Character::digital, 34, 32, 200, 10000, {{duck,52},{duckMode,1},{duckRate,3},{lpSlope,2}}},

    // Several taps, low feedback, so the rhythm reads as a pattern.
    {"Multi-Tap / Dotted Pair",    2, 2, Routing::stereo,   Character::digital, 16, 34, 180, 11000, {{duck,0},{decay,30},{lpSlope,2}}},
    {"Multi-Tap / Three Over Two", 4, 4, Routing::stereo,   Character::digital, 18, 32, 160, 10000, {{duck,0},{decay,28},{groove,1},{lpSlope,2}}},
    {"Multi-Tap / Crossfade",      5, 4, Routing::stereo,   Character::digital, 14, 34, 200, 11000, {{duck,0},{decay,22},{lpSlope,2}}},
    {"Multi-Tap / End Roll",      11, 2, Routing::stereo,   Character::digital, 12, 36, 250, 12000, {{duck,0},{decay,18},{lpSlope,2}}},
    {"Multi-Tap / Dub Holes",      8, 4, Routing::stereo,   Character::tape,    22, 30, 160,  4200, {{duck,0},{decay,34},{drive,28},{motion,12},{lpSlope,3}}},
    {"Multi-Tap / Lanterns",       7, 2, Routing::stereo,   Character::digital, 14, 32, 200, 10000, {{duck,0},{decay,26},{lpSlope,2}}},

    // Long, diffused, sometimes shimmered. This is the wash, on purpose.
    {"Ambient / Wash",             0, 9, Routing::stereo,   Character::digital, 48, 32, 200,  8000, {{duck,0},{diffusion,62},{diffusionSize,16},{shimmer,12},{lpSlope,2}}},
    {"Ambient / Shimmer",          0,14, Routing::stereo,   Character::digital, 55, 28, 250, 10000, {{duck,0},{diffusion,42},{diffusionSize,18},{shimmer,48},{lpSlope,2}}},
    {"Ambient / Bloom",            0, 8, Routing::stereo,   Character::tape,    42, 30, 180,  6000, {{duck,0},{diffusion,72},{diffusionSize,22},{drive,14},{motion,12},{lpSlope,2}}},
    {"Ambient / Cloud",            0,13, Routing::stereo,   Character::digital, 50, 28, 300,  7000, {{duck,0},{diffusion,68},{diffusionSize,24},{shimmer,22},{lpSlope,2}}},
    {"Ambient / Modulated",        0, 9, Routing::stereo,   Character::tape,    46, 28, 160,  4800, {{duck,0},{diffusion,38},{diffusionSize,14},{motion,36},{wobbleRate,.24f},{drive,20},{lpSlope,3}}},
    {"Ambient / Long",             0,15, Routing::stereo,   Character::digital, 40, 26, 150,  7800, {{duck,0},{diffusion,58},{diffusionSize,20},{shimmer,18},{lpSlope,2}}},

    // Reversed echoes. Chunk length follows the host grid.
    {"Reverse / Quarter",          0, 7, Routing::stereo,   Character::digital, 26, 36, 180, 10000, {{duck,0},{reverse,100},{reverseRate,7},{lpSlope,2}}},
    {"Reverse / Half",             0, 9, Routing::stereo,   Character::digital, 30, 34, 160,  9000, {{duck,0},{reverse,100},{reverseRate,9},{lpSlope,2}}},
    {"Reverse / Dotted",           0, 5, Routing::stereo,   Character::digital, 24, 34, 200, 10000, {{duck,0},{reverse,100},{reverseRate,5},{lpSlope,2}}},
    {"Reverse / Ping Pong",        0, 7, Routing::pingPong, Character::digital, 32, 34, 200, 10000, {{duck,0},{reverse,100},{reverseRate,7},{lpSlope,2},{width,100}}},
    {"Reverse / Eighth",           0, 4, Routing::stereo,   Character::digital, 20, 32, 220, 11000, {{duck,0},{reverse,100},{reverseRate,4},{lpSlope,2}}},
    {"Reverse / Swell",            0,14, Routing::stereo,   Character::tape,    28, 30, 150,  7000, {{duck,0},{reverse,100},{reverseRate,14},{drive,14},{motion,8},{lpSlope,2}}},

    // Dance piano: house and trance chords and stabs. All divisions are tempo-synced.
    {"Piano / Slap",               0, 0, Routing::mono,     Character::tape,    12, 26, 170,  7000, {{duck,0},{drive,18},{motion,6},{lpSlope,2}}},
    {"Piano / Slap Sixteen",       0, 2, Routing::stereo,   Character::bbd,     14, 24, 190,  6400, {{duck,0},{drive,26},{stereoOffset,7.f},{lpSlope,2}}},
    {"Piano / Quarter Stab",       0, 7, Routing::stereo,   Character::digital, 28, 30, 210, 11000, {{duck,32},{duckMode,1},{duckRate,7},{lpSlope,2}}},
    {"Piano / Quarter Chords",     0, 7, Routing::stereo,   Character::tape,    34, 26, 160,  8200, {{duck,0},{drive,16},{motion,8},{wobbleRate,.36f},{lpSlope,2}}},
    {"Piano / Dotted Eighth",      0, 5, Routing::stereo,   Character::digital, 36, 28, 230, 10000, {{duck,18},{duckMode,1},{duckRate,5},{lpSlope,2}}},
    {"Piano / Reverse Swell",      0, 9, Routing::stereo,   Character::digital, 22, 30, 180,  9000, {{duck,0},{reverse,100},{reverseRate,9},{lpSlope,2}}},
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
    auto state=parameters.copyState();state.setProperty("program",program.load(),nullptr);state.setProperty("programBank",oldBank.load()?0:3,nullptr);
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
        parameters.replaceState(state);oldBank.store(static_cast<int>(state.getProperty("programBank",0))!=3);program.store(juce::jlimit(0,getNumPrograms()-1,static_cast<int>(state.getProperty("program",0))));resetPending.store(true);
    }
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new oed::DelayProcessor;}
