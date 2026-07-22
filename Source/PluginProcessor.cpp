#include "PluginProcessor.h"
#if ! MUSIQUE_EQ_DSP_TESTS
#include "PluginEditor.h"
#endif

#include <complex>

namespace
{
    constexpr std::array<const char*, 5> kGainParamIds {{
        "low_gain", "low_mid_gain", "mid_gain", "high_mid_gain", "high_gain"
    }};

    constexpr std::array<const char*, 5> kFrequencyParamIds {{
        "low_freq", "low_mid_freq", "mid_freq", "high_mid_freq", "high_freq"
    }};

    constexpr std::array<float, 5> kLegacyFrequencyDefaults {{
        100.0f, 350.0f, 1200.0f, 4500.0f, 10000.0f
    }};

    static float dbToGain(float dB)
    {
        if (! std::isfinite(dB))
            return 1.0f;

        return std::pow(10.0f, dB / 20.0f);
    }

    constexpr std::array<float, 6> kIdentityBiquad {{ 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f }};

    template <typename Filter>
    void assignBiquad(Filter& filter, const std::array<float, 6>& coefficients)
    {
        *filter.coefficients = coefficients;
    }

    template <typename Filter>
    void primeBiquadStorage(Filter& filter)
    {
        assignBiquad(filter, kIdentityBiquad);
        filter.reset();
    }

    int butterworthStageCountForSlope(int slopeDbPerOct) noexcept
    {
        switch (slopeDbPerOct)
        {
            case 48: return 4;
            case 24: return 2;
            default: return 1;
        }
    }

    std::array<float, 4> butterworthQValuesForSlope(int slopeDbPerOct) noexcept
    {
        switch (slopeDbPerOct)
        {
            case 48:
                return {{ 0.5097956f, 0.6013449f, 0.8999762f, 2.5629154f }};
            case 24:
                return {{ 0.5411961f, 1.3065630f, 1.0f, 1.0f }};
            default:
                return {{ 0.7071068f, 1.0f, 1.0f, 1.0f }};
        }
    }

    double getBiquadMagnitudeForFrequency(const std::array<float, 6>& coefficients,
                                          double frequency,
                                          double sampleRate) noexcept
    {
        const double a0 = coefficients[3];
        if (! std::isfinite(a0) || std::abs(a0) <= 1.0e-12)
            return 1.0;

        const std::complex<double> z1 = std::exp(std::complex<double>(
            0.0, -juce::MathConstants<double>::twoPi * frequency / sampleRate));
        const auto z2 = z1 * z1;
        const auto numerator = static_cast<double>(coefficients[0])
            + static_cast<double>(coefficients[1]) * z1
            + static_cast<double>(coefficients[2]) * z2;
        const auto denominator = static_cast<double>(coefficients[3])
            + static_cast<double>(coefficients[4]) * z1
            + static_cast<double>(coefficients[5]) * z2;

        const auto magnitude = std::abs(numerator / denominator);
        return std::isfinite(magnitude) ? magnitude : 1.0;
    }

    void ensureParameterDefaultForLegacyState(juce::ValueTree& state, const char* paramId, const juce::var& defaultValue)
    {
        constexpr auto paramType = "PARAM";
        constexpr auto idProperty = "id";
        constexpr auto valueProperty = "value";

        for (int childIndex = 0; childIndex < state.getNumChildren(); ++childIndex)
        {
            auto child = state.getChild(childIndex);
            if (child.hasType(paramType)
                && child.getProperty(idProperty).toString() == paramId)
            {
                if (! child.hasProperty(valueProperty))
                    child.setProperty(valueProperty, defaultValue, nullptr);

                return;
            }
        }

        juce::ValueTree child(paramType);
        child.setProperty(idProperty, paramId, nullptr);
        child.setProperty(valueProperty, defaultValue, nullptr);
        state.appendChild(child, nullptr);
    }

    void ensureDefaultsForLegacyState(juce::ValueTree& state)
    {
        for (size_t i = 0; i < kFrequencyParamIds.size(); ++i)
            ensureParameterDefaultForLegacyState(state, kFrequencyParamIds[i], kLegacyFrequencyDefaults[i]);

        ensureParameterDefaultForLegacyState(state, "hpf_enabled", false);
        ensureParameterDefaultForLegacyState(state, "hpf_freq", 30.0f);
        ensureParameterDefaultForLegacyState(state, "hpf_slope", 12.0f);
        ensureParameterDefaultForLegacyState(state, "lpf_enabled", false);
        ensureParameterDefaultForLegacyState(state, "lpf_freq", 18000.0f);
        ensureParameterDefaultForLegacyState(state, "lpf_slope", 12.0f);
    }
}

