#include "DelayEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
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
        for(int j=std::max(1,at-2);j<=at+2;++j){sum+=x.l[static_cast<std::size_t>(j)];centre+=j*x.l[static_cast<std::size_t>(j)];}
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
double energy(const std::vector<float>& x,int begin=0,int end=-1) {
    const std::size_t stop=end<0?x.size():std::min(x.size(),static_cast<std::size_t>(end));
    double e=0;for(std::size_t i=static_cast<std::size_t>(begin);i<stop;++i)e+=x[i]*x[i];return e;
}
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
void shimmerHaasDuckHold() {
    auto s=clean();s.rate=0;s.feedback=.6f;
    Render plain(8000,12000),shimmered(8000,12000);
    for(std::size_t i=0;i<400;++i)plain.l[i]=shimmered.l[i]=std::sin(static_cast<float>(i)*.15f)*.5f;
    plain.run(s);
    s.shimmer=.8f;shimmered.run(s);
    check(plain.l!=shimmered.l&&energy(shimmered.l,1500)>0,"Shimmer pitch shift does not alter feedback loop");
    s=clean();s.rate=0;s.stereoOffset=10.f;
    Render haas(8000,4000);haas.l[0]=haas.r[0]=1;haas.run(s);
    check(haas.l[500]>.9f&&haas.r[580]>.9f,"Stereo Haas offset does not delay right wet channel by 10 ms");
    s=clean();s.rate=7;s.duck=1.f;
    Render noHold(8000,8000),withHold(8000,8000);
    std::fill(noHold.l.begin(),noHold.l.end(),.5f);std::fill(withHold.l.begin(),withHold.l.end(),.5f);
    noHold.run(s);
    s.duckHold=.8f;withHold.run(s);
    check(energy(withHold.l,4000)<energy(noHold.l,4000),"Duck hold does not extend ducking attenuation");
}
void reverseTiming() {
    for(double sr:{8000.,44100.}) for(double bpm:{100.,120.,126.,137.,174.}) {
        const double beat=sr*60.0/bpm, chunk=beat*beatRates[7], delay=chunk;
        const int frames=static_cast<int>(std::ceil(delay+chunk))+64;
        Render x(sr,frames); auto s=clean(); s.reverse=1.f; s.reverseRate=7; s.rate=7; s.feedback=0.f;
        x.l[0]=1.f; x.r[0]=1.f; x.run(s,bpm,128);
        int peak=1;
        for(int i=1;i<frames;++i) if(std::abs(x.l[static_cast<std::size_t>(i)])>std::abs(x.l[static_cast<std::size_t>(peak)])) peak=i;
        check(std::abs(peak-(delay+chunk*.5))<=1.0 && std::abs(x.l[static_cast<std::size_t>(peak)])>.45f,"Reverse echo misses delay + chunk/2");
        const int forward=std::clamp(static_cast<int>(std::lround(delay)),1,frames-1);
        check(std::abs(x.l[static_cast<std::size_t>(forward)])<.25f,"Reverse still plays a forward echo at the tap time");
    }
    constexpr int T=6000;
    Render ramp(8000,12000); auto s=clean(); s.reverse=1.f; s.reverseRate=7; s.rate=7;
    for(int i=0;i<4000;++i) ramp.l[static_cast<std::size_t>(i)]=ramp.r[static_cast<std::size_t>(i)]=i/4000.f;
    ramp.run(s,120,64);
    check(ramp.l[T-400]>ramp.l[T-100] && ramp.l[T-100]>ramp.l[T]+.01f,"Reverse chunk is not time-reversed");
}
void reverseClicks() {
    constexpr int sr=8000;
    Render x(sr,sr*2); auto s=clean(); s.reverse=1; s.reverseRate=7; s.rate=7; s.feedback=0;
    for(std::size_t i=0;i<x.l.size();++i){const float v=std::sin(static_cast<float>(i)*(2*3.14159265f*440.f/sr))*.25f; x.l[i]=x.r[i]=v;}
    x.run(s,120,128);
    float maxDelta=0;
    for(std::size_t i=sr/10;i<x.l.size();++i) maxDelta=std::max(maxDelta,std::abs(x.l[i]-x.l[i-1]));
    check(maxDelta<.2f,"Reverse chunk boundary clicks");
}
void reverseFeedbackPingPong() {
    auto s=clean(); s.reverse=1; s.reverseRate=7; s.rate=7; s.feedback=.5f; s.routing=Routing::pingPong;
    Render x(8000,32000); x.l[0]=1; x.r[0]=1; x.run(s,120,96);
    for(float v:x.l) check(std::isfinite(v)&&std::abs(v)<4.f,"Reverse feedback is unstable");
    for(float v:x.r) check(std::isfinite(v)&&std::abs(v)<4.f,"Reverse feedback is unstable");
    double left=0,right=0,laterL=0,laterR=0;
    for(int i=5000;i<7000;++i){left+=std::abs(x.l[static_cast<std::size_t>(i)]); right+=std::abs(x.r[static_cast<std::size_t>(i)]);}
    for(int i=11000;i<14000;++i){laterL+=std::abs(x.l[static_cast<std::size_t>(i)]); laterR+=std::abs(x.r[static_cast<std::size_t>(i)]);}
    check(left>right*4,"First reversed ping-pong image is not on the left");
    check(laterR>laterL,"Reversed ping-pong feedback does not reach the right");
}
void reverseBlockSize() {
    auto s=clean(); s.reverse=1; s.reverseRate=7; s.rate=7; s.feedback=.3f;
    Render a(8000,12000),b(8000,12000);
    for(std::size_t i=0;i<a.l.size();++i) a.l[i]=b.l[i]=std::sin(static_cast<float>(i)*.03f)*.2f;
    a.run(s,120,1); b.run(s,120,300);
    for(std::size_t i=0;i<a.l.size();++i) check(std::abs(a.l[i]-b.l[i])<.0001f,"Reverse delay depends on block size");
}
void modulationEdges() {
    Render x(8000,16000); auto s=clean(); s.character=Character::tape; s.motion=1; s.drive=0; s.wobbleShape=WobbleShape::square; s.wobbleRate=4;
    for(std::size_t i=0;i<x.l.size();++i) x.l[i]=x.r[i]=std::sin(static_cast<float>(i)*.05f)*.3f;
    x.run(s);
    float maxDelta=0;
    for(std::size_t i=1;i<x.l.size();++i) maxDelta=std::max(maxDelta,std::abs(x.l[i]-x.l[i-1]));
    check(maxDelta<.2f,"Square tape modulation clicks");
}
void sidechainDuck() {
    auto s=clean();s.duck=1.f;s.duckSource=DuckSource::sidechain;s.inputDuck=false;
    constexpr int n=8000;
    std::vector<float> l(n),r(n),sc(n);
    auto run=[&](DelayEngine& engine,int block,const float* side) {
        for(int i=0;i<n;i+=block)
            engine.process(l.data()+i,r.data()+i,std::min(block,n-i),s,{120,i/8000.0*2.0,true},side==nullptr?nullptr:side+i,side==nullptr?nullptr:side+i);
    };
    DelayEngine open;open.prepare(8000);l[0]=1;run(open,200,nullptr);
    check(std::abs(l[4000]-1.f)<.05f,"Sidechain mode ducks when no sidechain is connected");
    std::fill(l.begin(),l.end(),0.f);std::fill(r.begin(),r.end(),0.f);l[0]=1.f;
    std::fill(sc.begin(),sc.end(),0.f);std::fill(sc.begin()+3000,sc.begin()+5200,.85f);
    DelayEngine ducked;ducked.prepare(8000);run(ducked,200,sc.data());
    check(std::abs(l[4000])<.2f,"A sidechain hit does not duck the echo");
    std::fill(l.begin(),l.end(),0.f);l[0]=1.f;std::fill(sc.begin(),sc.end(),0.f);
    DelayEngine silent;silent.prepare(8000);run(silent,128,sc.data());
    check(std::abs(l[4000]-1.f)<.05f,"A silent sidechain still ducks");
    std::fill(l.begin(),l.end(),0.f);std::fill(r.begin(),r.end(),0.f);std::fill(sc.begin(),sc.end(),.7f);
    auto leak=s;leak.duck=0.f;DelayEngine dry;dry.prepare(8000);
    dry.process(l.data(),r.data(),n,leak,{120,0,true},sc.data(),sc.data());
    check(energy(l)<1.e-4&&energy(r)<1.e-4,"Sidechain audio is mixed into the output");
    std::fill(l.begin(),l.end(),0.f);std::fill(r.begin(),r.end(),0.f);l[0]=1.f;
    std::fill(sc.begin(),sc.end(),0.f);std::fill(sc.begin()+3000,sc.begin()+5200,.85f);
    DelayEngine blocks;blocks.prepare(8000);auto other=l;std::vector<float> otherR(n),otherSc=sc;
    run(blocks,1,sc.data());
    DelayEngine wide;wide.prepare(8000);
    wide.process(other.data(),otherR.data(),n,s,{120,0,true},otherSc.data(),otherSc.data());
    for(int i=0;i<n;++i) check(std::abs(l[static_cast<std::size_t>(i)]-other[static_cast<std::size_t>(i)])<.0001f,"Sidechain ducking depends on block size");
    DelayEngine fast;fast.prepare(8000);std::fill(l.begin(),l.end(),0.f);std::fill(sc.begin(),sc.end(),0.f);std::fill(sc.begin(),sc.begin()+80,1.f);
    fast.process(l.data(),r.data(),80,s,{120,0,true},sc.data(),nullptr);
    check(fast.duckGain()<.3f,"Sidechain detector attack is too slow for a kick");
    std::fill(sc.begin(),sc.end(),0.f);fast.process(l.data(),r.data(),n,s,{120,0,true},sc.data(),nullptr);
    check(fast.duckGain()>.5f,"Sidechain detector does not release");
    auto ignored=clean();ignored.duck=1.f;ignored.inputDuck=true;ignored.duckSource=DuckSource::beat;
    std::fill(l.begin(),l.end(),0.f);l[0]=1.f;std::fill(sc.begin(),sc.end(),.9f);
    DelayEngine withSide;withSide.prepare(8000);withSide.process(l.data(),r.data(),n,ignored,{120,0,true},sc.data(),sc.data());
    auto heard=l;std::fill(l.begin(),l.end(),0.f);l[0]=1.f;
    DelayEngine without;without.prepare(8000);without.process(l.data(),r.data(),n,ignored,{120,0,true});
    for(int i=0;i<n;++i) check(std::abs(heard[static_cast<std::size_t>(i)]-l[static_cast<std::size_t>(i)])<.0001f,"Input duck follows the sidechain");
}
double bandPower(const std::vector<float>& x,int begin,int end,double hz,double sr) {
    double re=0,im=0;
    const double w=2*3.14159265358979323846*hz/sr;
    for(int i=begin;i<end;++i) {
        const double v=x[static_cast<std::size_t>(i)];
        re+=v*std::cos(w*i);
        im-=v*std::sin(w*i);
    }
    const double n=std::max(1,end-begin);
    return (re*re+im*im)/(n*n);
}
void fillStereoSine(Render& x,double hz,float amp,int count=-1) {
    const int stop=count<0?static_cast<int>(x.l.size()):std::min(count,static_cast<int>(x.l.size()));
    const double w=2*3.14159265358979323846*hz/x.sr;
    for(int i=0;i<stop;++i) {
        const float v=static_cast<float>(std::sin(w*i)*amp);
        x.l[static_cast<std::size_t>(i)]=x.r[static_cast<std::size_t>(i)]=v;
    }
}
void shimmerOctaveShift() {
    constexpr double sr=48000;
    Render x(sr,static_cast<int>(sr*1.6));
    auto s=clean();s.rate=7;s.feedback=.75f;s.shimmer=1.f;
    fillStereoSine(x,440.0,.4f,static_cast<int>(sr*.45));
    x.run(s,120,256);
    const int a=static_cast<int>(sr*1.05),b=static_cast<int>(sr*1.4);
    const double e440=bandPower(x.l,a,b,440,sr),e880=bandPower(x.l,a,b,880,sr);
    check(e880>e440*.08,"Shimmer does not pitch-shift feedback up an octave (+12 semitones)");
    Render hi(sr,static_cast<int>(sr*1.6));
    fillStereoSine(hi,6500.0,.4f,static_cast<int>(sr*.45));
    hi.run(s,120,256);
    const double e13k=bandPower(hi.l,a,b,13000,sr);
    check(e13k<e880*.35,"Shimmer lacks 9 kHz anti-alias LPF before feedback injection");
}
void hermiteAllModes() {
    constexpr double sr=48000,bpm=137,hz=12000;
    Render x(sr,24000);
    auto s=clean();s.rate=2;s.character=Character::digital;
    fillStereoSine(x,hz,.5f);
    x.run(s,bpm,256);
    const double tapPower=bandPower(x.l,12000,20000,hz,sr);
    check(tapPower>.042,"Fractional digital tap uses linear interpolation instead of Hermite");
    Render haas(sr,24000);
    s.rate=2;s.stereoOffset=1.31f;
    fillStereoSine(haas,hz,.5f);
    haas.run(s,120,256);
    const double haasPower=bandPower(haas.r,12000,20000,hz,sr);
    check(haasPower>.042,"Fractional stereo Haas offset uses linear interpolation instead of Hermite");
}
void tapeHysteresisAndHeadBump() {
    constexpr double sr=48000;
    auto measureHz=[&](double hz) {
        Render x(sr,36000);
        auto s=clean();s.rate=2;s.character=Character::tape;s.drive=.35f;s.motion=0.f;
        fillStereoSine(x,hz,.2f);
        x.run(s,120,256);
        return bandPower(x.l,16000,32000,hz,sr);
    };
    const double p30=measureHz(30),p95=measureHz(95),p320=measureHz(320);
    check(p95>p30*1.12&&p95>p320*1.05,"Tape mode lacks 80-110 Hz 2-pole head-bump resonance");
    Render flutter(sr,48000);
    auto s=clean();s.rate=2;s.character=Character::tape;s.drive=0.f;s.motion=.8f;s.wobbleRate=1.f;s.wobbleShape=WobbleShape::sine;
    fillStereoSine(flutter,1000.0,.3f);
    flutter.run(s,120,256);
    double diff=0;
    for(int i=12000;i<24000;++i) diff+=std::abs(flutter.l[static_cast<std::size_t>(i)]-flutter.l[static_cast<std::size_t>(i+24000)]);
    check(diff/12000.0>1.e-3,"Tape flutter is purely periodic instead of stochastic Ornstein-Uhlenbeck");
}
void bbdCompanderAndClockFilter() {
    constexpr double sr=48000;
    auto measureRate=[&](int gridRate) {
        Render x(sr,72000);
        auto s=clean();s.rate=gridRate;s.character=Character::bbd;s.drive=.25f;s.motion=0.f;
        fillStereoSine(x,3600.0,.25f);
        x.run(s,120,256);
        return bandPower(x.l,52000,70000,3600,sr);
    };
    const double shortDelay=measureRate(0),longDelay=measureRate(9);
    check(shortDelay>longDelay*4.0,"BBD anti-alias/reconstruction LPF cutoff does not scale inversely with delay time");
}
void loFiAntiAliasAndDither() {
    constexpr double sr=48000;
    Render x(sr,36000);
    auto s=clean();s.rate=2;s.character=Character::loFi;
    fillStereoSine(x,15000.0,.4f);
    x.run(s,120,256);
    const double folded3k=bandPower(x.l,14000,32000,3000,sr);
    check(folded3k<1.e-4,"Lo-Fi decimator folds 15 kHz down to 3 kHz without anti-alias filtering");
}
void hadamardAllpassDiffuser() {
    auto s=clean();s.rate=0;s.diffusion=1.f;s.diffusionSize=18.f;
    Render x(16000,16000);
    x.l[0]=x.r[0]=1.f;
    x.run(s,120,128);
    const double eL=energy(x.l),eR=energy(x.r);
    check(std::abs(eL-1.0)<.05&&std::abs(eR-1.0)<.05,"Diffuser is not unity-gain energy preserving");
    check(x.l!=x.r,"Diffuser uses identical delay lengths across stereo channels");
}
void oversampledZdfFeedback() {
    constexpr double sr=48000;
    Render x(sr,36000);
    auto s=clean();s.rate=2;s.character=Character::digital;
    fillStereoSine(x,13000.0,1.8f);
    x.run(s,120,256);
    const double fundamental=bandPower(x.l,14000,32000,13000,sr);
    const double aliased9k=bandPower(x.l,14000,32000,9000,sr);
    check(fundamental>.1&&aliased9k<fundamental*1.e-4,"Feedback shaper folds 3rd harmonic of 13 kHz back to 9 kHz (not 2x oversampled)");
}
void wsolaPreFeedbackReverse() {
    constexpr double sr=8000;
    Render shortTap(sr,16000);
    auto s=clean();s.rate=0;s.reverse=1.f;s.reverseRate=7;s.feedback=0.f;
    shortTap.l[400]=shortTap.r[400]=1.f;
    shortTap.run(s,120,128);
    check(energy(shortTap.l,401,850)<1.e-5,"Short-delay reverse crosses write boundary and leaks before tap delay");
    Render tonal(sr,24000);
    s.rate=7;s.feedback=.6f;
    fillStereoSine(tonal,337.0,.3f);
    tonal.run(s,120,128);
    float maxJump=0.f;
    for(std::size_t i=6000;i<tonal.l.size();++i) maxJump=std::max(maxJump,std::abs(tonal.l[i]-tonal.l[i-1]));
    check(maxJump<.12f,"WSOLA reverse splices click on non-harmonic tones with feedback");
}
void rmsLookaheadDbDucking() {
    constexpr double sr=16000;
    constexpr int n=32000;
    DelayEngine engine;engine.prepare(sr);
    auto s=clean();s.rate=2;s.duck=.9f;s.duckRate=0;s.duckSource=DuckSource::sidechain;
    std::vector<float> l(n),r(n),sc(n);
    for(int i=0;i<n;++i) {
        l[static_cast<std::size_t>(i)]=r[static_cast<std::size_t>(i)]=.4f;
        if(i>=12000) sc[static_cast<std::size_t>(i)]=static_cast<float>(std::sin(2*3.14159265358979323846*45.0*i/sr)*.85);
    }
    sc[12000]=1.f;
    engine.process(l.data(),r.data(),n,s,{180,0,true},sc.data(),sc.data());
    const float edgeStep=std::abs(l[12000]-l[11999]);
    check(edgeStep<.025f,"Ducking clips initial transient duck edge with single-sample gain step");
    const double ripple90=bandPower(l,16000,30000,90,sr);
    check(ripple90<1.5e-5,"Ducking distorts wet tail with rectified 90 Hz bass ripple");
}
void equalPowerTransitionAndBbdVarispeed() {
    constexpr double sr=16000;
    Render x(sr,32000);
    auto s=clean();s.rate=7;s.character=Character::digital;
    std::uint32_t rng=1234525u;
    for(std::size_t i=0;i<x.l.size();++i) {
        rng=rng*1664525u+1013904223u;
        const float v=(static_cast<float>(rng>>8)/8388608.f-1.f)*.3f;
        x.l[i]=x.r[i]=v;
    }
    x.engine.process(x.l.data(),x.r.data(),20000,s,{120,0,true});
    const double steady=energy(x.l,19600,20000)/400.0;
    s.taps[0].step=2;
    x.engine.process(x.l.data()+20000,x.r.data()+20000,400,s,{120,2.5,true});
    const double mid=energy(x.l,20120,20280)/160.0;
    check(mid>steady*.75,"Rate transition has a -3 dB linear crossfade dip instead of equal-power");
    Render bbd(sr,32000);
    s.taps[0].step=1;s.character=Character::bbd;s.drive=0.f;
    fillStereoSine(bbd,440.0,.3f);
    bbd.engine.process(bbd.l.data(),bbd.r.data(),20000,s,{120,0,true});
    s.taps[0].step=2;
    bbd.engine.process(bbd.l.data()+20000,bbd.r.data()+20000,4000,s,{120,2.5,true});
    const double shifted=bandPower(bbd.l,20600,22200,440,sr),total=energy(bbd.l,20600,22200)/1600.0;
    check(total>1.e-4&&shifted<total*.35,"BBD mode does not use continuous varispeed Doppler slewing on rate changes");
}
int main() {
    int failed=0;
    const std::pair<const char*,void(*)()> cases[]={{"shimmer octave shift",shimmerOctaveShift},{"Hermite interpolation all modes",hermiteAllModes},{"tape hysteresis and head bump",tapeHysteresisAndHeadBump},{"BBD compander and clock filter",bbdCompanderAndClockFilter},{"Lo-Fi anti-alias and dither",loFiAntiAliasAndDither},{"Hadamard allpass diffuser",hadamardAllpassDiffuser},{"2x oversampled ZDF feedback",oversampledZdfFeedback},{"WSOLA pre-feedback reverse",wsolaPreFeedbackReverse},{"RMS lookahead dB ducking",rmsLookaheadDbDucking},{"equal-power transition and BBD varispeed",equalPowerTransitionAndBbdVarispeed},{"sidechain duck",sidechainDuck},{"reverse timing",reverseTiming},{"reverse clicks",reverseClicks},{"reverse feedback ping-pong",reverseFeedbackPingPong},{"reverse block size",reverseBlockSize},{"modulation edges",modulationEdges},{"shimmer, stereo Haas, and duck hold",shimmerHaasDuckHold},{"sustained settled hold",sustainedHold},{"settled hold input rejection",settledHoldInput},{"tap probability/ratchets",tapPerformance},{"tap tone",tapTone},{"groove/morph",grooveMorph},{"diffusion",diffusion},{"wobble rate",wobbleRate},{"wobble shape",wobbleShape},{"freeze",freezeLoop},{"fast timing automation",fastTimeAutomation},{"stutter seams",stutterEdges},{"filter automation",filterAutomation},{"extended host-synced grid",extendedGridTiming},{"beat timing",timing},{"routing",routing},{"taps",taps},{"dry/reset",dryAndReset},{"filters",filters},{"effects",effects},{"stability/block size",stable}};
    for(const auto& [name,test]:cases)try{test();std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
    return failed?1:0;
}
