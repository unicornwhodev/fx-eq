#pragma once

#include <JuceHeader.h>
#include "FXAudioVisualState.h"

class MusiqueEQProcessor : public juce::AudioProcessor
{
public:
    MusiqueEQProcessor();
    ~MusiqueEQProcessor() override = default;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi()  const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return parameters; }
    const fx::AudioVisualState& getVisualState() const noexcept { return visualState; }
    double getPreparedSampleRate() const noexcept { return preparedSampleRate; }
    float getCurrentInternalTrimDb() const noexcept { return currentInternalTrimDb.load(std::memory_order_relaxed); }

private:
    static constexpr int kNumBands = 5;

    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    juce::dsp::IIR::Filter<float> leftFilters[kNumBands];
    juce::dsp::IIR::Filter<float> rightFilters[kNumBands];
    std::array<juce::SmoothedValue<float>, kNumBands> gainSmoothers;
    juce::SmoothedValue<float> qSmoother;
    std::array<float, kNumBands> targetGains {};
    float targetQ = 1.0f;
    std::array<float, kNumBands> appliedGains {};
    float appliedQ = 1.0f;
    std::atomic<float> currentInternalTrimDb { 0.0f };
    double preparedSampleRate = 44100.0;

    void updateFilterCoefficients(const std::array<float, kNumBands>& bandGains, float qValue);
    bool refreshSmoothedTargets();
    static float computeInternalTrimDb(const std::array<float, kNumBands>& bandGains, float qValue) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueEQProcessor)
};