MusiqueEQProcessor::MusiqueEQProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "MusiqueEQ", createParameterLayout())
{
    targetGains.fill(0.0f);
    appliedGains.fill(0.0f);
    targetFrequencies = getDefaultBandFrequencies();
    appliedFrequencies = targetFrequencies;
}

juce::AudioProcessorValueTreeState::ParameterLayout MusiqueEQProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    p.push_back(std::make_unique<juce::AudioParameterFloat>("low_gain", "Low", -24.0f, 24.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("low_mid_gain", "Low Mid", -24.0f, 24.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mid_gain", "Mid", -24.0f, 24.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("high_mid_gain", "High Mid", -24.0f, 24.0f, 0.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("high_gain", "High", -24.0f, 24.0f, 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterFloat>("q", "Q", juce::NormalisableRange<float>(0.3f, 8.0f, 0.01f, 0.35f), 1.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", 0.0f, 100.0f, 100.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("output", "Output", -24.0f, 12.0f, 0.0f));

    p.push_back(std::make_unique<juce::AudioParameterBool>("bypass", "Bypass", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>("mono", "Mono", false));

    const juce::NormalisableRange<float> frequencyRange(20.0f, 20000.0f, 0.01f, 0.25f);
    p.push_back(std::make_unique<juce::AudioParameterFloat>("low_freq", "Low Frequency", frequencyRange, 100.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("low_mid_freq", "Low Mid Frequency", frequencyRange, 350.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("mid_freq", "Mid Frequency", frequencyRange, 1200.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("high_mid_freq", "High Mid Frequency", frequencyRange, 4500.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("high_freq", "High Frequency", frequencyRange, 10000.0f));

    p.push_back(std::make_unique<juce::AudioParameterBool>("hpf_enabled", "HPF Enabled", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("hpf_freq", "HPF Frequency", frequencyRange, 30.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("hpf_slope", "HPF Slope", 12.0f, 48.0f, 12.0f));
    p.push_back(std::make_unique<juce::AudioParameterBool>("lpf_enabled", "LPF Enabled", false));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("lpf_freq", "LPF Frequency", frequencyRange, 18000.0f));
    p.push_back(std::make_unique<juce::AudioParameterFloat>("lpf_slope", "LPF Slope", 12.0f, 48.0f, 12.0f));

    return { p.begin(), p.end() };
}

void MusiqueEQProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    preparedSampleRate = sanitizeSampleRate(sampleRate);
    currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
    invalidateTrimCache();

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = preparedSampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(juce::jmax(1, samplesPerBlock));
    spec.numChannels = 1;

    for (int i = 0; i < kNumBands; ++i)
    {
        leftFilters[i].reset();
        rightFilters[i].reset();
        leftFilters[i].prepare(spec);
        rightFilters[i].prepare(spec);
        primeBiquadStorage(leftFilters[i]);
        primeBiquadStorage(rightFilters[i]);
        gainSmoothers[(size_t) i].reset(preparedSampleRate, 0.02);
        frequencySmoothers[(size_t) i].reset(preparedSampleRate, 0.02);
    }

    for (int i = 0; i < kMaxCutFilterStages; ++i)
    {
        leftHpfFilters[i].reset();
        rightHpfFilters[i].reset();
        leftLpfFilters[i].reset();
        rightLpfFilters[i].reset();
        leftHpfFilters[i].prepare(spec);
        rightHpfFilters[i].prepare(spec);
        leftLpfFilters[i].prepare(spec);
        rightLpfFilters[i].prepare(spec);
        primeBiquadStorage(leftHpfFilters[i]);
        primeBiquadStorage(rightHpfFilters[i]);
        primeBiquadStorage(leftLpfFilters[i]);
        primeBiquadStorage(rightLpfFilters[i]);
    }

    qSmoother.reset(preparedSampleRate, 0.02);
    hpfFrequencySmoother.reset(preparedSampleRate, 0.02);
    lpfFrequencySmoother.reset(preparedSampleRate, 0.02);

    for (size_t i = 0; i < kGainParamIds.size(); ++i)
        targetGains[i] = sanitizeGainDb(parameters.getRawParameterValue(kGainParamIds[i])->load());

    targetFrequencies = readBandFrequenciesFromParameters();
    targetCutFilters = readCutFilterSettingsFromParameters();
    targetQ = sanitizeQ(parameters.getRawParameterValue("q")->load());

    for (int i = 0; i < kNumBands; ++i)
    {
        gainSmoothers[(size_t) i].setCurrentAndTargetValue(targetGains[(size_t) i]);
        frequencySmoothers[(size_t) i].setCurrentAndTargetValue(targetFrequencies[(size_t) i]);
        appliedGains[(size_t) i] = targetGains[(size_t) i];
        appliedFrequencies[(size_t) i] = targetFrequencies[(size_t) i];
    }

    qSmoother.setCurrentAndTargetValue(targetQ);
    hpfFrequencySmoother.setCurrentAndTargetValue(targetCutFilters.hpfFrequency);
    lpfFrequencySmoother.setCurrentAndTargetValue(targetCutFilters.lpfFrequency);
    appliedQ = targetQ;

    appliedCutFilters = targetCutFilters;
    updateFilterCoefficients(appliedGains, appliedQ, appliedFrequencies);
    updateCutFilterCoefficients(appliedCutFilters);
}

void MusiqueEQProcessor::releaseResources()
{
    for (int i = 0; i < kNumBands; ++i)
    {
        leftFilters[i].reset();
        rightFilters[i].reset();
    }

    for (int i = 0; i < kMaxCutFilterStages; ++i)
    {
        leftHpfFilters[i].reset();
        rightHpfFilters[i].reset();
        leftLpfFilters[i].reset();
        rightLpfFilters[i].reset();
    }

    activeHpfStages = 0;
    activeLpfStages = 0;
    invalidateTrimCache();
    currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
}

bool MusiqueEQProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MusiqueEQProcessor::updateFilterCoefficients(const std::array<float, kNumBands>& bandGains,
                                                  float qValue,
                                                  const std::array<float, kNumBands>& bandFrequencies)
{
    using Coeffs = juce::dsp::IIR::ArrayCoefficients<float>;

    const double safeSampleRate = sanitizeSampleRate(preparedSampleRate);
    const auto safeGains = sanitizeBandGains(bandGains);
    const auto bandFreqs = getBandFrequencies(bandFrequencies, safeSampleRate);
    const float safeQ = sanitizeQ(qValue);

    const std::array<std::array<float, 6>, kNumBands> coefficients {{
        Coeffs::makeLowShelf (safeSampleRate, static_cast<float>(bandFreqs[0]), safeQ, dbToGain(safeGains[0])),
        Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[1]), safeQ, dbToGain(safeGains[1])),
        Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[2]), safeQ, dbToGain(safeGains[2])),
        Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[3]), safeQ, dbToGain(safeGains[3])),
        Coeffs::makeHighShelf(safeSampleRate, static_cast<float>(bandFreqs[4]), safeQ, dbToGain(safeGains[4]))
    }};

    for (int i = 0; i < kNumBands; ++i)
    {
        assignBiquad(leftFilters[i], coefficients[(size_t) i]);
        assignBiquad(rightFilters[i], coefficients[(size_t) i]);
    }

    invalidateTrimCache();
}

