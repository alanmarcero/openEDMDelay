#pragma once
#include "DelayEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
namespace oed {
enum Param { gridRate, routing, character, feedback, decay, highPass, hpSlope, lowPass, lpSlope, drive, motion, duck, duckMode, duckRate, glitch, glitchRate, glitchAmount, mix, width, output, tempo, bypass, baseCount };
constexpr int parameterCount=baseCount+tapCount*4;
struct ParameterSpec { juce::String id,name; float min,max,initial,skew; juce::String choices,unit; };
const std::array<ParameterSpec,parameterCount>& parameterSpecs();
juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
using ParameterValues=std::array<std::atomic<float>*,parameterCount>;
Settings readSettings(const ParameterValues&);
const juce::StringArray& rateNames();
}
