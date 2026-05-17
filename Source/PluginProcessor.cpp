#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    static float dbToGain(float dB) { return std::pow(10.0f, dB / 20.0f); }
}

MusiqueEQProcessor::MusiqueEQProcessor()
    : AudioProcessor(BusesProperties()
          .withInput ("Input",  juce::AudioChannelSet::stereo(), true)
          .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "MusiqueEQ", createParameterLayout())
{
    targetGains.fill(0.0f);
    appliedGains.fill(0.0f);
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

    return { p.begin(), p.end() };
}

void MusiqueEQProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    preparedSampleRate = sampleRate;
    currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = 2048;
    spec.numChannels = 1;

    for (int i = 0; i < kNumBands; ++i)
    {
        leftFilters[i].reset();
        rightFilters[i].reset();
        leftFilters[i].prepare(spec);
        rightFilters[i].prepare(spec);
        gainSmoothers[(size_t) i].reset(sampleRate, 0.02);
    }

    qSmoother.reset(sampleRate, 0.02);

    targetGains[0] = parameters.getRawParameterValue("low_gain")->load();
    targetGains[1] = parameters.getRawParameterValue("low_mid_gain")->load();
    targetGains[2] = parameters.getRawParameterValue("mid_gain")->load();
    targetGains[3] = parameters.getRawParameterValue("high_mid_gain")->load();
    targetGains[4] = parameters.getRawParameterValue("high_gain")->load();
    targetQ = parameters.getRawParameterValue("q")->load();

    for (int i = 0; i < kNumBands; ++i)
    {
        gainSmoothers[(size_t) i].setCurrentAndTargetValue(targetGains[(size_t) i]);
        appliedGains[(size_t) i] = targetGains[(size_t) i];
    }

    qSmoother.setCurrentAndTargetValue(targetQ);
    appliedQ = targetQ;

    updateFilterCoefficients(appliedGains, appliedQ);
}

void MusiqueEQProcessor::releaseResources()
{
    for (int i = 0; i < kNumBands; ++i)
    {
        leftFilters[i].reset();
        rightFilters[i].reset();
    }

    currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
}

bool MusiqueEQProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void MusiqueEQProcessor::updateFilterCoefficients(const std::array<float, kNumBands>& bandGains, float qValue)
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    const double safeSampleRate = juce::jmax(12000.0, preparedSampleRate);
    const double maxFrequency = juce::jmax(200.0, safeSampleRate * 0.45);
    const float safeQ = juce::jlimit(0.3f, 8.0f, qValue);

    const double lowFreq = juce::jmin(100.0, maxFrequency);
    const double lowMidFreq = juce::jmin(350.0, maxFrequency);
    const double midFreq = juce::jmin(1200.0, maxFrequency);
    const double highMidFreq = juce::jmin(4500.0, maxFrequency);
    const double highFreq = juce::jmin(10000.0, maxFrequency);

    auto low     = Coeffs::makeLowShelf (safeSampleRate, lowFreq, safeQ, dbToGain(bandGains[0]));
    auto lowMid  = Coeffs::makePeakFilter(safeSampleRate, lowMidFreq, safeQ, dbToGain(bandGains[1]));
    auto mid     = Coeffs::makePeakFilter(safeSampleRate, midFreq, safeQ, dbToGain(bandGains[2]));
    auto highMid = Coeffs::makePeakFilter(safeSampleRate, highMidFreq, safeQ, dbToGain(bandGains[3]));
    auto high    = Coeffs::makeHighShelf(safeSampleRate, highFreq, safeQ, dbToGain(bandGains[4]));

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

bool MusiqueEQProcessor::refreshSmoothedTargets()
{
    const float newTargets[kNumBands] = {
        parameters.getRawParameterValue("low_gain")->load(),
        parameters.getRawParameterValue("low_mid_gain")->load(),
        parameters.getRawParameterValue("mid_gain")->load(),
        parameters.getRawParameterValue("high_mid_gain")->load(),
        parameters.getRawParameterValue("high_gain")->load()
    };
    const float newQ = parameters.getRawParameterValue("q")->load();

    bool changed = false;
    for (int i = 0; i < kNumBands; ++i)
    {
        if (! juce::approximatelyEqual(targetGains[(size_t) i], newTargets[i]))
        {
            targetGains[(size_t) i] = newTargets[i];
            gainSmoothers[(size_t) i].setTargetValue(newTargets[i]);
            changed = true;
        }
    }

    if (! juce::approximatelyEqual(targetQ, newQ))
    {
        targetQ = newQ;
        qSmoother.setTargetValue(newQ);
        changed = true;
    }

    return changed;
}

