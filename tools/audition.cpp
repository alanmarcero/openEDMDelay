#include "PluginProcessor.h"
#include "signals.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>
using namespace oed;
struct Host:juce::AudioPlayHead {
    PositionInfo info;
    juce::Optional<PositionInfo> getPosition() const override { return info; }
};
int findProgram(DelayProcessor& processor, const juce::String& name) {
    for(int i=0;i<processor.getNumPrograms();++i) if(processor.getProgramName(i)==name) return i;
    std::cerr<<"Missing preset "<<name<<"\n";
    return -1;
}
struct Rendered { std::vector<float> l,r; };
Rendered renderProgram(DelayProcessor& processor, int index, const std::vector<float>& inL, const std::vector<float>& inR, double bpm, int sr) {
    processor.setCurrentProgram(index);
    processor.prepareToPlay(sr,256);
    Host host; host.info.setBpm(bpm); host.info.setIsPlaying(true); host.info.setPpqPosition(0);
    processor.setPlayHead(&host);
    Rendered out{inL,inR};
    juce::MidiBuffer midi;
    const int n=static_cast<int>(out.l.size());
    for(int i=0;i<n;i+=256) {
        const int frames=std::min(256,n-i);
        float* channels[]={out.l.data()+i,out.r.data()+i};
        juce::AudioBuffer<float> block(channels,2,frames);
        host.info.setPpqPosition(i/static_cast<double>(sr)*bpm/60.0);
        processor.processBlock(block,midi);
    }
    processor.setPlayHead(nullptr);
    return out;
}
void report(const char* file, const Rendered& out, int sr) {
    float peak=0; double dc=0; int clips=0; int last=0;
    for(int i=0;i<static_cast<int>(out.l.size());++i) {
        const float left=out.l[static_cast<std::size_t>(i)], right=out.r[static_cast<std::size_t>(i)];
        peak=std::max(peak,std::max(std::abs(left),std::abs(right)));
        if(std::abs(left)>1.f||std::abs(right)>1.f) ++clips;
        dc+=left+right;
        if(std::max(std::abs(left),std::abs(right))>.005f) last=i;
    }
    std::printf("%s peak %.3f clips %d dc %.5f tail %.2fs\n", file, peak, clips, dc/(2.0*out.l.size()), last/static_cast<double>(sr));
}
int main(int argc, char** argv) {
    const std::string dir=argc>1?argv[1]:"/opt/cursor/artifacts/audition/";
    juce::File(dir).createDirectory();
    constexpr int sr=44100; constexpr double bpm=126; constexpr int n=sr*6;
    const std::vector<float> silence(static_cast<std::size_t>(n),0.f);
    auto clapL=silence, clapR=silence, pluckL=silence, pluckR=silence, stabL=silence, stabR=silence, chordL=silence, chordR=silence;
    audition::addClap(clapL,clapR,static_cast<int>(.2*sr),sr);
    audition::addClap(clapL,clapR,static_cast<int>(2.8*sr),sr);
    audition::addPluck(pluckL,pluckR,static_cast<int>(.2*sr),523.25f,sr);
    audition::addPluck(pluckL,pluckR,static_cast<int>(2.8*sr),659.25f,sr);
    audition::addPianoChord(stabL,stabR,static_cast<int>(.2*sr),sr,true);
    audition::addPianoChord(stabL,stabR,static_cast<int>(2.8*sr),sr,true);
    audition::addPianoChord(chordL,chordR,static_cast<int>(.25*sr),sr,false);
    audition::normalize(clapL,clapR,.7f); audition::normalize(pluckL,pluckR,.7f);
    audition::normalize(stabL,stabR,.7f); audition::normalize(chordL,chordR,.7f);
    const auto pick=[&](const char* which)->std::pair<const std::vector<float>*,const std::vector<float>*>{
        if(std::string(which)=="clap") return {&clapL,&clapR};
        if(std::string(which)=="pluck") return {&pluckL,&pluckR};
        if(std::string(which)=="chord") return {&chordL,&chordR};
        return {&stabL,&stabR};
    };
    struct Demo { const char* file; const char* program; const char* src; };
    const Demo demos[]={
        {"after-slapback.wav","Slapback / Sixteenth","clap"},
        {"after-quarter.wav","Quarter / Straight","stab"},
        {"after-dotted.wav","Dotted / Eighth","pluck"},
        {"after-triplet.wav","Triplet / Eighth","pluck"},
        {"after-pingpong.wav","Ping-Pong / Dotted Eighth","clap"},
        {"after-tapedub.wav","Tape Dub / Dotted","chord"},
        {"after-filtered.wav","Filtered / Telephone","pluck"},
        {"after-ducked.wav","Ducked / Quarter","stab"},
        {"after-multitap.wav","Multi-Tap / Crossfade","clap"},
        {"after-ambient.wav","Ambient / Wash","chord"},
        {"after-reverse.wav","Reverse / Quarter","pluck"},
    };
    juce::ScopedJuceInitialiser_GUI gui;
    DelayProcessor processor;
    for(const auto& demo:demos) {
        const int index=findProgram(processor,demo.program);
        if(index<0) return 1;
        const auto inputs=pick(demo.src);
        auto rendered=renderProgram(processor,index,*inputs.first,*inputs.second,bpm,sr);
        audition::writeWav16(dir+demo.file,rendered.l,rendered.r,sr);
        report(demo.file,rendered,sr);
    }
    const int section=sr*4;
    const char* pianoPrograms[]={"Piano / Slap","Piano / Quarter Stab","Piano / Dotted Eighth","Piano / Reverse Swell"};
    std::vector<float> pianoL(static_cast<std::size_t>(section*4)), pianoR=pianoL;
    for(int part=0;part<4;++part) {
        const int index=findProgram(processor,pianoPrograms[part]);
        if(index<0) return 1;
        std::vector<float> left(static_cast<std::size_t>(section)), right=left;
        audition::addPianoChord(left,right,static_cast<int>(.15*sr),sr,part<2);
        audition::normalize(left,right,.7f);
        auto rendered=renderProgram(processor,index,left,right,bpm,sr);
        std::copy(rendered.l.begin(),rendered.l.end(),pianoL.begin()+static_cast<std::size_t>(part*section));
        std::copy(rendered.r.begin(),rendered.r.end(),pianoR.begin()+static_cast<std::size_t>(part*section));
        std::printf("piano %s\n", pianoPrograms[part]);
    }
    audition::writeWav16(dir+"after-piano.wav",pianoL,pianoR,sr);
    report("after-piano.wav",Rendered{pianoL,pianoR},sr);
    return 0;
}
