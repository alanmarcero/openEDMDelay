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
    // Uplifting (16): tight, punchy rhythmic delays locked to trance grooves (zero reverb wash)
    {"Uplifting / Dotted Drive",     2, 2, Routing::pingPong, Character::digital, 29, 32, 310, 11400, {{decay,38},{duck,46},{duckMode,1},{duckRate,7},{width,96}}},
    {"Uplifting / Sixteen Bounce",   1, 2, Routing::pingPong, Character::digital, 34, 30, 340, 12200, {{decay,34},{duck,42},{duckMode,1},{duckRate,7},{swing,16}}},
    {"Uplifting / Triplet Spark",    4, 6, Routing::stereo,   Character::digital, 31, 29, 360, 12800, {{decay,40},{duck,38},{duckMode,1},{stereoOffset,-2.5f},{width,96}}},
    {"Uplifting / Neon Dotted",      0, 5, Routing::pingPong, Character::tape,    35, 31, 290,  9600, {{drive,28},{motion,14},{wobbleRate,.75f},{duck,44},{duckMode,1}}},
    {"Uplifting / Answering Arc",    6, 2, Routing::pingPong, Character::digital, 27, 29, 340, 10800, {{decay,36},{duck,48},{duckMode,1},{tapExtra(6,1),2}}},
    {"Uplifting / Trance Gate",      3, 2, Routing::pingPong, Character::digital, 32, 33, 380, 12400, {{glitch,1},{glitchRate,2},{glitchAmount,58},{duck,40},{duckMode,1}}},
    {"Uplifting / Bar Throw",        0,14, Routing::stereo,   Character::digital, 20, 30, 350, 11200, {{decay,48},{duck,54},{duckMode,1},{duckHold,30}}},
    {"Uplifting / Three Beat Hit",   0,13, Routing::pingPong, Character::digital, 22, 28, 340, 10800, {{decay,46},{duck,50},{duckMode,1},{stereoOffset,3.0f}}},
    {"Uplifting / Pure 138 Dotted",  1,11, Routing::stereo,   Character::digital, 32, 31, 320, 11800, {{decay,38},{duck,50},{duckMode,1},{width,98}}},
    {"Uplifting / Arp Stepper",      7, 2, Routing::pingPong, Character::digital, 26, 30, 410, 12600, {{decay,42},{swing,12},{duck,36},{duckMode,1},{tapExtra(4,2),1},{tapExtra(4,3),900}}},
    {"Uplifting / Six Beat Throw",   0,15, Routing::stereo,   Character::tape,    18, 28, 360,  9800, {{decay,52},{drive,24},{duck,56},{duckMode,1},{duckHold,35}}},
    {"Uplifting / Starlight Answer", 0, 8, Routing::pingPong, Character::digital, 28, 30, 330, 11600, {{decay,42},{duck,44},{duckMode,1},{stereoOffset,4.0f}}},
    {"Uplifting / Skyward Pluck",    4, 4, Routing::pingPong, Character::bbd,     30, 29, 270,  8200, {{drive,26},{decay,36},{duck,38},{duckHold,26}}},
    {"Uplifting / Prism Row",        7, 0, Routing::stereo,   Character::digital, 24, 31, 460, 12600, {{decay,44},{tapExtra(5,1),1},{tapExtra(7,1),1}}},
    {"Uplifting / Triplet Phrase",   0,12, Routing::pingPong, Character::digital, 24, 28, 340, 11000, {{decay,44},{duck,46},{duckMode,1},{duckHold,24}}},
    {"Uplifting / Backbeat Throw",   0, 9, Routing::pingPong, Character::tape,    26, 29, 320, 10200, {{drive,24},{decay,42},{duck,48},{duckMode,1},{duckHold,28}}},

    // Rolling (8): pocket 16ths, triplet shuffles, muted bassline-safe skips
    {"Rolling / Offbeat Snap",       5, 2, Routing::pingPong, Character::digital, 24, 30, 360,  9400, {{groove,1},{duck,52},{duckRate,4},{duckHold,42},{stereoOffset,3.0f}}},
    {"Rolling / Pocket Skip",       10, 1, Routing::stereo,   Character::tape,    22, 28, 380,  7800, {{groove,2},{motion,10},{tapExtra(3,0),68},{tapExtra(5,1),1}}},
    {"Rolling / Bassline Tuck",      8, 7, Routing::mono,     Character::bbd,     20, 26, 260,  4600, {{drive,48},{decay,42},{duck,66},{duckMode,1},{duckHold,52}}},
    {"Rolling / Three Chairs",       4, 4, Routing::pingPong, Character::bbd,     26, 28, 280,  6400, {{decay,38},{duck,36},{drive,32}}},
    {"Rolling / Shuffle Bar",        7, 2, Routing::stereo,   Character::digital, 24, 30, 350,  8600, {{groove,1},{decay,42},{tapExtra(6,1),1},{tapExtra(8,0),65}}},
    {"Rolling / Dotted Pocket",      1,11, Routing::stereo,   Character::tape,    27, 29, 310,  7600, {{swing,36},{drive,30},{motion,14},{tapExtra(2,1),1}}},
    {"Rolling / Triplet Drive",      3, 3, Routing::stereo,   Character::digital, 25, 31, 410, 10200, {{groove,1},{decay,36},{duck,42},{duckRate,3},{stereoOffset,-4.f}}},
    {"Rolling / Back Door",          3, 2, Routing::stereo,   Character::bbd,     23, 27, 250,  5600, {{groove,1},{drive,36},{decay,40},{duck,28},{tapExtra(2,0),78}}},

    // Progressive (8): tight syncopated dotted grooves & sculpted analog taps
    {"Progressive / Dotted Groove",  0, 5, Routing::pingPong, Character::tape,    32, 29, 260,  7200, {{drive,28},{motion,16},{wobbleRate,.35f},{decay,42},{duck,42},{duckMode,1}}},
    {"Progressive / Half Step",      8, 9, Routing::stereo,   Character::tape,    24, 28, 280,  5800, {{drive,30},{motion,14},{wobbleShape,1},{decay,48},{duck,44}}},
    {"Progressive / Dotted Pair",    2, 2, Routing::pingPong, Character::bbd,     30, 30, 260,  6800, {{drive,32},{decay,40},{stereoOffset,-4.5f},{duck,38},{duckMode,1}}},
    {"Progressive / Cross Pocket",   5, 2, Routing::stereo,   Character::tape,    28, 29, 240,  5400, {{drive,36},{motion,18},{swing,24},{decay,42},{tapExtra(3,2),1},{tapExtra(3,4),3200}}},
    {"Progressive / Eight Steps",    7, 4, Routing::stereo,   Character::bbd,     25, 28, 270,  6200, {{drive,30},{decay,44},{tapExtra(6,2),1},{tapExtra(6,4),3500},{duck,34}}},
    {"Progressive / Velvet Pluck",   2, 4, Routing::pingPong, Character::bbd,     28, 29, 240,  6900, {{drive,28},{stereoOffset,-4.5f},{decay,38},{duck,36},{duckHold,24}}},
    {"Progressive / Triplet Pocket", 4, 6, Routing::stereo,   Character::digital, 26, 30, 390,  9400, {{decay,40},{duck,38},{tapExtra(3,1),1},{tapExtra(5,2),1},{tapExtra(5,4),4200}}},
    {"Progressive / Dotted Answer",  1, 8, Routing::pingPong, Character::tape,    24, 28, 270,  6400, {{drive,28},{motion,12},{wobbleRate,.42f},{decay,46},{duck,42}}},

    // Psy (8): razor-sharp triplet patterns, gated chops & ratchet stabs
    {"Psy / Triplet Pulse",          4, 3, Routing::pingPong, Character::digital, 31, 33, 450, 11400, {{glitch,1},{glitchRate,3},{glitchAmount,48},{decay,36},{stereoOffset,4.0f}}},
    {"Psy / Cyber Ratchet",          5, 2, Routing::pingPong, Character::bbd,     29, 34, 510,  9200, {{glitch,1},{glitchRate,2},{glitchAmount,62},{tapExtra(1,1),4},{tapExtra(2,1),3}}},
    {"Psy / Fold Back",              6, 7, Routing::pingPong, Character::digital, 25, 32, 340,  9600, {{glitch,3},{glitchRate,2},{glitchAmount,52},{decay,42}}},
    {"Psy / Triplet Fill",           9, 1, Routing::stereo,   Character::digital, 28, 33, 430, 11600, {{glitch,2},{glitchRate,1},{glitchAmount,54},{decay,38},{tapExtra(1,1),2}}},
    {"Psy / Buffer Flip",           11, 2, Routing::stereo,   Character::digital, 30, 32, 360, 10400, {{glitch,3},{glitchRate,2},{glitchAmount,58},{stereoOffset,7.5f},{decay,40}}},
    {"Psy / Laser Comb",             7, 0, Routing::pingPong, Character::digital, 42, 33, 520, 13400, {{decay,46},{width,96},{tapExtra(2,1),2},{tapExtra(5,1),2}}},
    {"Psy / Acid Return",            9, 2, Routing::stereo,   Character::bbd,     30, 31, 380,  8200, {{drive,52},{glitch,2},{glitchRate,0},{glitchAmount,38},{decay,36}}},
    {"Psy / Spiral Matrix",          4, 1, Routing::pingPong, Character::tape,    32, 32, 340, 10200, {{patternMorph,40},{motion,22},{wobbleRate,2.4f},{tapExtra(4,7),10},{decay,38}}},

    // Tech (8): dry slamming club grooves, stutter chops & mechanical micro-delays
    {"Tech / Shutter",               7, 0, Routing::stereo,   Character::digital, 22, 34, 620, 11600, {{glitch,1},{glitchRate,0},{glitchAmount,78},{decay,38},{duck,24}}},
    {"Tech / Cork Heel",             0, 1, Routing::mono,     Character::tape,    18, 24, 220,  4800, {{drive,48},{motion,8},{decay,40},{duck,22}}},
    {"Tech / Ghost Step",            9,10, Routing::stereo,   Character::loFi,    21, 30, 420,  6400, {{swing,34},{glitch,2},{glitchRate,0},{glitchAmount,34},{tapExtra(4,0),60}}},
    {"Tech / Slice Index",           9, 2, Routing::stereo,   Character::loFi,    28, 36, 410,  8600, {{glitch,2},{glitchRate,2},{glitchAmount,66},{tapExtra(6,1),2},{decay,36}}},
    {"Tech / Carbon Copy",           1, 2, Routing::mono,     Character::loFi,    30, 32, 340,  5600, {{glitch,1},{glitchRate,2},{glitchAmount,50},{tapExtra(3,1),2},{decay,40}}},
    {"Tech / Dotted Micro",          8,10, Routing::pingPong, Character::loFi,    26, 33, 460,  7800, {{glitch,2},{glitchRate,0},{glitchAmount,62},{wobbleShape,4},{motion,24}}},
    {"Tech / Torn Splice",          11, 2, Routing::stereo,   Character::tape,    25, 32, 360,  6800, {{glitch,2},{glitchRate,1},{glitchAmount,48},{motion,28},{wobbleShape,4}}},
    {"Tech / Warehouse Slap",        0, 2, Routing::stereo,   Character::bbd,     22, 28, 280,  6200, {{drive,42},{decay,46},{duck,36},{tapExtra(1,6),65}}},

    // Bass & Breaks (8): synced chopper rhythms, breakbeat relays, tight high-passed punch
    {"Bass & Breaks / Amen Chopper", 7, 1, Routing::stereo,   Character::digital, 25, 31, 560, 11200, {{glitch,2},{glitchRate,2},{glitchAmount,46},{duck,42},{duckHold,38}}},
    {"Bass & Breaks / Jungle Rattler",10,3,Routing::pingPong, Character::loFi,    28, 30, 500,  8400, {{glitch,1},{glitchRate,1},{glitchAmount,36},{tapExtra(1,1),3},{tapExtra(3,1),2}}},
    {"Bass & Breaks / Neuro Scatter",6, 0, Routing::pingPong, Character::digital, 24, 30, 680, 12800, {{groove,2},{glitch,3},{glitchRate,3},{glitchAmount,40},{stereoOffset,-5.f}}},
    {"Bass & Breaks / Sidecut",      9, 2, Routing::stereo,   Character::digital, 20, 29, 660,  9800, {{duck,50},{duckMode,1},{decay,42},{tapExtra(4,1),2}}},
    {"Bass & Breaks / Snare Relay",  4, 3, Routing::pingPong, Character::digital, 26, 29, 520, 10500, {{glitch,1},{glitchRate,1},{glitchAmount,30},{tapExtra(2,1),1}}},
    {"Bass & Breaks / Brake Roll",  11, 2, Routing::stereo,   Character::loFi,    19, 32, 480,  7600, {{glitch,2},{glitchAmount,38},{glitchRate,1},{tapExtra(7,1),2}}},
    {"Bass & Breaks / Switch Hit",  10, 6, Routing::pingPong, Character::tape,    24, 30, 440,  7900, {{wobbleRate,1.8f},{motion,16},{tapExtra(4,7),6},{patternMorph,28}}},
    {"Bass & Breaks / Crosscut",     5,10, Routing::stereo,   Character::bbd,     27, 34, 510,  8300, {{glitch,1},{glitchRate,3},{glitchAmount,58},{drive,44},{tapExtra(5,0),80}}},

    // Classic (8): tight tape heads, crisp BBD slapbacks & dub chord bounces
    {"Classic / Tape Head Bounce",   8, 4, Routing::stereo,   Character::tape,    34, 30, 210,  4600, {{drive,46},{motion,18},{decay,42},{duck,26}}},
    {"Classic / Dub Chord Hit",      0, 5, Routing::pingPong, Character::tape,    38, 32, 190,  4100, {{drive,54},{motion,22},{wobbleRate,.45f},{decay,44},{duck,38},{duckMode,1}}},
    {"Classic / Bucket Slap",        0, 7, Routing::mono,     Character::bbd,     30, 28, 170,  3400, {{drive,52},{lpSlope,3},{decay,48},{duck,28}}},
    {"Classic / Dotted Gold",        4,11, Routing::stereo,   Character::bbd,     32, 30, 240,  5200, {{drive,44},{tapExtra(3,2),1},{tapExtra(3,4),2800},{tapExtra(5,5),1}}},
    {"Classic / Smoked Tape",        9, 4, Routing::stereo,   Character::tape,    33, 30, 220,  4400, {{drive,52},{motion,24},{wobbleShape,3},{stereoOffset,-4.5f},{decay,42}}},
    {"Classic / Hardware Slapback",  0,10, Routing::stereo,   Character::bbd,     18, 26, 360,  6800, {{drive,32},{decay,50},{tapExtra(1,5),2}}},
    {"Classic / Studio Sixteenth",   2, 2, Routing::stereo,   Character::tape,    28, 29, 310,  8600, {{drive,34},{motion,16},{wobbleRate,.72f},{patternMorph,12},{tapExtra(3,7),7}}},
    {"Classic / Eighth Stepper",     1, 4, Routing::pingPong, Character::digital, 29, 30, 290,  7200, {{decay,44},{duck,32},{duckMode,1},{stereoOffset,3.5f}}}
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
    auto state=parameters.copyState();state.setProperty("program",program.load(),nullptr);state.setProperty("programBank",oldBank.load()?0:2,nullptr);
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
        parameters.replaceState(state);oldBank.store(static_cast<int>(state.getProperty("programBank",0))!=2);program.store(juce::jlimit(0,getNumPrograms()-1,static_cast<int>(state.getProperty("program",0))));resetPending.store(true);
    }
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter(){return new oed::DelayProcessor;}