float MusiqueEQProcessor::computeInternalTrimDb(const std::array<float, kNumBands>& bandGains, float qValue) noexcept
{
    float positiveBoostSum = 0.0f;
    float maxBoost = 0.0f;
    for (float gain : bandGains)
    {
        const float positive = juce::jmax(0.0f, gain);
        positiveBoostSum += positive;
        maxBoost = juce::jmax(maxBoost, positive);
    }

    const float qStress = juce::jlimit(0.0f, 1.0f, (qValue - 1.0f) / 7.0f);
    const float excessMaxBoost = juce::jmax(0.0f, maxBoost - 6.0f);
    const float excessBoostSum = juce::jmax(0.0f, positiveBoostSum - 9.0f);
    const float trimDb = excessMaxBoost * 0.28f
        + excessBoostSum * 0.08f
        + excessMaxBoost * qStress * 0.22f;
    return juce::jlimit(0.0f, 12.0f, trimDb);
}

void MusiqueEQProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    visualState.captureInput(buffer);

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return;

    const bool bypass = (*parameters.getRawParameterValue("bypass") > 0.5f);
    const bool mono   = (*parameters.getRawParameterValue("mono") > 0.5f);
    const float mix   = *parameters.getRawParameterValue("mix") / 100.0f;
    const float out   = dbToGain(*parameters.getRawParameterValue("output"));

    if (mono)
    {
        for (int n = 0; n < numSamples; ++n)
        {
            const float m = 0.5f * (buffer.getSample(0, n) + buffer.getSample(1, n));
            buffer.setSample(0, n, m);
            buffer.setSample(1, n, m);
        }
    }

    if (bypass)
    {
        currentInternalTrimDb.store(0.0f, std::memory_order_relaxed);
        buffer.applyGain(out);
        visualState.captureOutput(buffer);
        return;
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

    if (! smoothingActive)
    {
        if (! juce::approximatelyEqual(appliedQ, targetQ)
            || ! std::equal(appliedGains.begin(), appliedGains.end(), targetGains.begin(), [](float a, float b) { return juce::approximatelyEqual(a, b); }))
        {
            appliedQ = targetQ;
            appliedGains = targetGains;
            updateFilterCoefficients(appliedGains, appliedQ);
        }

        const float internalTrimDb = computeInternalTrimDb(appliedGains, appliedQ);
        const float internalTrimGain = dbToGain(-internalTrimDb);
        currentInternalTrimDb.store(internalTrimDb, std::memory_order_relaxed);

        for (int n = 0; n < numSamples; ++n)
        {
            const float dryL = buffer.getSample(0, n);
            const float dryR = buffer.getSample(1, n);
            float wetL = dryL * internalTrimGain;
            float wetR = dryR * internalTrimGain;

            for (int i = 0; i < kNumBands; ++i)
            {
                wetL = leftFilters[i].processSample(wetL);
                wetR = rightFilters[i].processSample(wetR);
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
            updateFilterCoefficients(appliedGains, appliedQ);
            const float internalTrimDb = computeInternalTrimDb(appliedGains, appliedQ);
            const float internalTrimGain = dbToGain(-internalTrimDb);
            currentInternalTrimDb.store(internalTrimDb, std::memory_order_relaxed);

            for (int n = sampleIndex; n < chunkEnd; ++n)
            {
                const float dryL = buffer.getSample(0, n);
                const float dryR = buffer.getSample(1, n);
                float wetL = dryL * internalTrimGain;
                float wetR = dryR * internalTrimGain;

                for (int i = 0; i < kNumBands; ++i)
                {
                    wetL = leftFilters[i].processSample(wetL);
                    wetR = rightFilters[i].processSample(wetR);
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
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MusiqueEQProcessor::createEditor()
{
    return new MusiqueEQEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MusiqueEQProcessor();
}
