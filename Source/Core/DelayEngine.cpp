#include "DelayEngine.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <limits>
namespace oed {
namespace {
constexpr double pi=std::numbers::pi;
float finite(float x) {return std::isfinite(x)?x:0.f;}
float clamp(float x,float lo,float hi) {return std::clamp(finite(x),lo,hi);}
double rate(int i) {return beatRates[static_cast<std::size_t>(std::clamp(i,0,static_cast<int>(beatRates.size())-1))];}
double fraction(double x) {return x-std::floor(x);}
float wobbleNoise(std::uint64_t step) {
    std::uint32_t value=static_cast<std::uint32_t>(step)^static_cast<std::uint32_t>(step>>32)*0x9e3779b9u;
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;
    return static_cast<float>(value/2147483647.5-1.0);
}
float saturate(float x,float drive,bool tape) {
    const float amount=clamp(drive,0.f,1.f);
    const float k=1.f+amount*(tape?3.4f:2.2f);
    float y=x*k;
    if(tape&&amount>0.f) y+=(0.2f*amount)*y*std::abs(y);
    return std::tanh(y)/k;
}
float wobbleWave(double phase,WobbleShape shape) {
    const double position=fraction(phase);
    switch(shape) {
        case WobbleShape::triangle:return static_cast<float>(position<.5?4*position-1:3-4*position);
        case WobbleShape::square:return position<.5?-1.f:1.f;
        case WobbleShape::randomWalk: {
            const auto step=static_cast<std::uint64_t>(std::floor(phase));
            const float from=wobbleNoise(step),to=wobbleNoise(step+1);
            return from+(to-from)*static_cast<float>(position);
        }
        case WobbleShape::sampleHold:return wobbleNoise(static_cast<std::uint64_t>(std::floor(phase)));
        default:return static_cast<float>(std::sin(2*pi*position));
    }
}
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
    if(std::abs(targetLogHz-logHz)>1.e-4){logHz+=(targetLogHz-logHz)*smoothing;if(std::abs(targetLogHz-logHz)<=1.e-4)logHz=targetLogHz;coefficients();}
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
    for(auto& channel:diffusionHistory)for(auto& stage:channel)stage.assign(static_cast<std::size_t>(sampleRate*.08)+4,0);
    reset();
}
void DelayEngine::reset() {
    write=wetWrite=capture=valid=wetValid=0;phase=0;lastSlice=-1;initialized=false;
    diffusionWrite=0;wobblePhase=0;shimmerPhase=0;diffusion=0;diffusionSize=12;shimmer=0;stereoOffset=0;duckHold=0;
    envelope=0;lastDuck=1;tone={};toneHp={};held={};holdCounter=0;transition=0;reverseMix=0;wobbleValue=0;reverseChunk=1;
    for(auto& f:hp)f.clear();for(auto& f:lp)f.clear();
    for(auto& pair:tapHp)for(auto& f:pair)f.clear();
    for(auto& pair:tapLp)for(auto& f:pair)f.clear();
    for(auto& channel:diffusionHistory)for(auto& stage:channel)std::fill(stage.begin(),stage.end(),0.f);
    tapTone={};freezeMix=0;wasFrozen=false;frozenLoop=1;
}
float DelayEngine::read(const std::vector<float>& buffer,double time) const {
    time=std::clamp(time,1.,static_cast<double>(buffer.size()-2));
    const auto whole=static_cast<std::size_t>(time);const float part=static_cast<float>(time-whole);
    const auto a=(write+buffer.size()-whole)%buffer.size(),b=(a+buffer.size()-1)%buffer.size();
    const float av=whole<=valid?buffer[a]:0,bv=whole+1<=valid?buffer[b]:0;
    return av+(bv-av)*part;
}
float DelayEngine::readHermite(const std::vector<float>& buffer,double time) const {
    if(time<3.0) return read(buffer,time);
    time=std::clamp(time,2.0,static_cast<double>(buffer.size())-4.0);
    const auto i=static_cast<std::size_t>(time);const float t=static_cast<float>(time-static_cast<double>(i));
    const auto at=[&](std::size_t whole)->float {
        if(whole>valid||whole+1>=buffer.size()) return 0.f;
        return buffer[(write+buffer.size()-whole)%buffer.size()];
    };
    const float y0=at(i-1),y1=at(i),y2=at(i+1),y3=at(i+2);
    const float c1=.5f*(y2-y0),c2=y0-2.5f*y1+2.f*y2-.5f*y3,c3=.5f*(y3-y0)+1.5f*(y1-y2);
    return ((c3*t+c2)*t+c1)*t+y1;
}
float DelayEngine::readReverse(const std::vector<float>& buffer,double base,double chunk,double phase,bool hermite) const {
    const auto sample=[&](double pos){return hermite?readHermite(buffer,pos):read(buffer,pos);};
    const auto grain=[&](double ph) {
        ph-=std::floor(ph);
        const float w=static_cast<float>(.5-.5*std::cos(2*pi*ph));
        // Full gain at phase 0.5 reads `base + chunk/2` ago, and the head runs backward at 1x.
        const double pos=base+chunk*.5+(ph-.5)*2.0*chunk;
        return sample(std::max(1.0,pos))*w;
    };
    return grain(phase)+grain(phase+.5);
}
void DelayEngine::process(float* left,float* right,int frames,const Settings& s,const Transport& t,const float* sideLeft,const float* sideRight) {
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
    const float targetDiffusion=clamp(s.diffusion,0,1),targetDiffusionSize=clamp(s.diffusionSize,2,30);
    const float targetShimmer=clamp(s.shimmer,0,1),targetStereoOffset=clamp(s.stereoOffset,-20,20),targetDuckHold=clamp(s.duckHold,0,1);
    const int fadeLength=std::max(1,static_cast<int>(sampleRate*.025));
    if(!initialized) {
        delay=oldDelay=tapeDelay=nextDelay;previousLoop=oldLoop=tapeLoop=loop;gain=targetGain;pan=targetPan;
        feedback=targetFeedback;mix=targetMix;width=targetWidth;output=targetOutput;duck=targetDuck;
        glitchMix=targetGlitch;drive=targetDrive;motion=targetMotion;diffusion=targetDiffusion;diffusionSize=targetDiffusionSize;
        shimmer=targetShimmer;stereoOffset=targetStereoOffset;duckHold=targetDuckHold;freezeMix=s.freeze?1.f:0.f;
        reverseMix=clamp(s.reverse,0.f,1.f);reverseChunk=std::max(32.0,beatSamples*rate(s.reverseRate));initialized=true;
    } else if(transition==0 && (nextDelay!=delay||std::abs(loop-previousLoop)>.00001)) {
        oldDelay=delay;oldLoop=previousLoop;delay=nextDelay;previousLoop=loop;transition=fadeLength;
    }
    for(int ch=0;ch<2;++ch) {hp[static_cast<std::size_t>(ch)].configure(sampleRate,s.highPass,s.hpSlope,true);lp[static_cast<std::size_t>(ch)].configure(sampleRate,s.lowPass,s.lpSlope,false);}
    const float smooth=static_cast<float>(1-std::exp(-1/(sampleRate*.01)));
    const float freezeTarget=s.freeze?1.f:0.f;
    // Float rounding must not leave hold permanently short of unity at high sample rates.
    const float freezeTolerance=std::numeric_limits<float>::epsilon()/smooth;
    const double tapeSmooth=1-std::exp(-1/(sampleRate*.12));
    const float attack=static_cast<float>(std::exp(-1/(sampleRate*.005)));
    const float sideAttack=static_cast<float>(std::exp(-1/(sampleRate*.0015)));
    const double targetChunk=std::max(32.0,beatSamples*rate(s.reverseRate));
    const float chunkGlide=static_cast<float>(1-std::exp(-1/(sampleRate*.02)));
    for(int i=0;i<frames;++i) {
        const float dryL=finite(left[i]),dryR=right?finite(right[i]):dryL;
        const auto approach=[smooth](float& value,float target){value+=(target-value)*smooth;};
        approach(feedback,targetFeedback);approach(mix,targetMix);approach(width,targetWidth);approach(output,targetOutput);
        approach(freezeMix,freezeTarget);
        if(std::abs(freezeMix-freezeTarget)<=freezeTolerance)freezeMix=freezeTarget;
        approach(duck,targetDuck);approach(glitchMix,targetGlitch);approach(drive,targetDrive);approach(motion,targetMotion);
        approach(diffusion,targetDiffusion);approach(diffusionSize,targetDiffusionSize);
        approach(shimmer,targetShimmer);approach(stereoOffset,targetStereoOffset);approach(duckHold,targetDuckHold);
        approach(reverseMix,clamp(s.reverse,0.f,1.f));
        reverseChunk+=(targetChunk-reverseChunk)*chunkGlide;
        const float blend=transition>0?1-static_cast<float>(transition)/fadeLength:1;
        const bool tape=s.character==Character::tape;
        tapeLoop+=(loop-tapeLoop)*tapeSmooth;
        const float slew=static_cast<float>(1-std::exp(-1/(sampleRate*.0035)));
        wobbleValue+=(wobbleWave(wobblePhase,s.wobbleShape)-wobbleValue)*slew;
        const double flutter=std::sin(2*pi*wobblePhase*5.5);
        const double wobble=tape?motion*sampleRate*.0020*(.84*wobbleValue+.16*flutter):0;
        const double revPhase=fraction(phase/std::max(1.e-6,rate(s.reverseRate)));
        const auto delayed=[&](int ch,double newer,double older,double gliding) {
            const auto& buffer=ring[static_cast<std::size_t>(ch)];
            const auto pick=[&](double delay,bool modulated) {
                const float fwd=modulated?readHermite(buffer,delay):read(buffer,delay);
                if(reverseMix<1.e-5f) return fwd;
                return fwd+(readReverse(buffer,delay,reverseChunk,revPhase,modulated)-fwd)*reverseMix;
            };
            if(tape) return pick(gliding+wobble,true);
            const float a=pick(newer,false);return transition>0?pick(older,false)*(1.f-blend)+a*blend:a;
        };
        float fbL=delayed(0,previousLoop,oldLoop,tapeLoop),fbR=delayed(1,previousLoop,oldLoop,tapeLoop);
        if(shimmer>1.e-4f&&freezeMix<1) {
            const double grainSamples=std::max(16.,sampleRate*.042);
            const double phA=fraction(shimmerPhase),phB=fraction(shimmerPhase+.5);
            const float winA=static_cast<float>(.5-.5*std::cos(2*pi*phA));
            const float winB=static_cast<float>(.5-.5*std::cos(2*pi*phB));
            const double dA=std::max(1.,previousLoop-(1.-phA)*grainSamples);
            const double dB=std::max(1.,previousLoop-(1.-phB)*grainSamples);
            const float octL=read(ring[0],dA)*winA+read(ring[0],dB)*winB;
            const float octR=read(ring[1],dA)*winA+read(ring[1],dB)*winB;
            fbL=fbL*(1.f-.35f*shimmer)+octL*(.65f*shimmer);
            fbR=fbR*(1.f-.35f*shimmer)+octR*(.65f*shimmer);
        }
        if(freezeMix>=1) {fbL=read(ring[0],frozenLoop);fbR=read(ring[1],frozenLoop);}
        else if(freezeMix>0) {fbL+=(read(ring[0],frozenLoop)-fbL)*freezeMix;fbR+=(read(ring[1],frozenLoop)-fbR)*freezeMix;}
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
                if(tap.filter||tapHp[j][ch].fade>0||tapLp[j][ch].fade>0) {
                    v=tapLp[j][ch].tick(tapHp[j][ch].tick(v));
                }
                shaped[ch]=v;
            }
            l=shaped[0];r=shaped[1];
            if(s.routing==Routing::stereo){l*=1-std::max(0.f,pan[j]);r*=1+std::min(0.f,pan[j]);}
            wetL+=l*gain[j];wetR+=r*gain[j];
        }
        const float mono=(dryL+dryR)*.5f;
        std::array<float,2> feed{s.routing==Routing::stereo?dryL:mono,s.routing==Routing::stereo?dryR:(s.routing==Routing::mono?mono:0)};
        feed[0]*=1-freezeMix;feed[1]*=1-freezeMix;
        const float loopGain=freezeMix>=1?1.f:feedback+(1-feedback)*freezeMix;
        feed[0]+=fbL*loopGain;feed[1]+=fbR*loopGain;
        const bool updateHold=holdCounter==0;
        holdCounter=(holdCounter+1)%std::max(1,static_cast<int>(sampleRate/12000));
        for(std::size_t ch=0;ch<2;++ch) {
            float v=feed[ch];
            if(s.character!=Character::digital) {
                if(s.character==Character::loFi) {
                    if(updateHold)held[ch]=std::round(v*256.f)/256.f;
                    v=held[ch];
                } else v=saturate(v,drive,s.character==Character::tape);
                const float fc=s.character==Character::bbd?(2400.f-drive*600.f):(s.character==Character::tape?(4600.f-drive*1600.f):3200.f);
                const float coeff=static_cast<float>(1-std::exp(-2*pi*std::max(700.f,fc)/sampleRate));
                tone[ch]+=coeff*(v-tone[ch]);v=tone[ch];
                const float dc=static_cast<float>(1-std::exp(-2*pi*16.0/sampleRate));
                toneHp[ch]+=dc*(v-toneHp[ch]);v-=toneHp[ch];
            } else if (std::abs(v) > 1.0f) {
                const float sgn = v > 0.f ? 1.f : -1.f;
                v = sgn * (1.0f + std::tanh(std::abs(v) - 1.0f));
            }
            v=lp[ch].tick(hp[ch].tick(v));
            // Bound the feedback storage without clipping the dry branch.
            float stored=clamp(freezeMix>=1?feed[ch]:v+(feed[ch]-v)*freezeMix,-4,4);
            if(freezeMix<1&&diffusion>1.e-4f) {
                const float coefficient=diffusionStageFeedback*diffusion;
                for(std::size_t stage=0;stage<diffusionStageLengths.size();++stage) {
                    auto& history=diffusionHistory[ch][stage];
                    const auto delaySamples=static_cast<std::size_t>(std::clamp(std::round(sampleRate*diffusionSize*.001*diffusionStageLengths[stage]),1.,static_cast<double>(history.size()-2)));
                    const auto readAt=(diffusionWrite+history.size()-delaySamples)%history.size();
                    const float output=history[readAt]-coefficient*stored;
                    history[diffusionWrite]=stored+coefficient*output;stored=output;
                }
            } else for(auto& history:diffusionHistory[ch])history[diffusionWrite]=stored;
            ring[ch][write]=clamp(stored,-4,4);
        }
        write=(write+1)%ring[0].size();valid=std::min(valid+1,ring[0].size());
        diffusionWrite=(diffusionWrite+1)%diffusionHistory[0][0].size();
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
        if(std::abs(stereoOffset)>1.e-3f&&s.routing!=Routing::mono) {
            const double offsetSamples=std::clamp(std::abs(static_cast<double>(stereoOffset))*sampleRate*.001,0.,static_cast<double>(wetHistory[0].size()-3));
            const auto whole=static_cast<std::size_t>(offsetSamples);const float part=static_cast<float>(offsetSamples-whole);
            const int delayedCh=stereoOffset>0?1:0;
            const auto& hist=wetHistory[static_cast<std::size_t>(delayedCh)];
            const auto idxA=(wetWrite+hist.size()-1-whole)%hist.size(),idxB=(idxA+hist.size()-1)%hist.size();
            const float valA=whole<wetValid?hist[idxA]:0.f,valB=whole+1<wetValid?hist[idxB]:0.f;
            wet[static_cast<std::size_t>(delayedCh)]=valA+(valB-valA)*part;
        }
        if(duck>1.e-5f){
            const bool followInput=s.inputDuck||s.duckSource==DuckSource::input;
            const bool followSide=!followInput&&s.duckSource==DuckSource::sidechain;
            if(followInput||followSide){
                const float releaseHoldScale=1.f+duckHold*3.f;
                const float heldRelease=static_cast<float>(std::exp(-1/(beatSamples*rate(s.duckRate)*releaseHoldScale*.28)));
                float peak=0.f;
                if(followInput) peak=std::max(std::abs(dryL),std::abs(dryR));
                else {
                    if(sideLeft!=nullptr) peak=std::abs(sideLeft[i]);
                    if(sideRight!=nullptr) peak=std::max(peak,std::abs(sideRight[i]));
                }
                const float coeff=peak>envelope?(followSide?sideAttack:attack):heldRelease;
                envelope=peak+(envelope-peak)*coeff;
                lastDuck=std::exp(-duck*envelope*8.f);
            }else{
                const double duckPhase=fraction(phase/rate(s.duckRate)+1.e-9);
                const double holdFrac=duckHold*.55;
                const double shapedPhase=duckPhase<=holdFrac?0.:(duckPhase-holdFrac)/std::max(1.e-4,1.-holdFrac);
                const float u=static_cast<float>(1.-shapedPhase);
                lastDuck=1-duck*(u*u*u);
            }
        }else{
            lastDuck=1.f;
        }
        const float mid=(wet[0]+wet[1])*.5f,side=(wet[0]-wet[1])*.5f*width;
        wetL=(mid+side)*lastDuck;wetR=(mid-side)*lastDuck;
        if(s.routing==Routing::mono)wetL=wetR=mid*lastDuck;
        if(right){left[i]=(dryL*(1-mix)+wetL*mix)*output;right[i]=(dryR*(1-mix)+wetR*mix)*output;}
        else left[i]=(dryL*(1-mix)+(wetL+wetR)*.5f*mix)*output;
        phase+=increment;wobblePhase+=clamp(s.wobbleRate,.05f,8.f)/sampleRate;
        shimmerPhase+=1./std::max(16.,sampleRate*.042);
        if(transition>0)--transition;
    }
}
}
