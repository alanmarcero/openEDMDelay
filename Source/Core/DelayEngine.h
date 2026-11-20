#pragma once
#include <array>
#include <cstdint>
#include <vector>
namespace oed {
constexpr int tapCount = 8;
constexpr std::array<double, 16> beatRates{
    .125, 1.0/6, .25, 1.0/3, .5, .75, 2.0/3, 1, 1.5, 2,
    .1875, .375, 4.0/3, 3, 4, 6
};
constexpr float diffusionStageFeedback=.62f;
constexpr std::array<double,4> diffusionStageLengths{.61,.89,1.17,1.53};
enum class Routing { stereo, pingPong, mono };
enum class Character { digital, tape, bbd, loFi };
enum class Glitch { off, gate, stutter, reverse };
enum class DuckSource { beat, input, sidechain };
enum class WobbleShape { sine, triangle, square, randomWalk, sampleHold };
struct Tap { bool enabled = false; int step = 1; float level = 1; float pan = 0; float probability = 1; int ratchets = 1; bool filter = false;
    float highPass = 20, lowPass = 20000; int character = 0; float drive = .25f; int morphStep = 1; };
struct Settings {
    std::array<Tap, tapCount> taps{{{true, 1, 1, 0}}};
    int rate = 7;
    float swing = 0, morph = 0; int groove = 0; bool freeze = false;
    Routing routing = Routing::stereo;
    Character character = Character::digital;
    float feedback = .35f, decay = .3f, highPass = 120, lowPass = 9000;
    int hpSlope = 2, lpSlope = 2;
    float drive = .25f, motion = .2f, wobbleRate = .5f, diffusion = 0, diffusionSize = 12, duck = 0;
    float shimmer = 0, stereoOffset = 0, duckHold = 0, reverse = 0;
    int reverseRate = 7;
    WobbleShape wobbleShape = WobbleShape::sine;
    DuckSource duckSource = DuckSource::beat;
    bool inputDuck = false;
    int duckRate = 7, glitchRate = 2;
    Glitch glitch = Glitch::off;
    float glitchAmount = .5f, mix = .3f, width = 1, outputDb = 0;
};
struct Transport { double bpm = 120; double ppq = 0; bool hasPpq = false; };
class DelayEngine {
public:
    void prepare(double sampleRate);
    void reset();
    void process(float* left, float* right, int frames, const Settings&, const Transport&, const float* sideLeft = nullptr, const float* sideRight = nullptr);
    float duckGain() const { return lastDuck; }
private:
    struct Filter {
        struct Stage { double g=0,r=1,a1=0,a2=0,a3=0,ic1=0,ic2=0; };
        std::array<Stage,4> stages{}, oldStages{};
        double smoothing=1, sr=48000, logHz=0, targetLogHz=0;
        int slope=0, oldSlope=0, fade=0, fadeLength=1;
        bool primed=false, isHigh=false;
        void coefficients();
        void configure(double sampleRate, float hz, int slope, bool high);
        float tick(float value);
        void clear();
    };
    struct Svf2 { double ic1=0, ic2=0; };
    struct Oversampler2x {
        std::array<float,12> inHist{}, resEven{};
        std::array<float,7> resOdd{};
        std::size_t pos=0, oddPos=0;
        float prevShaped=0;
    };
    float readHermite(const std::vector<float>&, std::size_t head, std::size_t validCount, double delay) const;
    double sampleRate=48000, phase=0, previousLoop=0, tapeLoop=0, oldLoop=0;
    std::array<std::vector<float>,2> ring, revRing, wetHistory;
    std::array<std::array<std::vector<float>,diffusionStageLengths.size()>,2> diffusionHistory;
    std::array<std::array<double,diffusionStageLengths.size()>,2> diffThiranState{};
    std::array<Filter,2> hp,lp;
    std::array<std::array<Filter,2>,tapCount> tapHp,tapLp,oldTapHp,oldTapLp;
    std::array<std::array<float,2>,tapCount> tapTone{},oldTapTone{};
    std::array<Oversampler2x,2> oversampler{};
    std::array<Svf2,2> shimmerLpA{}, shimmerLpB{}, tapeBump{}, tapeGapLp{}, bbdLpA{}, bbdLpB{}, lofiPreLp{}, lofiRecLp{};
    std::array<double,2> tapeMag{}, tapePrevH{}, tapeHp{}, tapeDispIn{}, tapeDispOut{}, shimmerOffset{};
    std::array<float,2> bbdPreLp{}, bbdCompRms{}, bbdDc{}, bbdExpRms{}, bbdDeLp{};
    std::array<float,2> lofiDitherPrev{}, lofiQuantErr{}, held{};
    std::array<std::int64_t,4> revGrainId{-1,-1,-1,-1};
    std::array<std::array<std::array<double,4>,2>,2> revSpliceOffset{};
    std::vector<float> duckRmsWindow;
    std::size_t duckWinPos=0;
    double duckSumSq=0, ouState=0, ouSmooth=0, diffModPhase=0, lofiPhase=0;
    std::uint64_t ouCounter=0, lofiCounter=0;
    float freezeMix=0, diffusion=0, diffusionSize=12, shimmer=0, stereoOffset=0, duckHold=0, reverseMix=0, wobbleValue=0;
    double frozenLoop=1,wobblePhase=0,shimmerPhase=0,reverseChunk=1;bool wasFrozen=false;
    std::array<double,tapCount> delay{}, oldDelay{}, tapeDelay{};
    std::array<float,tapCount> gain{},pan{};
    std::size_t write=0, revWrite=0, wetWrite=0, diffusionWrite=0, capture=0, valid=0, revValid=0, wetValid=0;
    int transition=0, sliceSize=1, duckHoldCounter=0;
    double lastSlice=-1;
    float feedback=0,mix=0,width=1,output=1,duck=0,glitchMix=0,drive=0,motion=0;
    float duckPeakEnv=0,duckGainDb=0,lastDuck=1;
    bool initialized=false;
};
}