void MusiqueEQProcessor::updateCutFilterCoefficients(const CutFilterSettings& settings)
{
    using Coeffs = juce::dsp::IIR::ArrayCoefficients<float>;

    const double safeSampleRate = sanitizeSampleRate(preparedSampleRate);
    const auto previous = appliedCutFilters;
    const bool shouldResetHpf = settings.hpfEnabled != previous.hpfEnabled
        || (settings.hpfEnabled && settings.hpfSlopeDbPerOct != previous.hpfSlopeDbPerOct);
    const bool shouldResetLpf = settings.lpfEnabled != previous.lpfEnabled
        || (settings.lpfEnabled && settings.lpfSlopeDbPerOct != previous.lpfSlopeDbPerOct);

    const auto applyCutFilters = [safeSampleRate](auto& left,
                                                  auto& right,
                                                  bool highPass,
                                                  float frequency,
                                                  int slopeDbPerOct)
    {
        const auto qValues = butterworthQValuesForSlope(slopeDbPerOct);
        const int numStages = butterworthStageCountForSlope(slopeDbPerOct);

        for (int i = 0; i < numStages; ++i)
        {
            const auto coefficients = highPass
                ? Coeffs::makeHighPass(safeSampleRate, frequency, qValues[(size_t) i])
                : Coeffs::makeLowPass(safeSampleRate, frequency, qValues[(size_t) i]);
            assignBiquad(left[i], coefficients);
            assignBiquad(right[i], coefficients);
        }

        return numStages;
    };

    if (settings.hpfEnabled)
    {
        activeHpfStages = applyCutFilters(leftHpfFilters,
                                          rightHpfFilters,
                                          true,
                                          sanitizeCutFrequency(settings.hpfFrequency, safeSampleRate, 30.0f),
                                          settings.hpfSlopeDbPerOct);
    }
    else
    {
        activeHpfStages = 0;
    }

    if (settings.lpfEnabled)
    {
        activeLpfStages = applyCutFilters(leftLpfFilters,
                                          rightLpfFilters,
                                          false,
                                          sanitizeCutFrequency(settings.lpfFrequency, safeSampleRate, 18000.0f),
                                          settings.lpfSlopeDbPerOct);
    }
    else
    {
        activeLpfStages = 0;
    }

    if (shouldResetHpf)
    {
        for (int i = 0; i < kMaxCutFilterStages; ++i)
        {
            leftHpfFilters[i].reset();
            rightHpfFilters[i].reset();
        }
    }

    if (shouldResetLpf)
    {
        for (int i = 0; i < kMaxCutFilterStages; ++i)
        {
            leftLpfFilters[i].reset();
            rightLpfFilters[i].reset();
        }
    }

    appliedCutFilters = settings;
}

