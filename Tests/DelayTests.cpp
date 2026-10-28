#include "DelayEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace oed;
void check(bool ok,const char* why) {if(!ok) throw std::runtime_error(why);}
struct Render {
    DelayEngine engine;
    std::vector<float> l,r;
    double sr;
    Render(double rate=8000,int count=24000):l(static_cast<std::size_t>(count)),r(l.size()),sr(rate) {engine.prepare(rate);}
    void run(Settings s,double bpm=120,int block=127) {
        for(int i=0;i<static_cast<int>(l.size());i+=block)
            engine.process(l.data()+i,r.data()+i,std::min(block,static_cast<int>(l.size())-i),s,{bpm,i/sr*bpm/60,true});
    }
};
Settings clean() {Settings s;s.mix=1;s.feedback=0;s.hpSlope=0;s.lpSlope=0;s.decay=0;return s;}
void timing() {
    for(double sr:{8000.,44100.,48000.,96000.}) for(double bpm:{60.,120.,180.}) {
        const int at=static_cast<int>(std::round(sr*60/bpm)); Render x(sr,at+4);auto s=clean();x.l[0]=1;x.run(s,bpm);
        const double exact=sr*60/bpm; double sum=0,centre=0;
        for(int j=at-1;j<=at+1;++j){sum+=x.l[static_cast<std::size_t>(j)];centre+=j*x.l[static_cast<std::size_t>(j)];}
        check(std::abs(sum-1)<.0001 && std::abs(centre/sum-exact)<.001,"Quarter-note impulse misses beat");
        check(x.l[0]==0,"Wet-only leaks dry");
    }
}
void routing() {
    auto s=clean();s.feedback=.5f;s.routing=Routing::pingPong;
    Render x;x.l[0]=1;x.r[0]=1;x.run(s);
    check(std::abs(x.l[4000]-1)<.001 && std::abs(x.r[4000])<.001,"First ping-pong echo not left");
    check(std::abs(x.r[8000]-.5)<.001 && std::abs(x.l[8000])<.001,"Ping-pong feedback not crossfed");
    s.routing=Routing::mono;Render m;m.l[0]=1;m.run(s);check(std::abs(m.l[4000]-.5)<.001 && m.l==m.r,"Mono wet routing broken");
}
void taps() {
    auto s=clean();s.taps[1]={true,3,1,1};s.taps[0].pan=-1;
    Render x;x.l[0]=1;x.r[0]=1;x.run(s);
    check(x.l[4000]>.49 && x.r[4000]==0,"Tap pan/normalization wrong");
    check(x.r[12000]>.49 && x.l[12000]==0,"Second tap timing wrong");
    s.decay=1;Render d;d.l[0]=1;d.r[0]=1;d.run(s);check(d.r[12000]<x.r[12000]*.7,"Decay does not attenuate late taps");
}
void dryAndReset() {
    auto s=clean();s.mix=0;s.duck=1;s.glitch=Glitch::reverse;
    Render x;for(std::size_t i=0;i<x.l.size();++i) x.l[i]=std::sin(static_cast<float>(i)*.03f)*.4f;
    const auto input=x.l;x.run(s);check(x.l==input,"Wet effects alter dry signal");
    s=clean();x.engine.reset();std::fill(x.l.begin(),x.l.end(),0);std::fill(x.r.begin(),x.r.end(),0);x.run(s);
    check(std::all_of(x.l.begin(),x.l.end(),[](float v){return v==0;}),"Reset leaves stale tail");
}
double energy(const std::vector<float>& x,int begin=0) {double e=0;for(std::size_t i=static_cast<std::size_t>(begin);i<x.size();++i)e+=x[i]*x[i];return e;}
void filters() {
    auto s=clean();s.rate=0;
    auto measure=[&](int slope,bool high) {
        Render x(16000,32000);for(std::size_t i=0;i<x.l.size();++i)x.l[i]=static_cast<float>(std::sin(static_cast<double>(i)*(2*3.141592653589793*(high?100:6000)/16000))*.2);
        auto f=s;f.hpSlope=high?slope:0;f.lpSlope=high?0:slope;f.highPass=1000;f.lowPass=1000;x.run(f);return energy(x.l,16000);
    };
    for(bool high:{false,true}) {double last=measure(0,high);for(int slope:{1,2,3,4}){const double e=measure(slope,high);check(e<last*.55,"Filter slope does not increase rejection");last=e;}}
}
void effects() {
    auto s=clean();s.rate=0;Render base;base.l[0]=1;base.run(s);
    for(auto character:{Character::tape,Character::bbd,Character::loFi}) {Render x;x.l[0]=1;s.character=character;x.run(s);check(x.l!=base.l,"Character does not affect echoes");}
    s=clean();s.duck=1;Render ducked;ducked.l[0]=1;ducked.run(s);check(std::abs(ducked.l[4000])<.001,"Beat duck misses beat");
    s.inputDuck=true;Render input;std::fill(input.l.begin(),input.l.end(),.5f);input.run(s);check(energy(input.l,8000)<5,"Input duck does not suppress echoes");
    for(auto effect:{Glitch::gate,Glitch::stutter,Glitch::reverse}) {
        Render x,y;for(std::size_t i=0;i<x.l.size();++i)x.l[i]=y.l[i]=std::sin(static_cast<float>(i)*.071f)*.3f;
        s=clean();x.run(s);s.glitch=effect;s.glitchAmount=1;y.run(s);check(energy(y.l)>0 && x.l!=y.l,"Glitch is silent or ineffective");
    }
}
void stable() {
    auto s=clean();s.feedback=.97f;s.character=Character::tape;s.drive=1;s.motion=1;s.hpSlope=4;s.lpSlope=4;s.highPass=2000;s.lowPass=2000;
    for(auto& tap:s.taps)tap={true,1,1,0};Render x;x.l[0]=8;x.r[0]=-8;x.run(s,300,1);
    for(float v:x.l)check(std::isfinite(v)&&std::abs(v)<=8,"Unstable extreme feedback");
    Render a,b;for(std::size_t i=0;i<a.l.size();++i)a.l[i]=b.l[i]=std::sin(static_cast<float>(i)*.07f)*.3f;
    s=clean();s.glitch=Glitch::reverse;s.duck=.5f;a.run(s,120,1);b.run(s,120,511);
    for(std::size_t i=0;i<a.l.size();++i)check(std::abs(a.l[i]-b.l[i])<.0001f,"Block size changes processing");
}
void fastTimeAutomation() {
    Render x;auto s=clean();for(std::size_t i=0;i<x.l.size();++i)x.l[i]=x.r[i]=static_cast<float>(i)/20000;
    x.engine.process(x.l.data(),x.r.data(),12000,s,{120,0,true});
    s.taps[0].step=2;x.engine.process(x.l.data()+12000,x.r.data()+12000,64,s,{120,3,true});
    s.taps[0].step=3;x.engine.process(x.l.data()+12064,x.r.data()+12064,64,s,{120,3.016,true});
    check(std::abs(x.l[12064]-x.l[12063])<.01f,"Fast automation restarts from wrong read head");
}
void stutterEdges() {
    Render x;auto s=clean();s.rate=0;s.glitch=Glitch::stutter;s.glitchAmount=1;s.glitchRate=7;
    for(std::size_t i=0;i<x.l.size();++i)x.l[i]=x.r[i]=static_cast<float>(i)/20000;
    x.run(s);check(std::abs(x.l[10000]-x.l[9999])<.025f,"Stutter midpoint clicks");
}
void filterAutomation() {
    Render x;auto s=clean();s.rate=0;s.hpSlope=4;s.highPass=20;
    for(std::size_t i=0;i<x.l.size();++i)x.l[i]=x.r[i]=static_cast<float>(.25*std::sin(i*.035));
    x.engine.process(x.l.data(),x.r.data(),8000,s,{120,0,true});s.highPass=2000;
    x.engine.process(x.l.data()+8000,x.r.data()+8000,4000,s,{120,2,true});
    for(int i=8500;i<9000;++i)check(std::abs(x.l[static_cast<std::size_t>(i)]-x.l[static_cast<std::size_t>(i-1)])<.03f,"Cutoff automation clicks");
}
int main() {
    int failed=0;
    const std::pair<const char*,void(*)()> cases[]={{"fast timing automation",fastTimeAutomation},{"stutter seams",stutterEdges},{"filter automation",filterAutomation},{"beat timing",timing},{"routing",routing},{"taps",taps},{"dry/reset",dryAndReset},{"filters",filters},{"effects",effects},{"stability/block size",stable}};
    for(const auto& [name,test]:cases)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    return failed?1:0;
}
