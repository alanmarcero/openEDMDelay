#include "DelayEngine.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>
namespace oed {
namespace {
constexpr double pi=std::numbers::pi;
float finite(float x) {return std::isfinite(x)?x:0.f;}
float clamp(float x,float lo,float hi) {return std::clamp(finite(x),lo,hi);}
double rate(int i) {return beatRates[static_cast<std::size_t>(std::clamp(i,0,static_cast<int>(beatRates.size())-1))];}
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
    // Existing history limit; read() bounds timing offsets to this storage.
    for(auto& buffer:ring)buffer.assign(static_cast<std::size_t>(sampleRate*96.1)+4,0);
    for(auto& buffer:wetHistory)buffer.assign(static_cast<std::size_t>(sampleRate*12.1)+4,0);
    reset();
}
void DelayEngine::reset() {
    write=wetWrite=capture=valid=wetValid=0;phase=0;lastSlice=-1;initialized=false;
    envelope=0;lastDuck=1;tone={};held={};holdCounter=0;transition=0;
    for(auto& f:hp)f.clear();for(auto& f:lp)f.clear();
    for(auto& pair:tapHp)for(auto& f:pair)f.clear();
    for(auto& pair:tapLp)for(auto& f:pair)f.clear();
    tapTone={};freezeMix=0;wasFrozen=false;frozenLoop=1;
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
    std::array<float,tapCount> targetGain{},targetPan{},tapToneCoefficient{};
    double lastStep=1;float sum=0;
    for(std::size_t j=0;j<tapCount;++j) {
        const auto& tap=s.taps[j];
        tapToneCoefficient[j]=static_cast<float>(1-std::exp(-2*pi*(tap.character==2?2600:6500)/sampleRate));
        const int step=std::clamp(tap.step,1,16);
        const double morphed=step+(std::clamp(tap.morphStep,1,16)-step)*clamp(s.morph,0,1);
        const auto grooveOffset=[&](double position) {
            const int index=static_cast<int>(std::round(position))-1;
            if(s.groove==2) {constexpr double offsets[]{0,.12,-.08,.18};return offsets[index%4];}
            const double amount=s.groove==1?.25:clamp(s.swing,0,1)*.5;
            return index%2?amount:0.;
        };
        nextDelay[j]=unit*(morphed+grooveOffset(morphed));
        for(std::size_t ch=0;ch<2;++ch) {
            tapHp[j][ch].configure(sampleRate,tap.highPass,tap.filter?2:0,true);
            tapLp[j][ch].configure(sampleRate,tap.lowPass,tap.filter?2:0,false);
        }
        targetGain[j]=tap.enabled?clamp(tap.level,0,1)*std::exp(-4*clamp(s.decay,0,1)*(step-1)/15.f):0;
        targetPan[j]=clamp(tap.pan,-1,1);
        if(tap.enabled&&tap.level>0)lastStep=std::max(lastStep,nextDelay[j]/unit);
        sum+=tap.enabled?clamp(tap.level,0,1):0;
    }
    for(auto& value:targetGain)value/=std::max(1.f,sum);
    const double loop=unit*lastStep;
    if(s.freeze&&!wasFrozen)frozenLoop=std::round(loop);
    wasFrozen=s.freeze;
    const float targetFeedback=clamp(s.feedback,0,.97f),targetMix=clamp(s.mix,0,1),targetWidth=clamp(s.width,0,1);
    const float targetOutput=std::pow(10.f,clamp(s.outputDb,-24,12)/20),targetDuck=clamp(s.duck,0,1);
    const float targetGlitch=s.glitch==Glitch::off?0:clamp(s.glitchAmount,0,1);
    const float targetDrive=clamp(s.drive,0,1),targetMotion=clamp(s.motion,0,1);
    const int fadeLength=std::max(1,static_cast<int>(sampleRate*.025));
    if(!initialized) {
        delay=oldDelay=tapeDelay=nextDelay;previousLoop=oldLoop=tapeLoop=loop;gain=targetGain;pan=targetPan;
        feedback=targetFeedback;mix=targetMix;width=targetWidth;output=targetOutput;duck=targetDuck;
        glitchMix=targetGlitch;drive=targetDrive;motion=targetMotion;freezeMix=s.freeze?1.f:0.f;initialized=true;
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
        approach(freezeMix,s.freeze?1.f:0.f);
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
        fbL+=(read(ring[0],frozenLoop)-fbL)*freezeMix;fbR+=(read(ring[1],frozenLoop)-fbR)*freezeMix;
        if(s.routing==Routing::pingPong)std::swap(fbL,fbR);
        float wetL=0,wetR=0;
        for(std::size_t j=0;j<tapCount;++j) {
            approach(gain[j],targetGain[j]);approach(pan[j],targetPan[j]);
            tapeDelay[j]+=(nextDelay[j]-tapeDelay[j])*tapeSmooth;
            if(gain[j]<1.e-7f)continue;
            const auto& tap=s.taps[j];
            const int repeats=std::clamp(tap.ratchets,1,8);
            float l=0,r=0;
            for(int repeat=0;repeat<repeats;++repeat) {
                const double offset=unit*repeat/repeats;
                const float chance=clamp(tap.probability,0,1);
                if(chance<1) {
                    // Hash the source phrase so decisions survive block changes and host seeks.
                    const double phrase=std::floor((phase-(delay[j]+offset)/beatSamples)/ (loop/beatSamples)+1.e-7);
                    std::uint32_t hash=static_cast<std::uint32_t>(std::fmod(std::abs(phrase),4294967295.));
                    hash^=static_cast<std::uint32_t>(j+1)*0x9e3779b9u;hash^=hash>>16;hash*=0x7feb352du;hash^=hash>>15;
                    if(static_cast<double>(hash)/4294967296.>=chance)continue;
                }
                l+=delayed(0,delay[j]+offset,oldDelay[j]+offset,tapeDelay[j]+offset)/repeats;
                r+=delayed(1,delay[j]+offset,oldDelay[j]+offset,tapeDelay[j]+offset)/repeats;
            }
            std::array<float,2> shaped{l,r};
            for(std::size_t ch=0;ch<2;++ch) {
                float v=shaped[ch];
                if(tap.character==3)v=std::round(v*64.f)/64.f;
                else if(tap.character==1||tap.character==2) {
                    const float amount=clamp(tap.drive,0,1);
                    v=std::tanh(v*(1+4*amount))/(1+1.5f*amount);
                    tapTone[j][ch]+=tapToneCoefficient[j]*(v-tapTone[j][ch]);v=tapTone[j][ch];
                }
                shaped[ch]=tapLp[j][ch].tick(tapHp[j][ch].tick(v));
            }
            l=shaped[0];r=shaped[1];
            if(s.routing==Routing::pingPong) {if(j%2)std::swap(l,r);}
            else if(s.routing==Routing::stereo){l*=1-std::max(0.f,pan[j]);r*=1+std::min(0.f,pan[j]);}
            wetL+=l*gain[j];wetR+=r*gain[j];
        }
        const float mono=(dryL+dryR)*.5f;
        std::array<float,2> feed{s.routing==Routing::stereo?dryL:mono,s.routing==Routing::stereo?dryR:(s.routing==Routing::mono?mono:0)};
        feed[0]*=1-freezeMix;feed[1]*=1-freezeMix;
        feed[0]+=fbL*(feedback+(1-feedback)*freezeMix);feed[1]+=fbR*(feedback+(1-feedback)*freezeMix);
        const bool updateHold=holdCounter==0;
        holdCounter=(holdCounter+1)%std::max(1,static_cast<int>(sampleRate/12000));
        for(std::size_t ch=0;ch<2;++ch) {
            float v=feed[ch];
            if(s.character!=Character::digital) {
                if(s.character==Character::loFi) {
                    if(updateHold)held[ch]=std::round(v*256.f)/256.f;
                    v=held[ch];
                } else {
                    const float satDrive = 1.0f + drive * 4.0f;
                    const float asym = (s.character == Character::tape) ? 0.08f * v * std::abs(v) : 0.0f;
                    v = std::tanh((v + asym) * satDrive) / (1.0f + drive * 1.5f);
                }
                tone[ch]+=toneCoefficient*(v-tone[ch]);v=tone[ch];
            } else if (std::abs(v) > 1.0f) {
                const float sgn = v > 0.f ? 1.f : -1.f;
                v = sgn * (1.0f + std::tanh(std::abs(v) - 1.0f));
            }
            v=lp[ch].tick(hp[ch].tick(v));
            // Bound the feedback storage without clipping the dry branch.
            ring[ch][write]=clamp(v+(feed[ch]-v)*freezeMix,-4,4);
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
