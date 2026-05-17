#include "PluginProcessor.h"
#if ! MUSIQUE_EQ_DSP_TESTS
#include "PluginEditor.h"
#endif

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
        gainSmoothers[(size_t) i].reset(preparedSampleRate, 0.02);
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
    }

    qSmoother.reset(preparedSampleRate, 0.02);

    for (size_t i = 0; i < kGainParamIds.size(); ++i)
        targetGains[i] = sanitizeGainDb(parameters.getRawParameterValue(kGainParamIds[i])->load());

    targetFrequencies = readBandFrequenciesFromParameters();
    targetCutFilters = readCutFilterSettingsFromParameters();
    targetQ = sanitizeQ(parameters.getRawParameterValue("q")->load());

    for (int i = 0; i < kNumBands; ++i)
    {
        gainSmoothers[(size_t) i].setCurrentAndTargetValue(targetGains[(size_t) i]);
        appliedGains[(size_t) i] = targetGains[(size_t) i];
        appliedFrequencies[(size_t) i] = targetFrequencies[(size_t) i];
    }

    appliedCutFilters = targetCutFilters;
    qSmoother.setCurrentAndTargetValue(targetQ);
    appliedQ = targetQ;

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
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    const double safeSampleRate = sanitizeSampleRate(preparedSampleRate);
    const auto safeGains = sanitizeBandGains(bandGains);
    const auto bandFreqs = getBandFrequencies(bandFrequencies, safeSampleRate);
    const float safeQ = sanitizeQ(qValue);

    auto low     = Coeffs::makeLowShelf (safeSampleRate, static_cast<float>(bandFreqs[0]), safeQ, dbToGain(safeGains[0]));
    auto lowMid  = Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[1]), safeQ, dbToGain(safeGains[1]));
    auto mid     = Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[2]), safeQ, dbToGain(safeGains[2]));
    auto highMid = Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[3]), safeQ, dbToGain(safeGains[3]));
    auto high    = Coeffs::makeHighShelf(safeSampleRate, static_cast<float>(bandFreqs[4]), safeQ, dbToGain(safeGains[4]));

    *leftFilters[0].coefficients  = *low;
    *leftFilters[1].coefficients  = *lowMid;
    *leftFilters[2].coefficients  = *mid;
    *leftFilters[3].coefficients  = *highMid;
    *leftFilters[4].coefficients  = *high;

    *rightFilters[0].coefficients = *low;
    *rightFilters[1].coefficients = *lowMid;
    *rightFilters[2].coefficients = *mid;
    *rightFilters[3].coefficients = *highMid;
    *rightFilters[4].coefficients = *high;
}

void MusiqueEQProcessor::updateCutFilterCoefficients(const CutFilterSettings& settings)
{
    const double safeSampleRate = sanitizeSampleRate(preparedSampleRate);
    const auto applyDesignedFilters = [](auto& left, auto& right, const auto& designed)
    {
        const int numStages = juce::jmin(static_cast<int>(designed.size()), kMaxCutFilterStages);
        for (int i = 0; i < numStages; ++i)
        {
            *left[i].coefficients = *designed.getUnchecked(i);
            *right[i].coefficients = *designed.getUnchecked(i);
        }

        return numStages;
    };

    if (settings.hpfEnabled)
    {
        const auto designed = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
            sanitizeCutFrequency(settings.hpfFrequency, safeSampleRate, 30.0f),
            safeSampleRate,
            slopeDbPerOctToOrder(settings.hpfSlopeDbPerOct));
        activeHpfStages = applyDesignedFilters(leftHpfFilters, rightHpfFilters, designed);
    }
    else
    {
        activeHpfStages = 0;
    }

    if (settings.lpfEnabled)
    {
        const auto designed = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(
            sanitizeCutFrequency(settings.lpfFrequency, safeSampleRate, 18000.0f),
            safeSampleRate,
            slopeDbPerOctToOrder(settings.lpfSlopeDbPerOct));
        activeLpfStages = applyDesignedFilters(leftLpfFilters, rightLpfFilters, designed);
    }
    else
    {
        activeLpfStages = 0;
    }

    for (int i = 0; i < kMaxCutFilterStages; ++i)
    {
        leftHpfFilters[i].reset();
        rightHpfFilters[i].reset();
        leftLpfFilters[i].reset();
        rightLpfFilters[i].reset();
    }
}