MusiqueEQProcessor::TargetChangeFlags MusiqueEQProcessor::refreshSmoothedTargets()
{
    TargetChangeFlags changes {};
    std::array<float, kNumBands> newTargets {};
    for (size_t i = 0; i < kGainParamIds.size(); ++i)
        newTargets[i] = sanitizeGainDb(parameters.getRawParameterValue(kGainParamIds[i])->load());

    const auto newFrequencies = readBandFrequenciesFromParameters();
    const auto newCutFilters = readCutFilterSettingsFromParameters();
    const float newQ = sanitizeQ(parameters.getRawParameterValue("q")->load());

    for (int i = 0; i < kNumBands; ++i)
    {
        if (! juce::approximatelyEqual(targetGains[(size_t) i], newTargets[(size_t) i]))
        {
            targetGains[(size_t) i] = newTargets[(size_t) i];
            gainSmoothers[(size_t) i].setTargetValue(newTargets[(size_t) i]);
            changes.eqChanged = true;
        }

        if (! juce::approximatelyEqual(targetFrequencies[(size_t) i], newFrequencies[(size_t) i]))
        {
            targetFrequencies[(size_t) i] = newFrequencies[(size_t) i];
            frequencySmoothers[(size_t) i].setTargetValue(newFrequencies[(size_t) i]);
            changes.eqChanged = true;
        }
    }

    if (! juce::approximatelyEqual(targetQ, newQ))
    {
        targetQ = newQ;
        qSmoother.setTargetValue(newQ);
        changes.eqChanged = true;
    }

    if (! cutFilterSettingsEqual(targetCutFilters, newCutFilters))
    {
        if (! juce::approximatelyEqual(targetCutFilters.hpfFrequency, newCutFilters.hpfFrequency))
            hpfFrequencySmoother.setTargetValue(newCutFilters.hpfFrequency);

        if (! juce::approximatelyEqual(targetCutFilters.lpfFrequency, newCutFilters.lpfFrequency))
            lpfFrequencySmoother.setTargetValue(newCutFilters.lpfFrequency);

        targetCutFilters = newCutFilters;
        changes.cutChanged = true;
    }

    return changes;
}

std::array<float, MusiqueEQProcessor::kNumBands> MusiqueEQProcessor::readBandFrequenciesFromParameters() const noexcept
{
    std::array<float, kNumBands> frequencies = getDefaultBandFrequencies();

    for (size_t i = 0; i < kFrequencyParamIds.size(); ++i)
    {
        if (auto* raw = parameters.getRawParameterValue(kFrequencyParamIds[i]))
            frequencies[i] = raw->load();
    }

    return sanitizeBandFrequencies(frequencies, preparedSampleRate);
}

