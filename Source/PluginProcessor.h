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

    const juce::String getName() const override
    {
#if MUSIQUE_EQ_DSP_TESTS
        return "Musique EQ and Filter";
#else
        return JucePlugin_Name;
#endif
    }
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

#if MUSIQUE_EQ_DSP_TESTS
    using TestBiquadCoefficients = std::array<float, 5>;
    struct TestCoefficientSnapshots
    {
        std::array<TestBiquadCoefficients, 5> eq {};
        std::array<TestBiquadCoefficients, 4> hpf {};
        std::array<TestBiquadCoefficients, 4> lpf {};
        int activeHpfStages = 0;
        int activeLpfStages = 0;
    };

    TestCoefficientSnapshots getTestCoefficientSnapshots() const;
#endif

private:
    enum class ModelBand
    {
        hpf,
        low,
        lowMid,
        mid,
        highMid,
        high,
        lpf
    };

    enum class BandShape
    {
        inactive,
        highPass,
        lowShelf,
        bell,
        highShelf,
        lowPass
    };

    struct BandModel
    {
        ModelBand band;
        BandShape shape;
        bool active;
    };

    static constexpr int kNumModelBands = 7;
    static constexpr int kNumBands = 5;
    static constexpr int kMaxCutFilterStages = 4;
    static constexpr std::array<BandModel, kNumModelBands> kBandModel {{
        { ModelBand::hpf,     BandShape::highPass,  true  },
        { ModelBand::low,     BandShape::lowShelf,  true  },
        { ModelBand::lowMid,  BandShape::bell,      true  },
        { ModelBand::mid,     BandShape::bell,      true  },
        { ModelBand::highMid, BandShape::bell,      true  },
        { ModelBand::high,    BandShape::highShelf, true  },
        { ModelBand::lpf,     BandShape::lowPass,   true  }
    }};

    struct CutFilterSettings
    {
        bool hpfEnabled = false;
        float hpfFrequency = 30.0f;
        int hpfSlopeDbPerOct = 12;
        bool lpfEnabled = false;
        float lpfFrequency = 18000.0f;
        int lpfSlopeDbPerOct = 12;
    };

    struct TargetChangeFlags
    {
        bool eqChanged = false;
        bool cutChanged = false;
    };

    struct TrimCache
    {
        std::array<float, kNumBands> bandGains {};
        std::array<float, kNumBands> bandFrequencies {};
        float qValue = 1.0f;
        double sampleRate = 44100.0;
        float trimDb = 0.0f;
        bool valid = false;
    };

    juce::AudioProcessorValueTreeState parameters;
    fx::AudioVisualState visualState;
    juce::dsp::IIR::Filter<float> leftFilters[kNumBands];
    juce::dsp::IIR::Filter<float> rightFilters[kNumBands];
    juce::dsp::IIR::Filter<float> leftHpfFilters[kMaxCutFilterStages];
    juce::dsp::IIR::Filter<float> rightHpfFilters[kMaxCutFilterStages];
    juce::dsp::IIR::Filter<float> leftLpfFilters[kMaxCutFilterStages];
    juce::dsp::IIR::Filter<float> rightLpfFilters[kMaxCutFilterStages];
    std::array<juce::SmoothedValue<float>, kNumBands> gainSmoothers;
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, kNumBands> frequencySmoothers;
    juce::SmoothedValue<float> qSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> hpfFrequencySmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> lpfFrequencySmoother;
    std::array<float, kNumBands> targetGains {};
    std::array<float, kNumBands> targetFrequencies {};
    CutFilterSettings targetCutFilters {};
    float targetQ = 1.0f;
    std::array<float, kNumBands> appliedGains {};
    std::array<float, kNumBands> appliedFrequencies {};
    CutFilterSettings appliedCutFilters {};
    float appliedQ = 1.0f;
    int activeHpfStages = 0;
    int activeLpfStages = 0;
    std::atomic<float> currentInternalTrimDb { 0.0f };
    double preparedSampleRate = 44100.0;
    TrimCache trimCache {};

    void updateFilterCoefficients(const std::array<float, kNumBands>& bandGains,
                                  float qValue,
                                  const std::array<float, kNumBands>& bandFrequencies);
    void updateCutFilterCoefficients(const CutFilterSettings& settings);
    TargetChangeFlags refreshSmoothedTargets();
    void invalidateTrimCache() noexcept;
    float getCachedInternalTrimDb() noexcept;
    std::array<float, kNumBands> readBandFrequenciesFromParameters() const noexcept;
    CutFilterSettings readCutFilterSettingsFromParameters() const noexcept;
    static double sanitizeSampleRate(double sampleRate) noexcept;
    static float sanitizeGainDb(float gainDb) noexcept;
    static float sanitizeQ(float qValue) noexcept;
    static float sanitizeFrequency(float frequency, double sampleRate, float fallback) noexcept;
    static float sanitizeCutFrequency(float frequency, double sampleRate, float fallback) noexcept;
    static int sanitizeSlopeDbPerOct(float slopeDbPerOct) noexcept;
    static bool cutFilterSettingsEqual(const CutFilterSettings& a, const CutFilterSettings& b) noexcept;
    static bool bandFloatArraysEqual(const std::array<float, kNumBands>& a,
                                     const std::array<float, kNumBands>& b) noexcept;
    static std::array<float, kNumBands> sanitizeBandGains(const std::array<float, kNumBands>& bandGains) noexcept;
    static std::array<float, kNumBands> getDefaultBandFrequencies() noexcept;
    static std::array<float, kNumBands> sanitizeBandFrequencies(const std::array<float, kNumBands>& bandFrequencies,
                                                                double sampleRate) noexcept;
    static std::array<double, kNumBands> getBandFrequencies(const std::array<float, kNumBands>& bandFrequencies,
                                                            double sampleRate) noexcept;
    static float computeInternalTrimDb(const std::array<float, kNumBands>& bandGains,
                                       float qValue,
                                       const std::array<float, kNumBands>& bandFrequencies,
                                       double sampleRate) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueEQProcessor)
};
