#include "DelayEngine.h"
#include <algorithm>
#include <cmath>
#include <numbers>
namespace oed {
namespace {
constexpr double pi=std::numbers::pi;
float finite(float x) {return std::isfinite(x)?x:0.f;}
float clamp(float x,float lo,float hi) {return std::clamp(finite(x),lo,hi);}
double rate(int i) {return beatRates[static_cast<std::size_t>(std::clamp(i,0,9))];}
double fraction(double x) {return x-std::floor(x);}
}
void DelayEngine::Filter::clear() {stages={};oldStages={};primed=false;fade=0;}
void DelayEngine::Filter::coefficients() {
    const int count=slope<3?std::min(slope,1):(slope==3?2:4);
    const double w=2*pi*std::exp(logHz)/sr;
    if(slope==1) {
        const double k=std::tan(w*.5),a=1/(1+k);
        auto& s=stages[0];s.b0=isHigh?a:k*a;s.b1=isHigh?-a:k*a;s.b2=0;s.a1=(k-1)*a;s.a2=0;
    } else for(int i=0;i<count;++i) {
        const double q=1/(2*std::cos(pi*(2*i+1)/(4*count)));
        const double alpha=std::sin(w)/(2*q),c=std::cos(w),a=1/(1+alpha);
        auto& s=stages[static_cast<std::size_t>(i)];
        s.b0=(isHigh?(1+c):(1-c))*.5*a;s.b1=(isHigh?-(1+c):(1-c))*a;s.b2=s.b0;
        s.a1=-2*c*a;s.a2=(1-alpha)*a;
    }
}
void DelayEngine::Filter::configure(double sampleRate,float hz,int nextSlope,bool high) {
    sr=sampleRate;isHigh=high;nextSlope=std::clamp(nextSlope,0,4);
    smoothing=1-std::exp(-1/(sr*.03));fadeLength=std::max(1,static_cast<int>(sr*.02));
    targetLogHz=std::log(std::clamp(static_cast<double>(hz),10.,sr*.45));
    if(!primed){slope=nextSlope;logHz=targetLogHz;coefficients();primed=true;}
    else if(nextSlope!=slope&&fade==0){oldStages=stages;oldSlope=slope;stages={};slope=nextSlope;coefficients();fade=fadeLength;}
}
float DelayEngine::Filter::tick(float x) {
    if(std::abs(targetLogHz-logHz)>1.e-8){logHz+=(targetLogHz-logHz)*smoothing;coefficients();}
    const auto process=[](auto& chain,int order,double y) {
        const int count=order<3?std::min(order,1):(order==3?2:4);
        for(int i=0;i<count;++i) {
            auto& s=chain[static_cast<std::size_t>(i)];const double out=s.b0*y+s.z1;
            s.z1=s.b1*y-s.a1*out+s.z2;s.z2=s.b2*y-s.a2*out;y=out;
        }return y;
    };
    double y=process(stages,slope,x);
    if(fade>0){const double old=process(oldStages,oldSlope,x),blend=1-static_cast<double>(fade)/fadeLength;y=old+(y-old)*blend;--fade;}
    return static_cast<float>(y);
}
void DelayEngine::prepare(double sr) {
    sampleRate=std::isfinite(sr)&&sr>=8000?sr:48000;
    // 16 steps * 2 beats * 60 / 20 BPM, plus interpolation/modulation margin.
    for(auto& buffer:ring)buffer.assign(static_cast<std::size_t>(sampleRate*96.1)+4,0);
    for(auto& buffer:wetHistory)buffer.assign(static_cast<std::size_t>(sampleRate*12.1)+4,0);
    reset();
}
void DelayEngine::reset() {
    write=wetWrite=capture=valid=wetValid=0;phase=0;lastSlice=-1;initialized=false;
    envelope=0;lastDuck=1;tone={};held={};holdCounter=0;transition=0;
    for(auto& f:hp)f.clear();for(auto& f:lp)f.clear();
}
float DelayEngine::read(const std::vector<float>& buffer,double time) const {
    time=std::clamp(time,1.,static_cast<double>(buffer.size()-2));
    const auto whole=static_cast<std::size_t>(time);const float part=static_cast<float>(time-whole);
    const auto a=(write+buffer.size()-whole)%buffer.size(),b=(a+buffer.size()-1)%buffer.size();
    const float av=whole<=valid?buffer[a]:0,bv=whole+1<=valid?buffer[b]:0;
    return av+(bv-av)*part;
}
void DelayEngine::process(float* left,float* right,int frames,const Settings& s,const Transport& t) {
    if(ring[0].empty()||frames<=0)return;
    const double bpm=std::isfinite(t.bpm)?std::clamp(t.bpm,20.,400.):120.;
    const double beatSamples=sampleRate*60/bpm,unit=beatSamples*rate(s.rate);
    const double increment=1/beatSamples;
    if(t.hasPpq&&std::isfinite(t.ppq))phase=t.ppq;
    std::array<double,tapCount> nextDelay{};
    std::array<float,tapCount> targetGain{},targetPan{};
    int lastStep=1;float sum=0;
    for(std::size_t j=0;j<tapCount;++j) {
        const auto& tap=s.taps[j];const int step=std::clamp(tap.step,1,16);
        nextDelay[j]=unit*step;
        targetGain[j]=tap.enabled?clamp(tap.level,0,1)*std::exp(-4*clamp(s.decay,0,1)*(step-1)/15.f):0;
        targetPan[j]=clamp(tap.pan,-1,1);
        if(tap.enabled&&tap.level>0)lastStep=std::max(lastStep,step);
        sum+=tap.enabled?clamp(tap.level,0,1):0;
    }
    for(auto& value:targetGain)value/=std::max(1.f,sum);
    const double loop=unit*lastStep;
    const float targetFeedback=clamp(s.feedback,0,.97f),targetMix=clamp(s.mix,0,1),targetWidth=clamp(s.width,0,1);
    const float targetOutput=std::pow(10.f,clamp(s.outputDb,-24,12)/20),targetDuck=clamp(s.duck,0,1);
    const float targetGlitch=s.glitch==Glitch::off?0:clamp(s.glitchAmount,0,1);
    const float targetDrive=clamp(s.drive,0,1),targetMotion=clamp(s.motion,0,1);
    const int fadeLength=std::max(1,static_cast<int>(sampleRate*.025));
    if(!initialized) {
        delay=oldDelay=tapeDelay=nextDelay;previousLoop=oldLoop=tapeLoop=loop;gain=targetGain;pan=targetPan;
        feedback=targetFeedback;mix=targetMix;width=targetWidth;output=targetOutput;duck=targetDuck;
        glitchMix=targetGlitch;drive=targetDrive;motion=targetMotion;initialized=true;
    } else if(transition==0 && (nextDelay!=delay||std::abs(loop-previousLoop)>.00001)) {
        oldDelay=delay;oldLoop=previousLoop;delay=nextDelay;previousLoop=loop;transition=fadeLength;
    }
    for(int ch=0;ch<2;++ch) {hp[static_cast<std::size_t>(ch)].configure(sampleRate,s.highPass,s.hpSlope,true);lp[static_cast<std::size_t>(ch)].configure(sampleRate,s.lowPass,s.lpSlope,false);}
    const float smooth=static_cast<float>(1-std::exp(-1/(sampleRate*.01)));
    const double tapeSmooth=1-std::exp(-1/(sampleRate*.12));
    const float toneCoefficient=static_cast<float>(1-std::exp(-2*pi*(s.character==Character::bbd?2600:6500)/sampleRate));
    const float attack=static_cast<float>(std::exp(-1/(sampleRate*.005)));
    const float release=static_cast<float>(std::exp(-1/(beatSamples*rate(s.duckRate))));
    for(int i=0;i<frames;++i) {
        const float dryL=finite(left[i]),dryR=right?finite(right[i]):dryL;
        const auto approach=[smooth](float& value,float target){value+=(target-value)*smooth;};
        approach(feedback,targetFeedback);approach(mix,targetMix);approach(width,targetWidth);approach(output,targetOutput);
        approach(duck,targetDuck);approach(glitchMix,targetGlitch);approach(drive,targetDrive);approach(motion,targetMotion);
        const float blend=transition>0?1-static_cast<float>(transition)/fadeLength:1;
        const bool tape=s.character==Character::tape;
        tapeLoop+=(loop-tapeLoop)*tapeSmooth;
        const double wobble=tape?motion*sampleRate*.0015*(std::sin(phase*pi*.5)+.18*std::sin(phase*pi*14)):0;
        const auto delayed=[&](int ch,double newer,double older,double gliding) {
            const auto& buffer=ring[static_cast<std::size_t>(ch)];
            if(tape)return read(buffer,gliding+wobble);
            const float a=read(buffer,newer);return transition>0?read(buffer,older)*(1-blend)+a*blend:a;
        };
        float fbL=delayed(0,previousLoop,oldLoop,tapeLoop),fbR=delayed(1,previousLoop,oldLoop,tapeLoop);
        if(s.routing==Routing::pingPong)std::swap(fbL,fbR);
        float wetL=0,wetR=0;
        for(std::size_t j=0;j<tapCount;++j) {
            approach(gain[j],targetGain[j]);approach(pan[j],targetPan[j]);
            tapeDelay[j]+=(nextDelay[j]-tapeDelay[j])*tapeSmooth;
            if(gain[j]<1.e-7f)continue;
            float l=delayed(0,delay[j],oldDelay[j],tapeDelay[j]),r=delayed(1,delay[j],oldDelay[j],tapeDelay[j]);
            if(s.routing==Routing::pingPong) {if(j%2)std::swap(l,r);}
            else if(s.routing==Routing::stereo){l*=1-std::max(0.f,pan[j]);r*=1+std::min(0.f,pan[j]);}
            wetL+=l*gain[j];wetR+=r*gain[j];
        }
        const float mono=(dryL+dryR)*.5f;
        std::array<float,2> feed{s.routing==Routing::stereo?dryL:mono,s.routing==Routing::stereo?dryR:(s.routing==Routing::mono?mono:0)};
        feed[0]+=fbL*feedback;feed[1]+=fbR*feedback;
        const bool updateHold=holdCounter==0;
        holdCounter=(holdCounter+1)%std::max(1,static_cast<int>(sampleRate/12000));
        for(std::size_t ch=0;ch<2;++ch) {
            float v=feed[ch];
            if(s.character!=Character::digital) {
                if(s.character==Character::loFi) {
                    if(updateHold)held[ch]=std::round(v*256.f)/256.f;
                    v=held[ch];
                } else v=std::tanh(v*(1+drive*4))/(1+drive*1.5f);
                tone[ch]+=toneCoefficient*(v-tone[ch]);v=tone[ch];
            }
            v=lp[ch].tick(hp[ch].tick(v));
            // Bound the feedback storage without clipping the dry branch.
            ring[ch][write]=clamp(v,-4,4);
        }
        write=(write+1)%ring[0].size();valid=std::min(valid+1,ring[0].size());
        wetHistory[0][wetWrite]=wetL;wetHistory[1][wetWrite]=wetR;
        const double grid=phase/rate(s.glitchRate),slice=std::floor(grid+1.e-8),position=fraction(grid+1.e-8);
        if(slice!=lastSlice) {
            sliceSize=std::clamp(static_cast<int>(std::round(beatSamples*rate(s.glitchRate))),2,static_cast<int>(wetHistory[0].size()/2));
            capture=(wetWrite+wetHistory[0].size()-static_cast<std::size_t>(sliceSize))%wetHistory[0].size();lastSlice=slice;
        }
        std::array<float,2> wet{wetL,wetR};
        for(std::size_t ch=0;ch<2;++ch) {
            float effected=wet[ch];
            if(s.glitch==Glitch::gate)effected*=static_cast<float>(.5-.5*std::cos(2*pi*position));
            else if(s.glitch==Glitch::stutter||s.glitch==Glitch::reverse) {
                const double p=s.glitch==Glitch::reverse?1-position:fraction(position*2)*.5;
                const auto offset=static_cast<std::size_t>(std::clamp(static_cast<int>(p*(sliceSize-1)),0,sliceSize-1));
                const double repeatPhase=s.glitch==Glitch::stutter?fraction(position*2):position;
                const double repeatLength=s.glitch==Glitch::stutter?sliceSize*.5:sliceSize;
                const double edge=std::min(repeatPhase,1-repeatPhase)*repeatLength/(sampleRate*.002);
                effected=wetValid>=static_cast<std::size_t>(sliceSize*2)?wetHistory[ch][(capture+offset)%wetHistory[ch].size()]*static_cast<float>(std::clamp(edge,0.,1.)):0;
            }
            wet[ch]+=(effected-wet[ch])*glitchMix;
        }
        wetWrite=(wetWrite+1)%wetHistory[0].size();wetValid=std::min(wetValid+1,wetHistory[0].size());
        const float peak=std::max(std::abs(dryL),std::abs(dryR)),coeff=peak>envelope?attack:release;
        envelope=peak+(envelope-peak)*coeff;
        const double duckPhase=fraction(phase/rate(s.duckRate)+1.e-9);
        lastDuck=s.inputDuck?std::exp(-duck*envelope*24):1-duck*static_cast<float>(std::pow(1-duckPhase,3));
        const float mid=(wet[0]+wet[1])*.5f,side=(wet[0]-wet[1])*.5f*width;
        wetL=(mid+side)*lastDuck;wetR=(mid-side)*lastDuck;
        if(s.routing==Routing::mono)wetL=wetR=mid*lastDuck;
        if(right){left[i]=(dryL*(1-mix)+wetL*mix)*output;right[i]=(dryR*(1-mix)+wetR*mix)*output;}
        else left[i]=(dryL*(1-mix)+(wetL+wetR)*.5f*mix)*output;
        phase+=increment;if(transition>0)--transition;
    }
}
}