MusiqueEQProcessor::CutFilterSettings MusiqueEQProcessor::readCutFilterSettingsFromParameters() const noexcept
{
    CutFilterSettings settings {};

    if (auto* raw = parameters.getRawParameterValue("hpf_enabled"))
        settings.hpfEnabled = raw->load() > 0.5f;
    if (auto* raw = parameters.getRawParameterValue("hpf_freq"))
        settings.hpfFrequency = sanitizeCutFrequency(raw->load(), preparedSampleRate, 30.0f);
    if (auto* raw = parameters.getRawParameterValue("hpf_slope"))
        settings.hpfSlopeDbPerOct = sanitizeSlopeDbPerOct(raw->load());

    if (auto* raw = parameters.getRawParameterValue("lpf_enabled"))
        settings.lpfEnabled = raw->load() > 0.5f;
    if (auto* raw = parameters.getRawParameterValue("lpf_freq"))
        settings.lpfFrequency = sanitizeCutFrequency(raw->load(), preparedSampleRate, 18000.0f);
    if (auto* raw = parameters.getRawParameterValue("lpf_slope"))
        settings.lpfSlopeDbPerOct = sanitizeSlopeDbPerOct(raw->load());

    return settings;
}

double MusiqueEQProcessor::sanitizeSampleRate(double sampleRate) noexcept
{
    if (! std::isfinite(sampleRate))
        return 44100.0;

    return juce::jlimit(12000.0, 384000.0, sampleRate);
}

float MusiqueEQProcessor::sanitizeGainDb(float gainDb) noexcept
{
    if (! std::isfinite(gainDb))
        return 0.0f;

    return juce::jlimit(-24.0f, 24.0f, gainDb);
}

float MusiqueEQProcessor::sanitizeQ(float qValue) noexcept
{
    if (! std::isfinite(qValue))
        return 1.0f;

    return juce::jlimit(0.3f, 8.0f, qValue);
}

float MusiqueEQProcessor::sanitizeFrequency(float frequency, double sampleRate, float fallback) noexcept
{
    const double safeSampleRate = sanitizeSampleRate(sampleRate);
    const float maxFrequency = static_cast<float>(juce::jmax(20.0, safeSampleRate * 0.45));
    const float safeFallback = std::isfinite(fallback) ? juce::jlimit(20.0f, maxFrequency, fallback) : 1000.0f;

    if (! std::isfinite(frequency))
        return safeFallback;

    return juce::jlimit(20.0f, maxFrequency, frequency);
}

float MusiqueEQProcessor::sanitizeCutFrequency(float frequency, double sampleRate, float fallback) noexcept
{
    return sanitizeFrequency(frequency, sampleRate, fallback);
}

int MusiqueEQProcessor::sanitizeSlopeDbPerOct(float slopeDbPerOct) noexcept
{
    if (! std::isfinite(slopeDbPerOct))
        return 12;

    if (slopeDbPerOct < 18.0f)
        return 12;

    if (slopeDbPerOct < 36.0f)
        return 24;

    return 48;
}

bool MusiqueEQProcessor::cutFilterSettingsEqual(const CutFilterSettings& a, const CutFilterSettings& b) noexcept
{
    return a.hpfEnabled == b.hpfEnabled
        && juce::approximatelyEqual(a.hpfFrequency, b.hpfFrequency)
        && a.hpfSlopeDbPerOct == b.hpfSlopeDbPerOct
        && a.lpfEnabled == b.lpfEnabled
        && juce::approximatelyEqual(a.lpfFrequency, b.lpfFrequency)
        && a.lpfSlopeDbPerOct == b.lpfSlopeDbPerOct;
}

bool MusiqueEQProcessor::bandFloatArraysEqual(const std::array<float, kNumBands>& a,
                                              const std::array<float, kNumBands>& b) noexcept
{
    return std::equal(a.begin(), a.end(), b.begin(), [](float left, float right)
    {
        return juce::approximatelyEqual(left, right);
    });
}

std::array<float, MusiqueEQProcessor::kNumBands> MusiqueEQProcessor::sanitizeBandGains(const std::array<float, kNumBands>& bandGains) noexcept
{
    std::array<float, kNumBands> out {};
    for (int i = 0; i < kNumBands; ++i)
        out[(size_t) i] = sanitizeGainDb(bandGains[(size_t) i]);

    return out;
}

