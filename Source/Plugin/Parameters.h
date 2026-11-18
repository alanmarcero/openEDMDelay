#pragma once
#include "DelayEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
namespace oed {
enum Param { gridRate, routing, character, feedback, decay, highPass, hpSlope, lowPass, lpSlope, drive, motion, duck, duckMode, duckRate, glitch, glitchRate, glitchAmount, mix, width, output, tempo, bypass, baseCount };
enum PerformanceParam { swing=baseCount+tapCount*4, groove, freeze, patternMorph, tapExtraStart };
constexpr int tapExtraCount=8;
constexpr int styleParamStart=tapExtraStart+tapCount*tapExtraCount;
enum StyleParam { diffusion=styleParamStart, diffusionSize, wobbleRate, wobbleShape, shimmer, stereoOffset, duckHold, parameterCount };
struct ParameterSpec { juce::String id,name; float min,max,initial,skew; juce::String choices,unit; };
const std::array<ParameterSpec,parameterCount>& parameterSpecs();
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
using ParameterValues=std::array<std::atomic<float>*,parameterCount>;
Settings readSettings(const ParameterValues&);
const juce::StringArray& rateNames();
}
