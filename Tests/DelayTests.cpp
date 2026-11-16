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
void extendedGridTiming() {
    check(beatRates.size()==16,"EDM grid does not expose all 16 host-synced divisions");
    for(std::size_t index=0;index<beatRates.size();++index) {
        constexpr double sr=8000,bpm=120;
        const double exact=sr*60/bpm*beatRates[index];
        Render x(sr,static_cast<int>(std::ceil(exact))+8);auto s=clean();s.rate=static_cast<int>(index);x.l[0]=1;x.run(s,bpm);
        double sum=0,centre=0;
        for(int j=std::max(1,static_cast<int>(std::floor(exact))-2);j<=static_cast<int>(std::ceil(exact))+2;++j) {
            const auto sample=x.l[static_cast<std::size_t>(j)];sum+=sample;centre+=j*sample;
        }
        check(std::abs(sum-1)<.0001 && std::abs(centre/sum-exact)<.001,"Extended grid division misses its host-synced beat position");
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
void tapPerformance() {
    auto s=clean();s.taps[0].probability=0;
    Render muted;muted.l[0]=1;muted.run(s);check(energy(muted.l)==0,"Zero probability leaks echoes");
    s.taps[0].probability=1;s.taps[0].ratchets=4;
    Render rolls;rolls.l[0]=1;rolls.run(s);
    for(int at:{4000,5000,6000,7000})check(std::abs(rolls.l[static_cast<std::size_t>(at)]-.25f)<.001f,"Ratchet timing/normalization wrong");
    s.taps[0].probability=.5f;Render a,b;
    for(int i=0;i<24000;i+=1000)a.l[static_cast<std::size_t>(i)]=b.l[static_cast<std::size_t>(i)]=.5f;
    Render certain;certain.l=a.l;auto always=s;always.taps[0].probability=1;certain.run(always);
    a.run(s,120,1);b.run(s,120,511);check(a.l==b.l,"Probability depends on block size");
    check(energy(a.l)>0&&energy(a.l)<energy(certain.l),"Fractional probability does not select phrases");
}
void tapTone() {
    auto s=clean();Render plain;plain.l[0]=1;plain.run(s);
    s.taps[0].lowPass=200;s.taps[0].filter=true;Render filtered;filtered.l[0]=1;filtered.run(s);
    check(energy(filtered.l)<energy(plain.l)*.3,"Per-tap low pass ineffective");
    s.taps[0].filter=false;s.taps[0].character=3;Render dirty;dirty.l[0]=.123f;dirty.run(s);
    check(std::abs(dirty.l[4000]-.123f)>.001,"Tap character ineffective");
}
void grooveMorph() {
    auto s=clean();s.taps[0].step=2;s.swing=.5f;
    Render swing;swing.l[0]=1;swing.run(s);check(swing.l[9000]>.99f,"Swing misses offbeat");
    s.swing=0;s.groove=1;Render groove;groove.l[0]=1;groove.run(s);check(groove.l[9000]>.99f,"Groove template ineffective");
    s.groove=0;s.taps[0].morphStep=4;s.morph=.5f;
    Render morph;morph.l[0]=1;morph.run(s);check(morph.l[12000]>.99f,"Pattern morph misses midpoint");
}
void diffusion() {
    auto s=clean();s.rate=0;
    Render plain(8000,10000),diffuse(8000,10000);
    plain.l[0]=diffuse.l[0]=1;plain.run(s);
    s.diffusion=.8f;s.diffusionSize=12;diffuse.l[0]=1;diffuse.run(s);
    const auto spread=std::count_if(diffuse.l.begin()+501,diffuse.l.begin()+700,[](float v){return std::abs(v)>.001f;});
    check(spread>3&&plain.l!=diffuse.l,"Diffusion does not smear the echo");
    s.diffusionSize=24;Render larger(8000,10000);larger.l[0]=1;larger.run(s);
    check(larger.l!=diffuse.l,"Diffusion size has no effect");
}
void wobbleRate() {
    auto s=clean();s.character=Character::tape;s.motion=1;s.drive=0;
    Render slow(8000,12000),fast(8000,12000);
    for(std::size_t i=0;i<slow.l.size();++i)slow.l[i]=fast.l[i]=std::sin(static_cast<float>(i)*.071f)*.3f;
    s.wobbleRate=.25f;slow.run(s);
    s.wobbleRate=2.f;fast.run(s);
    check(slow.l!=fast.l,"Wobble rate does not change tape modulation");
}
void wobbleShape() {
    auto s=clean();s.character=Character::tape;s.motion=1;s.drive=0;s.wobbleRate=1.3f;
    Render sine(8000,12000),square(8000,12000);
    for(std::size_t i=0;i<sine.l.size();++i)sine.l[i]=square.l[i]=std::sin(static_cast<float>(i)*.071f)*.3f;
    s.wobbleShape=WobbleShape::sine;sine.run(s,120,127);
    s.wobbleShape=WobbleShape::square;square.run(s,120,511);
    check(sine.l!=square.l,"Wobble shape does not change tape modulation");
    Render walkA(8000,12000),walkB(8000,12000),sampleHold(8000,12000);
    for(std::size_t i=0;i<walkA.l.size();++i)walkA.l[i]=walkB.l[i]=sampleHold.l[i]=std::sin(static_cast<float>(i)*.071f)*.3f;
    s.wobbleShape=WobbleShape::randomWalk;walkA.run(s,120,127);walkB.run(s,120,511);
    s.wobbleShape=WobbleShape::sampleHold;sampleHold.run(s,120,127);
    check(walkA.l==walkB.l&&walkA.l!=sampleHold.l,"Random wobble depends on block size or shape");
}
void freezeLoop() {
    auto s=clean();s.freeze=true;Render empty;empty.l[0]=1;empty.run(s);check(energy(empty.l)==0,"Freeze admits fresh input");
    Render held(8000,32000);held.l[0]=.5f;
    held.engine.process(held.l.data(),held.r.data(),2000,s=clean(),{120,0,true});
    s.freeze=true;held.engine.process(held.l.data()+2000,held.r.data()+2000,30000,s,{120,.5,true});
    check(held.l[28000]>.49f,"Frozen phrase decays");
    held.engine.reset();std::fill(held.l.begin(),held.l.end(),0);held.run(s);check(energy(held.l)==0,"Reset preserves frozen history");
}
void sustainedHold() {
    auto s=clean();s.rate=0;
    Render held(48000,480000);held.l[0]=.5f;
    held.engine.process(held.l.data(),held.r.data(),100,s,{400,0,true});
    s.freeze=true;held.engine.process(held.l.data()+100,held.r.data()+100,479900,s,{400,100./7200,true});
    check(held.l[18000]>.01f,"Hold lost the captured phrase during engagement");
    check(std::abs(held.l[450000]-held.l[18000])<1.e-7f,"Settled hold keeps attenuating the phrase");
}
void settledHoldInput() {
    auto s=clean();s.rate=0;
    Render held(48000,48000);
    held.engine.process(held.l.data(),held.r.data(),3000,s,{400,0,true});
    s.freeze=true;held.engine.process(held.l.data()+3000,held.r.data()+3000,12000,s,{400,3000./7200,true});
    held.l[15000]=1;
    held.engine.process(held.l.data()+15000,held.r.data()+15000,33000,s,{400,15000./7200,true});
    check(energy(held.l)==0,"Settled hold keeps admitting fresh input");
    s.freeze=false;std::fill(held.l.begin(),held.l.end(),0);
    held.engine.process(held.l.data(),held.r.data(),15000,s,{400,48000./7200,true});
    held.l[15000]=1;held.engine.process(held.l.data()+15000,held.r.data()+15000,33000,s,{400,63000./7200,true});
    check(held.l[15900]>.99f,"Released hold does not restore input");
}
int main() {
    int failed=0;
    const std::pair<const char*,void(*)()> cases[]={{"sustained settled hold",sustainedHold},{"settled hold input rejection",settledHoldInput},{"tap probability/ratchets",tapPerformance},{"tap tone",tapTone},{"groove/morph",grooveMorph},{"diffusion",diffusion},{"wobble rate",wobbleRate},{"wobble shape",wobbleShape},{"freeze",freezeLoop},{"fast timing automation",fastTimeAutomation},{"stutter seams",stutterEdges},{"filter automation",filterAutomation},{"extended host-synced grid",extendedGridTiming},{"beat timing",timing},{"routing",routing},{"taps",taps},{"dry/reset",dryAndReset},{"filters",filters},{"effects",effects},{"stability/block size",stable}};
    for(const auto& [name,test]:cases)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    return failed?1:0;
}
