#pragma once
#include <array>
#include <vector>
namespace oed {
constexpr int tapCount = 8;
constexpr std::array<double, 10> beatRates{.125, 1.0/6, .25, 1.0/3, .5, .75, 2.0/3, 1, 1.5, 2};
enum class Routing { stereo, pingPong, mono };
enum class Character { digital, tape, bbd, loFi };
enum class Glitch { off, gate, stutter, reverse };
struct Tap { bool enabled = false; int step = 1; float level = 1; float pan = 0; };
struct Settings {
    std::array<Tap, tapCount> taps{{{true, 1, 1, 0}}};
    int rate = 7;
    Routing routing = Routing::stereo;
    Character character = Character::digital;
    float feedback = .35f, decay = .3f, highPass = 120, lowPass = 9000;
    int hpSlope = 2, lpSlope = 2;
    float drive = .25f, motion = .2f, duck = 0;
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
    void process(float* left, float* right, int frames, const Settings&, const Transport&);
    float duckGain() const { return lastDuck; }
private:
    struct Filter {
        struct Stage { double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0; };
        std::array<Stage,4> stages{}, oldStages{};
        double smoothing=1, sr=48000, logHz=0, targetLogHz=0;
        int slope=0, oldSlope=0, fade=0, fadeLength=1;
        bool primed=false, isHigh=false;
        void coefficients();
        void configure(double sampleRate, float hz, int slope, bool high);
        float tick(float value);
        void clear();
    };
    float read(const std::vector<float>&, double delay) const;
    double sampleRate=48000, phase=0, previousLoop=0, tapeLoop=0, oldLoop=0;
    std::array<std::vector<float>,2> ring, wetHistory;
    std::array<Filter,2> hp,lp;
    std::array<float,2> tone{}, held{};
    std::array<double,tapCount> delay{}, oldDelay{}, tapeDelay{};
    std::array<float,tapCount> gain{},pan{};
    std::size_t write=0, wetWrite=0, capture=0, valid=0, wetValid=0;
    int transition=0, holdCounter=0, sliceSize=1;
    double lastSlice=-1;
    float feedback=0,mix=0,width=1,output=1,duck=0,glitchMix=0,drive=0,motion=0;
    float envelope=0,lastDuck=1;
    bool initialized=false;
};
}
