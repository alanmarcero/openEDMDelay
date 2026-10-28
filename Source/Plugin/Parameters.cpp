#include "Parameters.h"
namespace oed {
const juce::StringArray& rateNames() {static const juce::StringArray names{"1/32","1/16 T","1/16","1/8 T","1/8","1/8 D","1/4 T","1/4","1/4 D","1/2"};return names;}
const std::array<ParameterSpec,parameterCount>& parameterSpecs() {
    static const auto specs=[] {
        const juce::String rates=rateNames().joinIntoString("|");
        std::array<ParameterSpec,parameterCount> a{{
            {"rate","Grid",0,9,2,1,rates,""},
            {"routing","Routing",0,2,1,1,"Stereo|Ping Pong|Mono",""},
            {"character","Character",0,3,1,1,"Digital|Tape|BBD|Lo-fi",""},
            {"feedback","Feedback",0,97,38,1,"","%"},
            {"decay","Decay",0,100,28,1,"","%"},
            {"highPass","High pass",20,20000,180,.25f,"","Hz"},
            {"hpSlope","HP slope",0,4,2,1,"Off|6 dB/oct|12 dB/oct|24 dB/oct|48 dB/oct",""},
            {"lowPass","Low pass",20,20000,8500,.25f,"","Hz"},
            {"lpSlope","LP slope",0,4,3,1,"Off|6 dB/oct|12 dB/oct|24 dB/oct|48 dB/oct",""},
            {"drive","Drive",0,100,22,1,"","%"},
            {"motion","Motion",0,100,18,1,"","%"},
            {"duck","Duck",0,100,25,1,"","%"},
            {"duckMode","Duck source",0,1,0,1,"Beat|Input",""},
            {"duckRate","Duck rate",0,9,7,1,rates,""},
            {"glitch","Glitch",0,3,0,1,"Off|Gate|Stutter|Reverse",""},
            {"glitchRate","Glitch rate",0,9,2,1,rates,""},
            {"glitchAmount","Glitch mix",0,100,45,1,"","%"},
            {"mix","Mix",0,100,32,1,"","%"},
            {"width","Width",0,100,100,1,"","%"},
            {"output","Output",-24,12,0,1,"","dB"},
            {"tempo","Internal BPM",20,400,120,1,"","BPM"},
            {"bypass","Bypass",0,1,0,1,"Off|On",""}
        }};
        constexpr int steps[]{3,5,8,11,13,14,15,16};
        for(int i=0;i<tapCount;++i) {
            const auto prefix="tap"+juce::String(i+1);const auto title="Tap "+juce::String(i+1)+" ";
            const auto index=static_cast<std::size_t>(baseCount+i*4);
            a[index]={prefix+"Enabled",title+"enabled",0,1,i<4?1.f:0.f,1,"Off|On",""};
            a[index+1]={prefix+"Step",title+"step",1,16,static_cast<float>(steps[i]),1,"",""};
            a[index+2]={prefix+"Level",title+"level",0,100,i==0?100.f:80.f,1,"","%"};
            a[index+3]={prefix+"Pan",title+"pan",-100,100,i%2?65.f:-65.f,1,"",""};
        }
        return a;
    }();return specs;
}
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for(std::size_t i=0;i<parameterSpecs().size();++i) {
        const auto& s=parameterSpecs()[i];const juce::ParameterID id{s.id,1};
        if(i==bypass|| (i>=baseCount&&(i-baseCount)%4==0))layout.add(std::make_unique<juce::AudioParameterBool>(id,s.name,s.initial>.5f));
        else if(s.choices.isNotEmpty())layout.add(std::make_unique<juce::AudioParameterChoice>(id,s.name,juce::StringArray::fromTokens(s.choices,"|",""),static_cast<int>(s.initial)));
        else {
            const float interval=(i>=baseCount&&(i-baseCount)%4==1)?1.f:.01f;
            juce::NormalisableRange<float> range{s.min,s.max,interval,s.skew};
            layout.add(std::make_unique<juce::AudioParameterFloat>(id,s.name,range,s.initial,juce::AudioParameterFloatAttributes{}.withLabel(s.unit)));
        }
    }return layout;
}
Settings readSettings(const ParameterValues& v) {
    const auto get=[&](int p){return v[static_cast<std::size_t>(p)]->load(std::memory_order_relaxed);};
    Settings s;s.rate=static_cast<int>(get(gridRate));s.routing=static_cast<Routing>(static_cast<int>(get(routing)));s.character=static_cast<Character>(static_cast<int>(get(character)));
    s.feedback=get(feedback)*.01f;s.decay=get(decay)*.01f;s.highPass=get(highPass);s.lowPass=get(lowPass);s.hpSlope=static_cast<int>(get(hpSlope));s.lpSlope=static_cast<int>(get(lpSlope));
    s.drive=get(drive)*.01f;s.motion=get(motion)*.01f;s.duck=get(duck)*.01f;s.inputDuck=get(duckMode)>.5f;s.duckRate=static_cast<int>(get(duckRate));
    s.glitch=static_cast<Glitch>(static_cast<int>(get(glitch)));s.glitchRate=static_cast<int>(get(glitchRate));s.glitchAmount=get(glitchAmount)*.01f;
    s.mix=get(mix)*.01f;s.width=get(width)*.01f;s.outputDb=get(output);
    for(int i=0;i<tapCount;++i){const int p=baseCount+i*4;s.taps[static_cast<std::size_t>(i)]={get(p)>.5f,static_cast<int>(get(p+1)),get(p+2)*.01f,get(p+3)*.01f};}
    return s;
}
}
