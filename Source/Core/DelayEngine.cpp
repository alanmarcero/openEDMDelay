#include "DelayEngine.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <limits>
namespace oed {
namespace {
constexpr double pi=std::numbers::pi;
constexpr double oversampleDelay=11.0;
constexpr std::array<float,6> halfbandTaps{0.311564109f,-0.087544441f,0.036694410f,-0.014513888f,0.004418691f,-0.000618881f};
constexpr std::array<double,4> hadamardVec{0.6,0.5,-0.5,0.37416573867739417};
constexpr std::array<std::array<double,4>,2> diffusionRatios{{{0.43,0.71,1.09,1.51},{0.51,0.83,1.21,1.63}}};
constexpr std::array<double,4> diffusionModRates{0.37,0.61,0.89,1.23};
float finite(float x) {return std::isfinite(x)?x:0.f;}
float clamp(float x,float lo,float hi) {return std::clamp(finite(x),lo,hi);}
double rate(int i) {return beatRates[static_cast<std::size_t>(std::clamp(i,0,static_cast<int>(beatRates.size())-1))];}
double fraction(double x) {return x-std::floor(x);}
float wobbleNoise(std::uint64_t step) {
    std::uint32_t value=static_cast<std::uint32_t>(step)^static_cast<std::uint32_t>(step>>32)*0x9e3779b9u;
    value^=value>>16;value*=0x7feb352du;value^=value>>15;value*=0x846ca68bu;value^=value>>16;
    return static_cast<float>(value/2147483647.5-1.0);
}
float softBound(float x) {
    const float absX=std::abs(x);
    if(absX<=1.5f) return x;
    const float sign=x>=0.f?1.f:-1.f;
    return sign*(1.5f+2.5f*std::tanh((absX-1.5f)/2.5f));
}
double svfLowpass(double x,double g,double r,double& ic1,double& ic2) {
    const double a1=1.0/(1.0+g*(g+2.0*r));
    const double a2=g*a1,a3=g*a2;
    const double v3=x-ic2;
    const double v1=a1*ic1+a2*v3;
    const double v2=ic2+a2*ic1+a3*v3;
    ic1=2.0*v1-ic1;
    ic2=2.0*v2-ic2;
    return v2;
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
    const double fc=std::clamp(std::exp(logHz),10.0,sr*0.46);
    const double g=std::tan(pi*fc/sr);
    if(slope==1) {
        stages[0].g=g/(1.0+g);
        return;
    }
    for(int i=0;i<count;++i) {
        auto& st=stages[static_cast<std::size_t>(i)];
        st.g=g;
        st.r=std::cos(pi*(2*i+1)/(4*count));
        st.a1=1.0/(1.0+g*(g+2.0*st.r));
        st.a2=g*st.a1;
        st.a3=g*st.a2;
    }
}
void DelayEngine::Filter::configure(double sampleRate,float hz,int nextSlope,bool high) {
    sr=sampleRate;isHigh=high;nextSlope=std::clamp(nextSlope,0,4);
    smoothing=1-std::exp(-1/(sr*.03));fadeLength=std::max(1,static_cast<int>(sr*.02));
    targetLogHz=std::log(std::clamp(static_cast<double>(hz),10.,sr*.45));
    if(!primed){slope=nextSlope;logHz=targetLogHz;coefficients();primed=true;return;}
    if(nextSlope!=slope&&fade==0){oldStages=stages;oldSlope=slope;stages={};slope=nextSlope;coefficients();fade=fadeLength;}
}
float DelayEngine::Filter::tick(float x) {
    if(std::abs(targetLogHz-logHz)>1.e-4){logHz+=(targetLogHz-logHz)*smoothing;if(std::abs(targetLogHz-logHz)<=1.e-4)logHz=targetLogHz;coefficients();}
    const auto process=[high=isHigh](auto& chain,int order,double y) {
        if(order<=0) return y;
        if(order==1) {
            auto& st=chain[0];
            const double v=(y-st.ic1)*st.g;
            const double lpOut=v+st.ic1;
            st.ic1=lpOut+v;
            return high?(y-lpOut):lpOut;
        }
        constexpr int stageCounts[]{1,1,1,2,4};
        const int count=stageCounts[std::clamp(order,1,4)];
        for(int i=0;i<count;++i) {
            auto& st=chain[static_cast<std::size_t>(i)];
            const double v3=y-st.ic2;
            const double v1=st.a1*st.ic1+st.a2*v3;
            const double v2=st.ic2+st.a2*st.ic1+st.a3*v3;
            st.ic1=2.0*v1-st.ic1;
            st.ic2=2.0*v2-st.ic2;
            y=high?(y-2.0*st.r*v1-v2):v2;
        }
        return y;
    };
    double y=process(stages,slope,x);
    if(fade>0){const double old=process(oldStages,oldSlope,x),blend=1-static_cast<double>(fade)/fadeLength;y=old+(y-old)*blend;--fade;}
    return static_cast<float>(y);
}
void DelayEngine::prepare(double sr) {
    sampleRate = std::clamp(std::isfinite(sr) ? sr : 48000.0, 8000.0, 384000.0);
    // Bound readHermite() timing offsets to the allocated history length.
    for(auto& buffer:ring)buffer.assign(static_cast<std::size_t>(sampleRate*96.1)+16,0);
    for(auto& buffer:revRing)buffer.assign(static_cast<std::size_t>(sampleRate*24.1)+16,0);
    for(auto& buffer:wetHistory)buffer.assign(static_cast<std::size_t>(sampleRate*12.1)+8,0);
    for(auto& channel:diffusionHistory)for(auto& stage:channel)stage.assign(std::max<std::size_t>(64,static_cast<std::size_t>(sampleRate*.08)+8),0);
    duckRmsWindow.assign(std::max<std::size_t>(1,static_cast<std::size_t>(std::lround(sampleRate*0.005))),0.f);
    reset();
}
void DelayEngine::reset() {
    write=revWrite=wetWrite=capture=valid=revValid=wetValid=0;phase=0;lastSlice=-1;initialized=false;
    diffusionWrite=0;wobblePhase=0;shimmerPhase=0;diffusion=0;diffusionSize=12;shimmer=0;stereoOffset=0;duckHold=0;
    duckWinPos=0;duckSumSq=0;duckPeakEnv=0;duckGainDb=0;duckHoldCounter=0;lastDuck=1;
    ouState=0;ouSmooth=0;diffModPhase=0;lofiPhase=0;ouCounter=0;lofiCounter=0;
    held={};transition=0;reverseMix=0;wobbleValue=0;reverseChunk=1;
    shimmerOffset={};revGrainId={-1,-1,-1,-1};revSpliceOffset={};diffThiranState={};
    tapeMag={};tapePrevH={};tapeHp={};tapeDispIn={};tapeDispOut={};
    bbdPreLp={};bbdCompRms={};bbdDc={};bbdExpRms={};bbdDeLp={};
    lofiDitherPrev={};lofiQuantErr={};
    oversampler={};shimmerLpA={};shimmerLpB={};tapeBump={};tapeGapLp={};bbdLpA={};bbdLpB={};lofiPreLp={};lofiRecLp={};
    for(auto& f:hp)f.clear();for(auto& f:lp)f.clear();
    for(auto& pair:tapHp)for(auto& f:pair)f.clear();
    for(auto& pair:tapLp)for(auto& f:pair)f.clear();
    for(auto& pair:oldTapHp)for(auto& f:pair)f.clear();
    for(auto& pair:oldTapLp)for(auto& f:pair)f.clear();
    for(auto& channel:diffusionHistory)for(auto& stage:channel)std::fill(stage.begin(),stage.end(),0.f);
    std::fill(duckRmsWindow.begin(),duckRmsWindow.end(),0.f);
    tapTone={};oldTapTone={};freezeMix=0;wasFrozen=false;frozenLoop=1;
}
float DelayEngine::readHermite(const std::vector<float>& buffer,std::size_t head,std::size_t validCount,double time) const {
    if(buffer.size()<6) return 0.f;
    time=std::clamp(time,1.0,static_cast<double>(buffer.size()-4));
    const auto idx=static_cast<std::size_t>(time);
    const float frac=static_cast<float>(time-static_cast<double>(idx));
    const auto sampleAt=[&](std::size_t whole)->float {
        if(whole<1||whole>validCount||whole>=buffer.size()) return 0.f;
        return buffer[(head+buffer.size()-whole)%buffer.size()];
    };
    if(idx<2) {
        const float y1=sampleAt(1),y2=sampleAt(2),y3=sampleAt(3);
        return y1+frac*(-1.5f*y1+2.f*y2-0.5f*y3)+frac*frac*(0.5f*y1-y2+0.5f*y3);
    }
    const float y0=sampleAt(idx-1),y1=sampleAt(idx),y2=sampleAt(idx+1),y3=sampleAt(idx+2);
    const float c1=.5f*(y2-y0),c2=y0-2.5f*y1+2.f*y2-.5f*y3,c3=.5f*(y3-y0)+1.5f*(y1-y2);
    return ((c3*frac+c2)*frac+c1)*frac+y1;
}
void DelayEngine::process(float* left,float* right,int frames,const Settings& s,const Transport& t,const float* sideLeft,const float* sideRight) {
    if(ring[0].empty()||frames<=0)return;
    const double bpm=std::isfinite(t.bpm)?std::clamp(t.bpm,20.,400.):120.;
    const double beatSamples=sampleRate*60/bpm,unit=beatSamples*rate(s.rate);
    const double increment=1/beatSamples;
    if(t.hasPpq&&std::isfinite(t.ppq))phase=t.ppq;
    const double blockStartPhase=phase;
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
            oldTapHp[j][ch].configure(sampleRate,tap.highPass,tap.filter?2:0,true);
            oldTapLp[j][ch].configure(sampleRate,tap.lowPass,tap.filter?2:0,false);
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
    const bool varispeed=(s.character==Character::tape||s.character==Character::bbd);
    if(!initialized) {
        delay=oldDelay=tapeDelay=nextDelay;previousLoop=oldLoop=tapeLoop=loop;gain=targetGain;pan=targetPan;
        feedback=targetFeedback;mix=targetMix;width=targetWidth;output=targetOutput;duck=targetDuck;
        glitchMix=targetGlitch;drive=targetDrive;motion=targetMotion;diffusion=targetDiffusion;diffusionSize=targetDiffusionSize;
        shimmer=targetShimmer;stereoOffset=targetStereoOffset;duckHold=targetDuckHold;freezeMix=s.freeze?1.f:0.f;
        reverseMix=clamp(s.reverse,0.f,1.f);reverseChunk=std::max(32.0,beatSamples*rate(s.reverseRate));initialized=true;
    }
    if(!varispeed&&transition==0&&(nextDelay!=delay||std::abs(loop-previousLoop)>.00001)) {
        oldDelay=delay;oldLoop=previousLoop;oldTapHp=tapHp;oldTapLp=tapLp;oldTapTone=tapTone;
        delay=nextDelay;previousLoop=loop;transition=fadeLength;
    }
    if(varispeed) {delay=nextDelay;previousLoop=loop;transition=0;}
    for(int ch=0;ch<2;++ch) {hp[static_cast<std::size_t>(ch)].configure(2.0*sampleRate,s.highPass,s.hpSlope,true);lp[static_cast<std::size_t>(ch)].configure(2.0*sampleRate,s.lowPass,s.lpSlope,false);}
    const float smooth=static_cast<float>(1-std::exp(-1/(sampleRate*.01)));
    const float freezeTarget=s.freeze?1.f:0.f;
    // Float rounding must not leave hold permanently short of unity at high sample rates.
    const float freezeTolerance=std::numeric_limits<float>::epsilon()/smooth;
    const double tapeSmooth=1-std::exp(-1/(sampleRate*.12));
    const double targetChunk=std::max(32.0,beatSamples*rate(s.reverseRate));
    const float chunkGlide=static_cast<float>(1-std::exp(-1/(sampleRate*.02)));
    const double ouDecay=std::exp(-18.0/sampleRate);
    const double ouDrive=std::sqrt(std::max(0.0,1.0-ouDecay*ouDecay));
    const double ouFilter=1.0-std::exp(-2.0*pi*14.0/sampleRate);
    const double shimmerLpG=std::tan(pi*std::min(9000.0,sampleRate*0.42)/sampleRate);
    const double tapeHpAlpha=1.0-std::exp(-2.0*pi*38.0/sampleRate);
    const double tapeBumpG=std::tan(pi*std::min(95.0,sampleRate*0.4)/sampleRate);
    const double tapeDispTan=std::tan(pi*std::min(1400.0,sampleRate*0.4)/sampleRate);
    const double tapeDispCoeff=(tapeDispTan-1.0)/(tapeDispTan+1.0);
    const float bbdEmpAlpha=static_cast<float>(1.0-std::exp(-2.0*pi*std::min(1800.0,sampleRate*0.4)/sampleRate));
    const float bbdDeAlpha=static_cast<float>(1.0-std::exp(-2.0*pi*std::min(2970.0,sampleRate*0.44)/sampleRate));
    const float bbdRmsAlpha=static_cast<float>(1.0-std::exp(-1.0/(sampleRate*0.008)));
    const float bbdDcAlpha=static_cast<float>(1.0-std::exp(-2.0*pi*18.0/sampleRate));
    const float fastPeakRise=static_cast<float>(1.0-std::exp(-1.0/(sampleRate*0.0008)));
    const float fastPeakFall=static_cast<float>(1.0-std::exp(-1.0/(sampleRate*0.005)));
    const float duckAttackSide=static_cast<float>(1.0-std::exp(-1.0/(sampleRate*0.0022)));
    const float duckAttackIn=static_cast<float>(1.0-std::exp(-1.0/(sampleRate*0.0038)));
    for(int i=0;i<frames;++i) {
        phase=blockStartPhase+static_cast<double>(i)*increment;
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
        const float fadeAngle=0.5f*static_cast<float>(pi)*blend;
        const float gainOld=transition>0?std::cos(fadeAngle):0.f;
        const float gainNew=transition>0?std::sin(fadeAngle):1.f;
        tapeLoop+=(loop-tapeLoop)*tapeSmooth;
        const float slew=static_cast<float>(1-std::exp(-1/(sampleRate*.0035)));
        wobbleValue+=(wobbleWave(wobblePhase,s.wobbleShape)-wobbleValue)*slew;

        // Pair deterministic wow with stochastic OU flutter so modulation never repeats.
        const double xi=0.8660254*(static_cast<double>(wobbleNoise(ouCounter*2u))+static_cast<double>(wobbleNoise(ouCounter*2u+1u)));
        ++ouCounter;
        ouState=ouDecay*ouState+ouDrive*xi;
        ouSmooth+=ouFilter*(ouState-ouSmooth);
        const double tapeWobble=motion*sampleRate*0.0020*(0.78*wobbleValue+0.22*ouSmooth);
        const double bbdWobble=motion*sampleRate*0.0007*wobbleValue;
        const double wobble=s.character==Character::tape?tapeWobble:0.0;
        const double modSamples=s.character==Character::bbd?bbdWobble:wobble;

        const double revSweepPhase=fraction(phase/std::max(1.e-6,rate(s.reverseRate))+1.e-9);
        const double wsolaLen=std::clamp(reverseChunk*0.25,32.0,sampleRate*0.040);
        const double wsolaGrid=(phase*beatSamples)/wsolaLen+1.e-9;
        const double wsolaBasePhase=fraction(wsolaGrid);
        for(std::size_t k=0;k<4;++k) {
            const auto grainIndex=static_cast<std::int64_t>(std::floor(wsolaGrid+0.25*static_cast<double>(k)));
            if(reverseMix>1.e-5f&&grainIndex!=revGrainId[k]) {
                revGrainId[k]=grainIndex;
                const std::size_t refIdx=(k+2)&3u;
                const double refBase=std::max(previousLoop,0.5*reverseChunk+40.0);
                const int maxLag=std::clamp(static_cast<int>(sampleRate*0.003),2,24);
                const int stride=std::max(1,maxLag/6);
                for(std::size_t sweep=0;sweep<2;++sweep) {
                    const double sweepPh=fraction(revSweepPhase+0.5*static_cast<double>(sweep));
                    const double macroPos=refBase+0.5*reverseChunk+(sweepPh-0.5)*2.0*reverseChunk;
                    for(std::size_t ch=0;ch<2;++ch) {
                        const auto& rbuf=revRing[ch];
                        const double refPos=std::max(4.0,macroPos+revSpliceOffset[ch][sweep][refIdx]);
                        double bestScore=-2.0,bestLag=0.0;
                        for(int lag=-maxLag;lag<=maxLag;lag+=stride) {
                            if(macroPos+lag<4.0) continue;
                            double dot=0.0,eA=0.0,eB=0.0;
                            for(int m=0;m<6;++m) {
                                const float a=readHermite(rbuf,revWrite,revValid,refPos+m*2.0);
                                const float b=readHermite(rbuf,revWrite,revValid,macroPos+lag+m*2.0);
                                dot+=static_cast<double>(a)*static_cast<double>(b);
                                eA+=static_cast<double>(a)*static_cast<double>(a);
                                eB+=static_cast<double>(b)*static_cast<double>(b);
                            }
                            const double score=(eA>1.e-7&&eB>1.e-7)?dot/std::sqrt(eA*eB):0.0;
                            const double biased=score-(lag==0?0.0:1.e-3);
                            if(biased>bestScore) {bestScore=biased;bestLag=static_cast<double>(lag);}
                        }
                        revSpliceOffset[ch][sweep][k]=bestLag;
                    }
                }
            }
        }

        const auto readBranch=[&](std::size_t ch,double targetDelay) {
            const double compDelay=std::max(1.0,targetDelay-oversampleDelay);
            if(reverseMix<1.e-5f) return readHermite(ring[ch],write,valid,compDelay);
            const double ringDelay=std::max(1.0,targetDelay+0.5*reverseChunk*reverseMix-oversampleDelay);
            const float fromRing=readHermite(ring[ch],write,valid,ringDelay);
            const double safeBase=std::max(targetDelay,0.5*reverseChunk+40.0);
            double revTotal=0.0;
            for(std::size_t sweep=0;sweep<2;++sweep) {
                const double sweepPh=fraction(revSweepPhase+0.5*static_cast<double>(sweep));
                const float sweepWin=static_cast<float>(0.5-0.5*std::cos(2.0*pi*sweepPh));
                if(sweepWin<1.e-6f) continue;
                const double macroPos=safeBase+0.5*reverseChunk+(sweepPh-0.5)*2.0*reverseChunk;
                double wsolaSum=0.0;
                for(std::size_t k=0;k<4;++k) {
                    const double wPh=fraction(wsolaBasePhase+0.25*static_cast<double>(k));
                    const float hann4x=static_cast<float>(0.25*(1.0-std::cos(2.0*pi*wPh)));
                    const double pos=std::max(2.0,macroPos+revSpliceOffset[ch][sweep][k]);
                    wsolaSum+=static_cast<double>(readHermite(revRing[ch],revWrite,revValid,pos))*hann4x;
                }
                revTotal+=wsolaSum*sweepWin;
            }
            return fromRing+static_cast<float>(revTotal)*reverseMix;
        };

        float fbL=0.f,fbR=0.f;
        for(std::size_t ch=0;ch<2;++ch) {
            float fbVal=0.f;
            if(varispeed) fbVal=readBranch(ch,tapeLoop+modSamples);
            if(!varispeed) {
                const float newer=readBranch(ch,previousLoop);
                fbVal=transition>0?(gainOld*readBranch(ch,oldLoop)+gainNew*newer):newer;
            }
            if(ch==0) fbL=fbVal;
            if(ch==1) fbR=fbVal;
        }

        if(shimmer>1.e-4f&&freezeMix<1) {
            const double activeLoop=varispeed?tapeLoop:previousLoop;
            const double compLoop=std::max(2.0,activeLoop-oversampleDelay);
            const double grainSamples=std::clamp(compLoop*0.85,16.0,sampleRate*.042);
            const double basePos=std::max(2.0,compLoop-0.5*grainSamples);
            const double prevShA=fraction(shimmerPhase),prevShB=fraction(shimmerPhase+.5);
            shimmerPhase+=1./grainSamples;
            const double phA=fraction(shimmerPhase),phB=fraction(shimmerPhase+.5);
            for(std::size_t idx=0;idx<2;++idx) {
                const double curPh=idx==0?phA:phB;
                const double prevPh=idx==0?prevShA:prevShB;
                const double otherPh=idx==0?phB:phA;
                if(curPh<prevPh-0.5) {
                    const double refPos=basePos+(1.0-otherPh)*grainSamples+shimmerOffset[1u-idx];
                    const double nomStart=basePos+grainSamples;
                    const float refVal=0.5f*(readHermite(ring[0],write,valid,refPos)+readHermite(ring[1],write,valid,refPos));
                    const float refSlope=0.5f*(readHermite(ring[0],write,valid,refPos+1.0)+readHermite(ring[1],write,valid,refPos+1.0))-refVal;
                    const int maxShift=std::clamp(static_cast<int>(sampleRate*0.003),2,24);
                    double best=-1.e9,chosen=0.0;
                    for(int shift=-maxShift;shift<=maxShift;shift+=2) {
                        if(basePos+shift<2.0) continue;
                        const double cand=nomStart+shift;
                        const float val=0.5f*(readHermite(ring[0],write,valid,cand)+readHermite(ring[1],write,valid,cand));
                        const float slp=0.5f*(readHermite(ring[0],write,valid,cand+1.0)+readHermite(ring[1],write,valid,cand+1.0))-val;
                        const double score=static_cast<double>(val*refVal+slp*refSlope)-(shift==0?0.0:1.e-6);
                        if(score>best) {best=score;chosen=static_cast<double>(shift);}
                    }
                    shimmerOffset[idx]=chosen;
                }
            }
            const float winA=static_cast<float>(.5-.5*std::cos(2*pi*phA));
            const float winB=static_cast<float>(.5-.5*std::cos(2*pi*phB));
            const double dA=std::max(2.0,basePos+(1.0-phA)*grainSamples+shimmerOffset[0]);
            const double dB=std::max(2.0,basePos+(1.0-phB)*grainSamples+shimmerOffset[1]);
            for(std::size_t ch=0;ch<2;++ch) {
                const double rawOct=static_cast<double>(readHermite(ring[ch],write,valid,dA)*winA+readHermite(ring[ch],write,valid,dB)*winB);
                const double stage1=svfLowpass(rawOct,shimmerLpG,0.9238795,shimmerLpA[ch].ic1,shimmerLpA[ch].ic2);
                const float oct=static_cast<float>(svfLowpass(stage1,shimmerLpG,0.3826834,shimmerLpB[ch].ic1,shimmerLpB[ch].ic2));
                if(ch==0) fbL=fbL*(1.f-.5f*shimmer)+oct*(.55f*shimmer);
                if(ch==1) fbR=fbR*(1.f-.5f*shimmer)+oct*(.55f*shimmer);
            }
        }
        const double compFrozen=std::max(1.0,frozenLoop-oversampleDelay);
        if(freezeMix>=1) {fbL=readHermite(ring[0],write,valid,compFrozen);fbR=readHermite(ring[1],write,valid,compFrozen);}
        if(freezeMix>0&&freezeMix<1) {fbL+=(readHermite(ring[0],write,valid,compFrozen)-fbL)*freezeMix;fbR+=(readHermite(ring[1],write,valid,compFrozen)-fbR)*freezeMix;}
        if(s.routing==Routing::pingPong)std::swap(fbL,fbR);
        float wetL=0,wetR=0;
        for(std::size_t j=0;j<tapCount;++j) {
            approach(gain[j],targetGain[j]);approach(pan[j],targetPan[j]);
            tapeDelay[j]+=(nextDelay[j]-tapeDelay[j])*tapeSmooth;
            if(gain[j]<1.e-7f)continue;
            const auto& tap=s.taps[j];
            const int repeats=std::clamp(tap.ratchets,1,8);
            const int branchCount=(!varispeed&&transition>0)?2:1;
            float l=0,r=0;
            for(int branch=0;branch<branchCount;++branch) {
                const auto& discreteDelays=branch==0?delay:oldDelay;
                const auto& branchDelays=varispeed?tapeDelay:discreteDelays;
                auto& branchTone=branch==0?tapTone:oldTapTone;
                auto& branchHp=branch==0?tapHp:oldTapHp;
                auto& branchLp=branch==0?tapLp:oldTapLp;
                const float xfadeGain=branch==0?gainNew:gainOld;
                const float branchGain=branchCount==1?1.f:xfadeGain;
                float bl=0,br=0;
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
                    const double tapPos=branchDelays[j]+offset+(varispeed?modSamples:0.0);
                    bl+=readBranch(0,tapPos)/repeats;
                    br+=readBranch(1,tapPos)/repeats;
                }
                std::array<float,2> shaped{bl,br};
                for(std::size_t ch=0;ch<2;++ch) {
                    float v=shaped[ch];
                    if(tap.character==3)v=std::round(v*64.f)/64.f;
                    if(tap.character==1||tap.character==2) {
                        const float amount=clamp(tap.drive,0,1);
                        v=std::tanh(v*(1+4*amount))/(1+1.5f*amount);
                        branchTone[j][ch]+=tapToneCoefficient[j]*(v-branchTone[j][ch]);v=branchTone[j][ch];
                    }
                    if(tap.filter||branchHp[j][ch].fade>0||branchLp[j][ch].fade>0) {
                        v=branchLp[j][ch].tick(branchHp[j][ch].tick(v));
                    }
                    shaped[ch]=v;
                }
                l+=shaped[0]*branchGain;
                r+=shaped[1]*branchGain;
            }
            if(s.routing==Routing::stereo){l*=1-std::max(0.f,pan[j]);r*=1+std::min(0.f,pan[j]);}
            wetL+=l*gain[j];wetR+=r*gain[j];
        }

        // Expand 1:2 and de-emphasize highs to suppress BBD clock hiss.
        if(s.character==Character::bbd) {
            std::array<float,2> bbdWet{wetL,wetR};
            for(std::size_t ch=0;ch<2;++ch) {
                const float w=bbdWet[ch];
                bbdExpRms[ch]+=bbdRmsAlpha*(w*w-bbdExpRms[ch]);
                const float rmsOut=std::sqrt(std::max(0.f,bbdExpRms[ch])+1.e-8f);
                const float expanded=w*std::min(3.5f,rmsOut/0.25f);
                bbdDeLp[ch]+=bbdDeAlpha*(expanded-bbdDeLp[ch]);
                bbdWet[ch]=bbdDeLp[ch]+(expanded-bbdDeLp[ch])/1.65f;
            }
            wetL=bbdWet[0];wetR=bbdWet[1];
        }

        const float mono=(dryL+dryR)*.5f;
        const float routedLeft=s.routing==Routing::stereo?dryL:mono;
        float routedRight=0.f;
        if(s.routing==Routing::stereo) routedRight=dryR;
        if(s.routing==Routing::mono) routedRight=mono;
        std::array<float,2> dryRouted{routedLeft,routedRight};

        // Pre-emphasize highs and 2:1-compress input to match the output expander.
        if(s.character==Character::bbd) {
            for(std::size_t ch=0;ch<2;++ch) {
                const float x=dryRouted[ch];
                bbdPreLp[ch]+=bbdEmpAlpha*(x-bbdPreLp[ch]);
                const float pre=x+0.65f*(x-bbdPreLp[ch]);
                bbdCompRms[ch]+=bbdRmsAlpha*(pre*pre-bbdCompRms[ch]);
                const float rmsIn=std::sqrt(std::max(0.f,bbdCompRms[ch])+1.e-8f);
                dryRouted[ch]=pre*std::min(3.5f,std::sqrt(0.25f/std::max(rmsIn,1.e-4f)));
            }
        }

        const float dryActive=1.f-freezeMix;
        revRing[0][revWrite]=dryRouted[0]*dryActive;
        revRing[1][revWrite]=dryRouted[1]*dryActive;
        revWrite=(revWrite+1)%revRing[0].size();
        revValid=std::min(revValid+1,revRing[0].size());

        const float fwdDryScale=dryActive*(1.f-reverseMix);
        const float loopGain=freezeMix>=1?1.f:feedback+(1-feedback)*freezeMix;
        std::array<float,2> feed{dryRouted[0]*fwdDryScale+fbL*loopGain,dryRouted[1]*fwdDryScale+fbR*loopGain};

        const double decimRate=std::clamp(12000.0-drive*4000.0,3200.0,sampleRate*0.75);
        lofiPhase+=decimRate/sampleRate;
        const bool updateHold=lofiPhase>=1.0;
        if(updateHold) lofiPhase-=std::floor(lofiPhase);
        const double lofiCut=std::clamp(0.45*decimRate,800.0,sampleRate*0.43);
        const double lofiG=std::tan(pi*lofiCut/sampleRate);
        const double bbdCut=std::clamp(680.0/std::max(0.02,tapeLoop/sampleRate),750.0,std::min(9500.0,sampleRate*0.42));
        const double bbdG2x=std::tan(pi*bbdCut/(2.0*sampleRate));
        const double tapeGapG=std::tan(pi*std::clamp(4800.0-drive*1800.0,900.0,sampleRate*0.43)/sampleRate);

        for(std::size_t ch=0;ch<2;++ch) {
            auto& os=oversampler[ch];
            os.inHist[os.pos]=feed[ch];
            float sEven=0.f;
            for(std::size_t m=0;m<6;++m) {
                const float a=os.inHist[(os.pos+12-(5-m))%12];
                const float b=os.inHist[(os.pos+12-(6+m))%12];
                sEven+=(2.f*halfbandTaps[m])*(a+b);
            }
            const float sOdd=os.inHist[(os.pos+7)%12];

            for(int sub=0;sub<2;++sub) {
                const float u=sub==0?sEven:sOdd;
                float shapedSub=softBound(u);
                if(s.character==Character::tape) {
                    const double driveScale=1.0+2.6*drive;
                    const double field=static_cast<double>(u)*driveScale;
                    const double dH=field-tapePrevH[ch];
                    tapePrevH[ch]=field;
                    const double effectiveField=field+(0.06+0.10*drive)*tapeMag[ch];
                    const double anhyst=std::tanh(effectiveField/1.05);
                    const double dir=dH>=0.0?1.0:-1.0;
                    const double kp=0.28+0.22*drive;
                    const double step=std::abs(dH)/(kp+std::abs(dH));
                    const double pull=dir*(anhyst-tapeMag[ch])>0.0?step*(anhyst-tapeMag[ch]):0.0;
                    tapeMag[ch]=std::clamp(0.55*(tapeMag[ch]*0.9995+pull)+0.45*anhyst,-1.0,1.0);
                    const double norm=driveScale*(0.66+0.16*drive)/1.05;
                    shapedSub=softBound(static_cast<float>(tapeMag[ch]/std::max(0.5,norm)));
                }
                if(s.character==Character::bbd) {
                    const float k=1.f+2.0f*drive;
                    const float asym=(0.08f+0.12f*drive)*u*u;
                    shapedSub=softBound((std::tanh(u*k+asym)-std::tanh(asym))/k);
                }
                if(s.character==Character::digital&&std::abs(u)>1.f) {
                    const float sgn=u>0.f?1.f:-1.f;
                    shapedSub=softBound(sgn*(1.f+std::tanh(std::abs(u)-1.f)));
                }
                if(s.character==Character::digital&&std::abs(u)<=1.f) shapedSub=u;
                if(sub==0) os.resEven[os.pos]=shapedSub-sEven;
                if(sub==1) os.resOdd[os.oddPos]=shapedSub-sOdd;
            }

            float resBandlimited=0.5f*os.resOdd[(os.oddPos+1)%7];
            for(std::size_t m=0;m<6;++m) {
                const float e0=os.resEven[(os.pos+12-(5-m))%12];
                const float e1=os.resEven[(os.pos+12-(6+m))%12];
                resBandlimited+=halfbandTaps[m]*(e0+e1);
            }
            const float linearDelayed=os.inHist[(os.pos+1)%12];
            float v=linearDelayed+resBandlimited;
            os.pos=(os.pos+1)%12;
            os.oddPos=(os.oddPos+1)%7;

            if(s.character==Character::tape) {
                tapeHp[ch]+=tapeHpAlpha*(static_cast<double>(v)-tapeHp[ch]);
                const double highPassed=static_cast<double>(v)-tapeHp[ch];
                const double bumpA1=1.0/(1.0+tapeBumpG*(tapeBumpG+0.625));
                const double bumpA2=tapeBumpG*bumpA1,bumpA3=tapeBumpG*bumpA2;
                const double bumpV3=highPassed-tapeBump[ch].ic2;
                const double bumpV1=bumpA1*tapeBump[ch].ic1+bumpA2*bumpV3;
                const double bumpV2=tapeBump[ch].ic2+bumpA2*tapeBump[ch].ic1+bumpA3*bumpV3;
                tapeBump[ch].ic1=2.0*bumpV1-tapeBump[ch].ic1;
                tapeBump[ch].ic2=2.0*bumpV2-tapeBump[ch].ic2;
                const double contoured=0.85*highPassed+(0.19+0.08*drive)*(0.625*bumpV1);
                const double gapped=svfLowpass(contoured,tapeGapG,0.70710678,tapeGapLp[ch].ic1,tapeGapLp[ch].ic2);
                const double dispersed=tapeDispCoeff*(gapped-tapeDispOut[ch])+tapeDispIn[ch];
                tapeDispIn[ch]=gapped;tapeDispOut[ch]=dispersed;
                v=static_cast<float>(dispersed);
            }
            if(s.character==Character::bbd) {
                const double sub0=0.5*(static_cast<double>(os.prevShaped)+static_cast<double>(v));
                const double a0=svfLowpass(sub0,bbdG2x,0.86,bbdLpA[ch].ic1,bbdLpA[ch].ic2);
                (void)svfLowpass(a0,bbdG2x,0.35,bbdLpB[ch].ic1,bbdLpB[ch].ic2);
                const double a1=svfLowpass(static_cast<double>(v),bbdG2x,0.86,bbdLpA[ch].ic1,bbdLpA[ch].ic2);
                const float filtered=static_cast<float>(svfLowpass(a1,bbdG2x,0.35,bbdLpB[ch].ic1,bbdLpB[ch].ic2));
                bbdDc[ch]+=bbdDcAlpha*(filtered-bbdDc[ch]);
                v=filtered-bbdDc[ch];
            }
            if(s.character==Character::loFi) {
                const float preFiltered=static_cast<float>(svfLowpass(static_cast<double>(linearDelayed),lofiG,0.70710678,lofiPreLp[ch].ic1,lofiPreLp[ch].ic2));
                if(updateHold) {
                    const float u1=0.5f*wobbleNoise(lofiCounter*2u+ch);
                    const float u2=0.5f*wobbleNoise(lofiCounter*2u+ch+7919u);
                    if(ch==1) ++lofiCounter;
                    const float tpdf=u1+u2;
                    const float ditherGate=clamp(std::abs(preFiltered)*64.f,0.f,1.f);
                    const float shapedDither=(tpdf-0.5f*lofiDitherPrev[ch])*(ditherGate/256.f);
                    lofiDitherPrev[ch]=tpdf;
                    const float quantized=std::round((preFiltered+shapedDither-0.5f*lofiQuantErr[ch])*256.f)/256.f;
                    lofiQuantErr[ch]=clamp(quantized-preFiltered,-1.f/128.f,1.f/128.f);
                    held[ch]=quantized;
                }
                v=static_cast<float>(svfLowpass(static_cast<double>(held[ch]),lofiG,0.54,lofiRecLp[ch].ic1,lofiRecLp[ch].ic2));
            }

            const float subMid=0.5f*(os.prevShaped+v);
            os.prevShaped=v;
            if(hp[ch].slope>0||hp[ch].fade>0||lp[ch].slope>0||lp[ch].fade>0) {
                (void)lp[ch].tick(hp[ch].tick(subMid));
                v=lp[ch].tick(hp[ch].tick(v));
            }
            // Bound the feedback storage without clipping the dry branch.
            float stored=freezeMix>=1?linearDelayed:softBound(v+(linearDelayed-v)*freezeMix);

            // Unitary Hadamard feedback preserves repeat energy while smearing transients.
            const float activeDiff=freezeMix>=1.f?0.f:diffusion*(1.f-freezeMix);
            std::array<double,4> delayedLines{};
            const int maxPrime=static_cast<int>(diffusionHistory[ch][0].size())-4;
            for(std::size_t stage=0;stage<diffusionStageLengths.size();++stage) {
                auto& history=diffusionHistory[ch][stage];
                int cand=std::clamp(static_cast<int>(std::lround(sampleRate*diffusionSize*0.001*diffusionRatios[ch][stage])),2,std::max(2,maxPrime));
                if(cand>2&&cand%2==0) ++cand;
                while(cand>3&&cand<maxPrime) {
                    bool composite=false;
                    for(int d=3;d*d<=cand;d+=2) {
                        if(cand%d==0) {composite=true;break;}
                    }
                    if(!composite) break;
                    cand+=2;
                }
                const auto primeDelay=static_cast<std::size_t>(std::min(cand,std::max(2,maxPrime)));
                const float raw=history[(diffusionWrite+history.size()-primeDelay)%history.size()];
                const double eta=0.32+0.16*std::sin(2.0*pi*(diffModPhase*diffusionModRates[stage]+0.25*static_cast<double>(ch)));
                const double apState=static_cast<double>(raw)+eta*diffThiranState[ch][stage];
                delayedLines[stage]=diffThiranState[ch][stage]-eta*apState;
                diffThiranState[ch][stage]=apState;
            }
            const double s01=delayedLines[0]+delayedLines[1],d01=delayedLines[0]-delayedLines[1];
            const double s23=delayedLines[2]+delayedLines[3],d23=delayedLines[2]-delayedLines[3];
            const std::array<double,4> hadamard{0.5*(s01+s23),0.5*(d01+d23),0.5*(s01-s23),0.5*(d01-d23)};
            const double theta=static_cast<double>(activeDiff)*1.18;
            const double cosT=std::cos(theta),sinT=std::sin(theta);
            const double proj=hadamardVec[0]*hadamard[0]+hadamardVec[1]*hadamard[1]+hadamardVec[2]*hadamard[2]+hadamardVec[3]*hadamard[3];
            for(std::size_t stage=0;stage<diffusionStageLengths.size();++stage) {
                diffusionHistory[ch][stage][diffusionWrite]=static_cast<float>(hadamard[stage]+((cosT-1.0)*proj+sinT*stored)*hadamardVec[stage]);
            }
            stored=static_cast<float>(-sinT*proj+cosT*stored);
            ring[ch][write]=clamp(stored,-4,4);
        }
        write=(write+1)%ring[0].size();valid=std::min(valid+1,ring[0].size());
        diffusionWrite=(diffusionWrite+1)%diffusionHistory[0][0].size();
        diffModPhase+=1.0/sampleRate;
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
            if(s.glitch==Glitch::stutter||s.glitch==Glitch::reverse) {
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
            const double offsetSamples=std::clamp(std::abs(static_cast<double>(stereoOffset))*sampleRate*.001,0.,static_cast<double>(wetHistory[0].size()-5));
            const int delayedCh=stereoOffset>0?1:0;
            const auto& hist=wetHistory[static_cast<std::size_t>(delayedCh)];
            wet[static_cast<std::size_t>(delayedCh)]=readHermite(hist,wetWrite,wetValid,1.0+offsetSamples);
        }

        // Smooth gain in dB over a 5 ms window to prevent clicks and bass ripple.
        const bool followInput=s.inputDuck||s.duckSource==DuckSource::input;
        const bool followSide=!followInput&&s.duckSource==DuckSource::sidechain;
        float detPeak=0.f,detSq=0.f;
        if(followInput) {
            detPeak=std::max(std::abs(dryL),std::abs(dryR));
            detSq=0.5f*(dryL*dryL+dryR*dryR);
        }
        if(followSide) {
            const float sl=sideLeft?finite(sideLeft[i]):0.f;
            const float srCh=sideRight?finite(sideRight[i]):sl;
            detPeak=std::max(std::abs(sl),std::abs(srCh));
            detSq=(sideLeft&&sideRight)?0.5f*(sl*sl+srCh*srCh):(sl*sl);
        }
        duckSumSq+=static_cast<double>(detSq)-static_cast<double>(duckRmsWindow[duckWinPos]);
        duckRmsWindow[duckWinPos]=detSq;
        duckWinPos=(duckWinPos+1)%duckRmsWindow.size();
        if(duckWinPos==0) {
            double recompute=0.0;
            for(float sq:duckRmsWindow) recompute+=static_cast<double>(sq);
            duckSumSq=recompute;
        }
        const float rms5ms=static_cast<float>(std::sqrt(std::max(0.0,duckSumSq/static_cast<double>(duckRmsWindow.size()))));
        const float peakCoeff=detPeak>duckPeakEnv?fastPeakRise:fastPeakFall;
        duckPeakEnv+=(detPeak-duckPeakEnv)*peakCoeff;
        lastDuck=1.f;
        if(duck>1.e-5f&&(followInput||followSide)) {
            const float hybrid=0.72f*rms5ms+0.28f*duckPeakEnv;
            const float overDb=std::max(0.f,20.f*std::log10(std::max(hybrid,1.e-5f))+38.f);
            const float targetGrDb=-std::min(40.f,duck*overDb*1.15f);
            const float attCoeff=followSide?duckAttackSide:duckAttackIn;
            const double releaseSamples=std::max(sampleRate*0.045,beatSamples*rate(s.duckRate)*(0.22+0.55*duckHold));
            const float relCoeff=static_cast<float>(1.0-std::exp(-1.0/releaseSamples));
            const bool isAttacking=targetGrDb<duckGainDb;
            if(isAttacking) {
                duckGainDb+=(targetGrDb-duckGainDb)*attCoeff;
                duckHoldCounter=static_cast<int>(sampleRate*0.008+duckHold*beatSamples*rate(s.duckRate)*0.30);
            }
            if(!isAttacking&&duckHoldCounter>0) --duckHoldCounter;
            if(!isAttacking&&duckHoldCounter<=0) duckGainDb+=(targetGrDb-duckGainDb)*relCoeff;
            lastDuck=std::pow(10.f,duckGainDb/20.f);
        }
        if(duck>1.e-5f&&!followInput&&!followSide) {
            const double duckPhase=fraction(phase/rate(s.duckRate)+1.e-9);
            const double holdFrac=duckHold*.55;
            const double shapedPhase=duckPhase<=holdFrac?0.:(duckPhase-holdFrac)/std::max(1.e-4,1.-holdFrac);
            const float u=static_cast<float>(1.-shapedPhase);
            lastDuck=1-duck*(u*u*u);
        }
        const float mid=(wet[0]+wet[1])*.5f,side=(wet[0]-wet[1])*.5f*width;
        wetL=(mid+side)*lastDuck;wetR=(mid-side)*lastDuck;
        if(s.routing==Routing::mono)wetL=wetR=mid*lastDuck;
        if(right){left[i]=(dryL*(1-mix)+wetL*mix)*output;right[i]=(dryR*(1-mix)+wetR*mix)*output;}
        if(!right)left[i]=(dryL*(1-mix)+(wetL+wetR)*.5f*mix)*output;
        phase=blockStartPhase+static_cast<double>(i+1)*increment;
        wobblePhase+=clamp(s.wobbleRate,.05f,8.f)/sampleRate;
        if(transition>0)--transition;
    }
}
}