bool MusiqueEQProcessor::refreshSmoothedTargets()
{
    std::array<float, kNumBands> newTargets {};
    for (size_t i = 0; i < kGainParamIds.size(); ++i)
        newTargets[i] = sanitizeGainDb(parameters.getRawParameterValue(kGainParamIds[i])->load());

    const auto newFrequencies = readBandFrequenciesFromParameters();
    const auto newCutFilters = readCutFilterSettingsFromParameters();
    const float newQ = sanitizeQ(parameters.getRawParameterValue("q")->load());

    bool changed = false;
    for (int i = 0; i < kNumBands; ++i)
    {
        if (! juce::approximatelyEqual(targetGains[(size_t) i], newTargets[(size_t) i]))
        {
            targetGains[(size_t) i] = newTargets[(size_t) i];
            gainSmoothers[(size_t) i].setTargetValue(newTargets[(size_t) i]);
            changed = true;
        }

        if (! juce::approximatelyEqual(targetFrequencies[(size_t) i], newFrequencies[(size_t) i]))
        {
            targetFrequencies[(size_t) i] = newFrequencies[(size_t) i];
            changed = true;
        }
    }

    if (! juce::approximatelyEqual(targetQ, newQ))
    {
        targetQ = newQ;
        qSmoother.setTargetValue(newQ);
        changed = true;
    }

    if (! cutFilterSettingsEqual(targetCutFilters, newCutFilters))
    {
        targetCutFilters = newCutFilters;
        changed = true;
    }

    return changed;
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

int MusiqueEQProcessor::slopeDbPerOctToOrder(int slopeDbPerOct) noexcept
{
    return juce::jlimit(2, 8, sanitizeSlopeDbPerOct(static_cast<float>(slopeDbPerOct)) / 6);
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

float MusiqueEQProcessor::computeInternalTrimDb(const std::array<float, kNumBands>& bandGains,
                                                float qValue,
                                                const std::array<float, kNumBands>& bandFrequencies,
                                                double sampleRate) noexcept
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    const double safeSampleRate = sanitizeSampleRate(sampleRate);
    const auto safeGains = sanitizeBandGains(bandGains);
    const auto bandFreqs = getBandFrequencies(bandFrequencies, safeSampleRate);
    const float safeQ = sanitizeQ(qValue);

    const auto low     = Coeffs::makeLowShelf (safeSampleRate, static_cast<float>(bandFreqs[0]), safeQ, dbToGain(safeGains[0]));
    const auto lowMid  = Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[1]), safeQ, dbToGain(safeGains[1]));
    const auto mid     = Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[2]), safeQ, dbToGain(safeGains[2]));
    const auto highMid = Coeffs::makePeakFilter(safeSampleRate, static_cast<float>(bandFreqs[3]), safeQ, dbToGain(safeGains[3]));
    const auto high    = Coeffs::makeHighShelf(safeSampleRate, static_cast<float>(bandFreqs[4]), safeQ, dbToGain(safeGains[4]));

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
        const double magnitude = low->getMagnitudeForFrequency(frequency, safeSampleRate)
            * lowMid->getMagnitudeForFrequency(frequency, safeSampleRate)
            * mid->getMagnitudeForFrequency(frequency, safeSampleRate)
            * highMid->getMagnitudeForFrequency(frequency, safeSampleRate)
            * high->getMagnitudeForFrequency(frequency, safeSampleRate);

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

    const bool targetChanged = refreshSmoothedTargets();
    const bool smoothingActive = targetChanged
        || qSmoother.isSmoothing()
        || std::any_of(gainSmoothers.begin(), gainSmoothers.end(), [](auto& smoother) { return smoother.isSmoothing(); });

    if (mix <= 0.0001f)
    {
        currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
        buffer.applyGain(out);
        visualState.captureOutput(buffer);
        return;
    }

    if (! cutFilterSettingsEqual(appliedCutFilters, targetCutFilters))
    {
        appliedCutFilters = targetCutFilters;
        updateCutFilterCoefficients(appliedCutFilters);
    }

    if (! smoothingActive)
    {
        if (! juce::approximatelyEqual(appliedQ, targetQ)
            || ! std::equal(appliedGains.begin(), appliedGains.end(), targetGains.begin(), [](float a, float b) { return juce::approximatelyEqual(a, b); })
            || ! std::equal(appliedFrequencies.begin(), appliedFrequencies.end(), targetFrequencies.begin(), [](float a, float b) { return juce::approximatelyEqual(a, b); }))
        {
            appliedQ = targetQ;
            appliedGains = targetGains;
            appliedFrequencies = targetFrequencies;
            updateFilterCoefficients(appliedGains, appliedQ, appliedFrequencies);
        }

        const float internalTrimDb = computeInternalTrimDb(appliedGains, appliedQ, appliedFrequencies, preparedSampleRate);
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
            for (int i = 0; i < kNumBands; ++i)
                appliedGains[(size_t) i] = gainSmoothers[(size_t) i].getNextValue();
            appliedQ = qSmoother.getNextValue();
            appliedFrequencies = targetFrequencies;
            updateFilterCoefficients(appliedGains, appliedQ, appliedFrequencies);
            const float internalTrimDb = computeInternalTrimDb(appliedGains, appliedQ, appliedFrequencies, preparedSampleRate);
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