std::array<float, MusiqueEQProcessor::kNumBands> MusiqueEQProcessor::getDefaultBandFrequencies() noexcept
{
    return {
        kLegacyFrequencyDefaults[0],
        kLegacyFrequencyDefaults[1],
        kLegacyFrequencyDefaults[2],
        kLegacyFrequencyDefaults[3],
        kLegacyFrequencyDefaults[4]
    };
}

std::array<float, MusiqueEQProcessor::kNumBands> MusiqueEQProcessor::sanitizeBandFrequencies(
    const std::array<float, kNumBands>& bandFrequencies,
    double sampleRate) noexcept
{
    const auto defaults = getDefaultBandFrequencies();
    std::array<float, kNumBands> out {};

    for (int i = 0; i < kNumBands; ++i)
        out[(size_t) i] = sanitizeFrequency(bandFrequencies[(size_t) i], sampleRate, defaults[(size_t) i]);

    return out;
}

std::array<double, MusiqueEQProcessor::kNumBands> MusiqueEQProcessor::getBandFrequencies(
    const std::array<float, kNumBands>& bandFrequencies,
    double sampleRate) noexcept
{
    const auto safeFrequencies = sanitizeBandFrequencies(bandFrequencies, sampleRate);

    return {
        static_cast<double>(safeFrequencies[0]),
        static_cast<double>(safeFrequencies[1]),
        static_cast<double>(safeFrequencies[2]),
        static_cast<double>(safeFrequencies[3]),
        static_cast<double>(safeFrequencies[4])
    };
}

void MusiqueEQProcessor::invalidateTrimCache() noexcept
{
    trimCache.valid = false;
}

float MusiqueEQProcessor::getCachedInternalTrimDb() noexcept
{
    const double safeSampleRate = sanitizeSampleRate(preparedSampleRate);

    if (! trimCache.valid
        || ! bandFloatArraysEqual(trimCache.bandGains, appliedGains)
        || ! bandFloatArraysEqual(trimCache.bandFrequencies, appliedFrequencies)
        || ! juce::approximatelyEqual(trimCache.qValue, appliedQ)
        || ! juce::approximatelyEqual(trimCache.sampleRate, safeSampleRate))
    {
        trimCache.bandGains = appliedGains;
        trimCache.bandFrequencies = appliedFrequencies;
        trimCache.qValue = appliedQ;
        trimCache.sampleRate = safeSampleRate;
        trimCache.trimDb = computeInternalTrimDb(appliedGains, appliedQ, appliedFrequencies, safeSampleRate);
        trimCache.valid = true;
    }

    return trimCache.trimDb;
}

float MusiqueEQProcessor::computeInternalTrimDb(const std::array<float, kNumBands>& bandGains,
                                                float qValue,
                                                const std::array<float, kNumBands>& bandFrequencies,
                                                double sampleRate) noexcept
{
    using Coeffs = juce::dsp::IIR::ArrayCoefficients<float>;

    const double safeSampleRate = sanitizeSampleRate(sampleRate);
    const auto safeGains = sanitizeBandGains(bandGains);
    const auto bandFreqs = getBandFrequencies(bandFrequencies, safeSampleRate);
    const float safeQ = sanitizeQ(qValue);

    const std::array<std::array<float, 6>, kNumBands> coefficients {{
        Coeffs::makeLowShelf (safeSampleRate, static_cast<float>(bandFreqs[0]), safeQ, dbToGain(safeGains[0])),
        Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[1]), safeQ, dbToGain(safeGains[1])),
        Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[2]), safeQ, dbToGain(safeGains[2])),
        Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[3]), safeQ, dbToGain(safeGains[3])),
        Coeffs::makeHighShelf(safeSampleRate, static_cast<float>(bandFreqs[4]), safeQ, dbToGain(safeGains[4]))
    }};

    constexpr int numProbePoints = 96;
    constexpr double minFrequency = 20.0;
    const double maxProbeFrequency = juce::jlimit(minFrequency, 20000.0, safeSampleRate * 0.45);
    const double logMin = std::log(minFrequency);
    const double logMax = std::log(maxProbeFrequency);

    double peakMagnitude = 1.0;
    for (int i = 0; i < numProbePoints; ++i)
    {
        const double normalised = numProbePoints == 1 ? 0.0 : static_cast<double>(i) / static_cast<double>(numProbePoints - 1);
        const double frequency = std::exp(logMin + (logMax - logMin) * normalised);
        double magnitude = 1.0;
        for (const auto& biquad : coefficients)
            magnitude *= getBiquadMagnitudeForFrequency(biquad, frequency, safeSampleRate);

        if (std::isfinite(magnitude))
            peakMagnitude = juce::jmax(peakMagnitude, magnitude);
    }

    const float peakDb = juce::Decibels::gainToDecibels(static_cast<float>(peakMagnitude), -120.0f);
    constexpr float trimThresholdDb = 9.0f;
    return juce::jlimit(0.0f, 12.0f, peakDb - trimThresholdDb);
}

void MusiqueEQProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(buffer);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const bool bypass = (*parameters.getRawParameterValue("bypass") > 0.5f);
    if (bypass)
    {
        currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
        visualState.captureOutput(buffer);
        return;
    }

    const bool mono   = (*parameters.getRawParameterValue("mono") > 0.5f);
    const float mix   = juce::jlimit(0.0f, 1.0f, parameters.getRawParameterValue("mix")->load() / 100.0f);
    const float out   = dbToGain(sanitizeGainDb(parameters.getRawParameterValue("output")->load()));

    if (mono)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float m = 0.5f * (buffer.getSample(0, n) + buffer.getSample(1, n));
            buffer.setSample(0, n, m);
            buffer.setSample(1, n, m);
        }
    }

    const auto targetChanges = refreshSmoothedTargets();
    const bool eqSmoothingActive = targetChanges.eqChanged
        || qSmoother.isSmoothing()
        || std::any_of(gainSmoothers.begin(), gainSmoothers.end(), [](auto& smoother) { return smoother.isSmoothing(); })
        || std::any_of(frequencySmoothers.begin(), frequencySmoothers.end(), [](auto& smoother) { return smoother.isSmoothing(); });
    const bool cutSmoothingActive = targetChanges.cutChanged
        || hpfFrequencySmoother.isSmoothing()
        || lpfFrequencySmoother.isSmoothing();

    if (mix <= 0.0001f)
    {
        currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
        buffer.applyGain(out);
        visualState.captureOutput(buffer);
        return;
    }

    const auto updateCutFiltersIfNeeded = [this](const CutFilterSettings& settings)
    {
        if (! cutFilterSettingsEqual(appliedCutFilters, settings))
            updateCutFilterCoefficients(settings);
    };

    const auto nextCutSettings = [this]()
    {
        auto settings = targetCutFilters;
        settings.hpfFrequency = hpfFrequencySmoother.isSmoothing()
            ? hpfFrequencySmoother.getNextValue()
            : targetCutFilters.hpfFrequency;
        settings.lpfFrequency = lpfFrequencySmoother.isSmoothing()
            ? lpfFrequencySmoother.getNextValue()
            : targetCutFilters.lpfFrequency;
        return settings;
    };

    if (! eqSmoothingActive && ! cutSmoothingActive)
    {
        if (! juce::approximatelyEqual(appliedQ, targetQ)
            || ! bandFloatArraysEqual(appliedGains, targetGains)
            || ! bandFloatArraysEqual(appliedFrequencies, targetFrequencies))
        {
            appliedQ = targetQ;
            appliedGains = targetGains;
            appliedFrequencies = targetFrequencies;
            updateFilterCoefficients(appliedGains, appliedQ, appliedFrequencies);
        }

        updateCutFiltersIfNeeded(targetCutFilters);

        const float internalTrimDb = getCachedInternalTrimDb();
        const float internalTrimGain = dbToGain(-internalTrimDb);
        currentInternalTrimDb.store(internalTrimDb, std::memory_order_relaxed);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = buffer.getSample(0, n);
            const float dryR = buffer.getSample(1, n);
            float wetL = dryL;
            float wetR = dryR;

            for (int i = 0; i < activeHpfStages; ++i)
            {
                wetL = leftHpfFilters[i].processSample(wetL);
                wetR = rightHpfFilters[i].processSample(wetR);
            }

            wetL *= internalTrimGain;
            wetR *= internalTrimGain;

            for (int i = 0; i < kNumBands; ++i)
            {
                wetL = leftFilters[i].processSample(wetL);
                wetR = rightFilters[i].processSample(wetR);
            }

            for (int i = 0; i < activeLpfStages; ++i)
            {
                wetL = leftLpfFilters[i].processSample(wetL);
                wetR = rightLpfFilters[i].processSample(wetR);
            }

            buffer.setSample(0, n, (dryL * (1.0f - mix) + wetL * mix) * out);
            buffer.setSample(1, n, (dryR * (1.0f - mix) + wetR * mix) * out);
        }
    }
    else
    {
        constexpr int smoothingChunkSize = 16;
        int sampleIndex = 0;
        while (sampleIndex < numSamples)
        {
            const int chunkEnd = juce::jmin(numSamples, sampleIndex + smoothingChunkSize);
            if (eqSmoothingActive)
            {
                for (int i = 0; i < kNumBands; ++i)
                {
                    appliedGains[(size_t) i] = gainSmoothers[(size_t) i].getNextValue();
                    appliedFrequencies[(size_t) i] = frequencySmoothers[(size_t) i].getNextValue();
                }

                appliedQ = qSmoother.getNextValue();
                updateFilterCoefficients(appliedGains, appliedQ, appliedFrequencies);
            }

            if (cutSmoothingActive)
                updateCutFiltersIfNeeded(nextCutSettings());

            const float internalTrimDb = getCachedInternalTrimDb();
            const float internalTrimGain = dbToGain(-internalTrimDb);
            currentInternalTrimDb.store(internalTrimDb, std::memory_order_relaxed);

            for (int n = sampleIndex; n < chunkEnd; ++n)
            {
                const float dryL = buffer.getSample(0, n);
                const float dryR = buffer.getSample(1, n);
                float wetL = dryL;
                float wetR = dryR;

                for (int i = 0; i < activeHpfStages; ++i)
                {
                    wetL = leftHpfFilters[i].processSample(wetL);
                    wetR = rightHpfFilters[i].processSample(wetR);
                }

                wetL *= internalTrimGain;
                wetR *= internalTrimGain;

                for (int i = 0; i < kNumBands; ++i)
                {
                    wetL = leftFilters[i].processSample(wetL);
                    wetR = rightFilters[i].processSample(wetR);
                }

                for (int i = 0; i < activeLpfStages; ++i)
                {
                    wetL = leftLpfFilters[i].processSample(wetL);
                    wetR = rightLpfFilters[i].processSample(wetR);
                }

                buffer.setSample(0, n, (dryL * (1.0f - mix) + wetL * mix) * out);
                buffer.setSample(1, n, (dryR * (1.0f - mix) + wetR * mix) * out);
            }

            sampleIndex = chunkEnd;
        }
    }

    visualState.captureOutput(buffer);
}

void MusiqueEQProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void MusiqueEQProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
    {
        auto restoredState = juce::ValueTree::fromXml(*xml);
        ensureDefaultsForLegacyState(restoredState);
        parameters.replaceState(restoredState);
    }
}

#if MUSIQUE_EQ_DSP_TESTS
namespace
{
    MusiqueEQProcessor::TestBiquadCoefficients snapshotBiquadForTests(
        const juce::dsp::IIR::Filter<float>& filter)
    {
        MusiqueEQProcessor::TestBiquadCoefficients out {{ 1.0f, 0.0f, 0.0f, 0.0f, 0.0f }};

        if (filter.coefficients == nullptr)
            return out;

        const auto* raw = filter.coefficients->getRawCoefficients();
        if (raw == nullptr)
            return out;

        const auto order = filter.coefficients->getFilterOrder();
        const auto count = juce::jlimit<size_t>(0, out.size(), static_cast<size_t>(order * 2 + 1));
        for (size_t i = 0; i < count; ++i)
            out[i] = raw[i];

        return out;
    }
}

MusiqueEQProcessor::TestCoefficientSnapshots MusiqueEQProcessor::getTestCoefficientSnapshots() const
{
    TestCoefficientSnapshots snapshots {};

    for (int i = 0; i < kNumBands; ++i)
        snapshots.eq[(size_t) i] = snapshotBiquadForTests(leftFilters[i]);

    for (int i = 0; i < kMaxCutFilterStages; ++i)
    {
        snapshots.hpf[(size_t) i] = snapshotBiquadForTests(leftHpfFilters[i]);
        snapshots.lpf[(size_t) i] = snapshotBiquadForTests(leftLpfFilters[i]);
    }

    snapshots.activeHpfStages = activeHpfStages;
    snapshots.activeLpfStages = activeLpfStages;
    return snapshots;
}
#endif

juce::AudioProcessorEditor* MusiqueEQProcessor::createEditor()
{
#if MUSIQUE_EQ_DSP_TESTS
    return nullptr;
#else
    return new MusiqueEQEditor(*this);
#endif
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusiqueEQProcessor();
}
