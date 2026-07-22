#include "PluginProcessor.h"
#include "EQGraphMapping.h"
#include "EQGraphUI.h"
#include "EQPresetMigration.h"
#include "BinaryData.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace
{
constexpr double pi = 3.14159265358979323846;

constexpr std::array<const char*, 5> frequencyParamIds {{
    "low_freq", "low_mid_freq", "mid_freq", "high_mid_freq", "high_freq"
}};

constexpr std::array<float, 5> legacyFrequencyDefaults {{
    100.0f, 350.0f, 1200.0f, 4500.0f, 10000.0f
}};

constexpr std::array<const char*, 6> cutFilterParamIds {{
    "hpf_enabled", "hpf_freq", "hpf_slope", "lpf_enabled", "lpf_freq", "lpf_slope"
}};

struct GoldenAudioSignature
{
    int version = 1;
    double sampleRate = 48000.0;
    int blockSize = 256;
    int frames = 4096;
    std::vector<int> tapIndices;
    std::vector<float> leftTaps;
    std::vector<float> rightTaps;
    float rmsLeft = 0.0f;
    float rmsRight = 0.0f;
    float peak = 0.0f;
    float trimDb = 0.0f;
};

struct Runner
{
    int checks = 0;
    int failures = 0;

    void expect(bool condition, const std::string& name)
    {
        ++checks;
        if (condition)
        {
            std::cout << "[PASS] " << name << '\n';
            return;
        }

        ++failures;
        std::cout << "[FAIL] " << name << '\n';
    }
};

float dbToGain(float dB)
{
    return std::pow(10.0f, dB / 20.0f);
}

void setParameter(MusiqueEQProcessor& processor, const juce::String& id, float value)
{
    auto* parameter = processor.getAPVTS().getParameter(id);
    if (parameter == nullptr)
    {
        std::cerr << "Missing parameter: " << id << '\n';
        std::exit(2);
    }

    parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
}

float getParameterValue(MusiqueEQProcessor& processor, const juce::String& id)
{
    if (auto* raw = processor.getAPVTS().getRawParameterValue(id))
        return raw->load();

    std::cerr << "Missing raw parameter: " << id << '\n';
    std::exit(2);
}

void setAllBandGains(MusiqueEQProcessor& processor, float value)
{
    setParameter(processor, "low_gain", value);
    setParameter(processor, "low_mid_gain", value);
    setParameter(processor, "mid_gain", value);
    setParameter(processor, "high_mid_gain", value);
    setParameter(processor, "high_gain", value);
}

void setDefaultBandFrequencies(MusiqueEQProcessor& processor)
{
    for (size_t i = 0; i < frequencyParamIds.size(); ++i)
        setParameter(processor, frequencyParamIds[i], legacyFrequencyDefaults[i]);
}

juce::ValueTree copyStateTree(MusiqueEQProcessor& processor)
{
    juce::MemoryBlock stateData;
    processor.getStateInformation(stateData);

    auto xml = juce::AudioProcessor::getXmlFromBinary(stateData.getData(), static_cast<int>(stateData.getSize()));
    if (xml == nullptr)
    {
        std::cerr << "Failed to decode processor state XML\n";
        std::exit(2);
    }

    return juce::ValueTree::fromXml(*xml);
}

void loadStateTree(MusiqueEQProcessor& processor, const juce::ValueTree& state)
{
    auto xml = state.createXml();
    if (xml == nullptr)
    {
        std::cerr << "Failed to encode processor state XML\n";
        std::exit(2);
    }

    juce::MemoryBlock stateData;
    juce::AudioProcessor::copyXmlToBinary(*xml, stateData);
    processor.setStateInformation(stateData.getData(), static_cast<int>(stateData.getSize()));
}

void removeParameterFromState(juce::ValueTree& state, const juce::String& id)
{
    for (int i = state.getNumChildren() - 1; i >= 0; --i)
    {
        auto child = state.getChild(i);
        if (child.hasType("PARAM") && child.getProperty("id").toString() == id)
            state.removeChild(i, nullptr);
    }
}

void prepare(MusiqueEQProcessor& processor, double sampleRate, int maximumBlockSize)
{
    processor.setPlayConfigDetails(2, 2, sampleRate, maximumBlockSize);
    processor.prepareToPlay(sampleRate, maximumBlockSize);
}

juce::AudioBuffer<float> makeStereoSignal(int samples, double sampleRate, float amplitude = 0.05f)
{
    juce::AudioBuffer<float> buffer(2, samples);

    for (int n = 0; n < samples; ++n)
    {
        const double t = static_cast<double>(n) / sampleRate;
        const auto left = static_cast<float>(amplitude
            * (std::sin(2.0 * pi * 73.0 * t) + 0.25 * std::sin(2.0 * pi * 997.0 * t)));
        const auto right = static_cast<float>(amplitude
            * (std::sin(2.0 * pi * 311.0 * t + 0.37) + 0.20 * std::sin(2.0 * pi * 1703.0 * t)));

        buffer.setSample(0, n, left);
        buffer.setSample(1, n, right);
    }

    return buffer;
}

juce::AudioBuffer<float> makeLowFrequencySignal(int samples, double sampleRate, float amplitude = 0.05f)
{
    juce::AudioBuffer<float> buffer(2, samples);

    for (int n = 0; n < samples; ++n)
    {
        const double t = static_cast<double>(n) / sampleRate;
        const auto value = static_cast<float>(amplitude * std::sin(2.0 * pi * 60.0 * t));
        buffer.setSample(0, n, value);
        buffer.setSample(1, n, value * 0.7f);
    }

    return buffer;
}

juce::AudioBuffer<float> makeStereoSine(int samples, double sampleRate, double frequency, float amplitude = 0.05f)
{
    juce::AudioBuffer<float> buffer(2, samples);

    for (int n = 0; n < samples; ++n)
    {
        const double t = static_cast<double>(n) / sampleRate;
        const auto value = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * t));
        buffer.setSample(0, n, value);
        buffer.setSample(1, n, value * 0.83f);
    }

    return buffer;
}

juce::AudioBuffer<float> makeBroadbandProbe(int samples, double sampleRate, float amplitude = 0.015f)
{
    juce::AudioBuffer<float> buffer(2, samples);

    for (int n = 0; n < samples; ++n)
    {
        const double t = static_cast<double>(n) / sampleRate;
        const auto left = static_cast<float>(amplitude
            * (std::sin(2.0 * pi * 120.0 * t)
               + 0.8 * std::sin(2.0 * pi * 750.0 * t)
               + 0.6 * std::sin(2.0 * pi * 2400.0 * t)
               + 0.4 * std::sin(2.0 * pi * 6800.0 * t)));
        const auto right = static_cast<float>(amplitude
            * (std::sin(2.0 * pi * 180.0 * t + 0.11)
               + 0.7 * std::sin(2.0 * pi * 950.0 * t)
               + 0.5 * std::sin(2.0 * pi * 3200.0 * t)
               + 0.3 * std::sin(2.0 * pi * 9100.0 * t)));

        buffer.setSample(0, n, left);
        buffer.setSample(1, n, right);
    }

    return buffer;
}

juce::AudioBuffer<float> copyOf(const juce::AudioBuffer<float>& source)
{
    juce::AudioBuffer<float> copy;
    copy.makeCopyOf(source);
    return copy;
}

void process(MusiqueEQProcessor& processor, juce::AudioBuffer<float>& buffer)
{
    juce::MidiBuffer midi;
    processor.processBlock(buffer, midi);
}

void processInBlocks(MusiqueEQProcessor& processor, juce::AudioBuffer<float>& buffer, int blockSize)
{
    int offset = 0;
    while (offset < buffer.getNumSamples())
    {
        const int numSamples = juce::jmin(blockSize, buffer.getNumSamples() - offset);
        juce::AudioBuffer<float> block(buffer.getNumChannels(), numSamples);
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            block.copyFrom(channel, 0, buffer, channel, offset, numSamples);

        process(processor, block);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.copyFrom(channel, offset, block, channel, 0, numSamples);

        offset += numSamples;
    }
}

void settleSmoothing(MusiqueEQProcessor& processor, int blocks = 80, int blockSize = 64)
{
    for (int i = 0; i < blocks; ++i)
    {
        juce::AudioBuffer<float> buffer(2, blockSize);
        buffer.clear();
        process(processor, buffer);
    }
}

juce::AudioBuffer<float> makeImpulse(int samples, int channels = 2)
{
    juce::AudioBuffer<float> buffer(channels, samples);
    buffer.clear();

    for (int channel = 0; channel < channels; ++channel)
        buffer.setSample(channel, 0, 1.0f);

    return buffer;
}

float maxAbs(const juce::AudioBuffer<float>& buffer)
{
    float out = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            out = juce::jmax(out, std::abs(buffer.getSample(channel, sample)));
    return out;
}

float maxAbsDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float out = 0.0f;
    const int channels = juce::jmin(a.getNumChannels(), b.getNumChannels());
    const int samples = juce::jmin(a.getNumSamples(), b.getNumSamples());

    for (int channel = 0; channel < channels; ++channel)
        for (int sample = 0; sample < samples; ++sample)
            out = juce::jmax(out, std::abs(a.getSample(channel, sample) - b.getSample(channel, sample)));

    return out;
}

float maxChannelDiff(const juce::AudioBuffer<float>& buffer)
{
    float out = 0.0f;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        out = juce::jmax(out, std::abs(buffer.getSample(0, sample) - buffer.getSample(1, sample)));
    return out;
}

float maxAdjacentDiff(const juce::AudioBuffer<float>& buffer)
{
    float out = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 1; sample < buffer.getNumSamples(); ++sample)
            out = juce::jmax(out, std::abs(buffer.getSample(channel, sample) - buffer.getSample(channel, sample - 1)));
    return out;
}

float rmsFrom(const juce::AudioBuffer<float>& buffer, int startSample)
{
    double sum = 0.0;
    int count = 0;

    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    {
        for (int sample = juce::jmax(0, startSample); sample < buffer.getNumSamples(); ++sample)
        {
            const double value = buffer.getSample(channel, sample);
            sum += value * value;
            ++count;
        }
    }

    return count > 0 ? static_cast<float>(std::sqrt(sum / static_cast<double>(count))) : 0.0f;
}

float channelRms(const juce::AudioBuffer<float>& buffer, int channel)
{
    if (channel < 0 || channel >= buffer.getNumChannels() || buffer.getNumSamples() <= 0)
        return 0.0f;

    double sum = 0.0;
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const double value = buffer.getSample(channel, sample);
        sum += value * value;
    }

    return static_cast<float>(std::sqrt(sum / static_cast<double>(buffer.getNumSamples())));
}

int firstSignificantSample(const juce::AudioBuffer<float>& buffer, float threshold = 1.0e-6f)
{
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            if (std::abs(buffer.getSample(channel, sample)) >= threshold)
                return sample;

    return -1;
}

bool allFinite(const juce::AudioBuffer<float>& buffer)
{
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
            if (! std::isfinite(buffer.getSample(channel, sample)))
                return false;

    return true;
}

juce::AudioBuffer<float> renderImpulseResponse(MusiqueEQProcessor& processor, double sampleRate)
{
    constexpr int fftOrder = 14;
    constexpr int fftSize = 1 << fftOrder;
    prepare(processor, sampleRate, fftSize);

    auto buffer = makeImpulse(fftSize, 2);
    process(processor, buffer);
    return buffer;
}

float fftMagnitudeDbAt(const juce::AudioBuffer<float>& impulseResponse, double sampleRate, double frequency)
{
    constexpr int fftOrder = 14;
    constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);

    const int samples = juce::jmin(fftSize, impulseResponse.getNumSamples());
    for (int sample = 0; sample < samples; ++sample)
        fftData[(size_t) sample] = impulseResponse.getSample(0, sample);

    fft.performFrequencyOnlyForwardTransform(fftData.data(), true);

    const auto maxBin = fftSize / 2;
    const int bin = juce::jlimit(1, maxBin - 1, static_cast<int>(std::round(frequency * fftSize / sampleRate)));
    const auto magnitude = juce::jmax(fftData[(size_t) bin], 1.0e-8f);
    return juce::Decibels::gainToDecibels(magnitude, -160.0f);
}

bool coefficientsFinite(const MusiqueEQProcessor::TestBiquadCoefficients& coefficients)
{
    return std::all_of(coefficients.begin(), coefficients.end(), [](float value)
    {
        return std::isfinite(value);
    });
}

float coefficientDistance(const MusiqueEQProcessor::TestBiquadCoefficients& a,
                          const MusiqueEQProcessor::TestBiquadCoefficients& b)
{
    float distance = 0.0f;
    for (size_t i = 0; i < a.size(); ++i)
        distance += std::abs(a[i] - b[i]);

    return distance;
}

void expectNearBuffer(Runner& runner,
                      const juce::AudioBuffer<float>& actual,
                      const juce::AudioBuffer<float>& expected,
                      float tolerance,
                      const std::string& name)
{
    const auto diff = maxAbsDiff(actual, expected);
    runner.expect(diff <= tolerance, name + " (max diff " + std::to_string(diff) + ")");
}

void expectInRange(Runner& runner,
                   float actual,
                   float minValue,
                   float maxValue,
                   const std::string& name)
{
    runner.expect(actual >= minValue && actual <= maxValue,
                  name + " (actual " + std::to_string(actual)
                      + ", range " + std::to_string(minValue)
                      + ".." + std::to_string(maxValue) + ")");
}

void expectParameterNear(Runner& runner,
                         MusiqueEQProcessor& processor,
                         const juce::String& id,
                         float expected,
                         float tolerance,
                         const std::string& name)
{
    const float actual = getParameterValue(processor, id);
    runner.expect(std::abs(actual - expected) <= tolerance,
                  name + " (actual " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}

void expectObjectFloatNear(Runner& runner,
                           juce::DynamicObject* object,
                           const char* id,
                           float expected,
                           float tolerance,
                           const std::string& name)
{
    if (object == nullptr || ! object->hasProperty(id))
    {
        runner.expect(false, name + " (missing property)");
        return;
    }

    const auto actual = static_cast<float>(object->getProperty(id));
    runner.expect(std::abs(actual - expected) <= tolerance,
                  name + " (actual " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}

void applyPresetToProcessor(MusiqueEQProcessor& processor, const juce::var& preset)
{
    const auto migrated = musique::eq::presets::migratePresetForCurrentParameters(preset);
    auto* object = migrated.getDynamicObject();
    if (object == nullptr)
        return;

    const auto& properties = object->getProperties();
    for (int i = 0; i < properties.size(); ++i)
    {
        const auto key = properties.getName(i);
        if (key == juce::Identifier("name"))
            continue;

        if (auto* parameter = processor.getAPVTS().getParameter(key.toString()))
        {
            const auto value = static_cast<float>(object->getProperty(key));
            parameter->setValueNotifyingHost(parameter->convertTo0to1(value));
        }
    }
}

juce::File findFactoryBankForTests()
{
    auto dir = juce::File::getCurrentWorkingDirectory();

    for (int i = 0; i < 12; ++i)
    {
        const std::array<juce::File, 3> candidates {{
            dir.getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("fx-eq").getChildFile("Presets").getChildFile("factory_bank.json"),
            dir.getChildFile("FX").getChildFile("fx-eq").getChildFile("Presets").getChildFile("factory_bank.json")
        }};

        for (const auto& candidate : candidates)
            if (candidate.existsAsFile())
                return candidate;

        const auto parent = dir.getParentDirectory();
        if (parent == dir)
            break;

        dir = parent;
    }

    return {};
}

juce::Array<juce::var> loadFactoryPresetsForTests(Runner& runner)
{
    juce::Array<juce::var> presets;
    const auto factoryBank = findFactoryBankForTests();
    runner.expect(factoryBank.existsAsFile(), "factory preset bank file is discoverable");

    if (! factoryBank.existsAsFile())
        return presets;

    const auto parsed = juce::JSON::parse(factoryBank.loadFileAsString());
    auto* bank = parsed.getDynamicObject();
    runner.expect(bank != nullptr, "factory preset bank parses as JSON object");

    if (bank == nullptr)
        return presets;

    auto* presetArray = bank->getProperty("presets").getArray();
    runner.expect(presetArray != nullptr, "factory preset bank contains presets array");

    if (presetArray != nullptr)
        presets.addArray(*presetArray);

    return presets;
}

juce::File findGoldenReferenceForTests()
{
    auto dir = juce::File::getCurrentWorkingDirectory();

    for (int i = 0; i < 12; ++i)
    {
        const std::array<juce::File, 3> candidates {{
            dir.getChildFile("Tests").getChildFile("golden").getChildFile("eq_reference_48k_stereo.json"),
            dir.getChildFile("fx-eq").getChildFile("Tests").getChildFile("golden").getChildFile("eq_reference_48k_stereo.json"),
            dir.getChildFile("FX").getChildFile("fx-eq").getChildFile("Tests").getChildFile("golden").getChildFile("eq_reference_48k_stereo.json")
        }};

        for (const auto& candidate : candidates)
            if (candidate.existsAsFile())
                return candidate;

        const auto parent = dir.getParentDirectory();
        if (parent == dir)
            break;

        dir = parent;
    }

    return {};
}

void configureGoldenParameters(MusiqueEQProcessor& processor)
{
    setParameter(processor, "low_gain", 3.0f);
    setParameter(processor, "low_mid_gain", -2.0f);
    setParameter(processor, "mid_gain", 4.0f);
    setParameter(processor, "high_mid_gain", -1.5f);
    setParameter(processor, "high_gain", 2.0f);
    setParameter(processor, "q", 1.8f);
    setParameter(processor, "low_freq", 90.0f);
    setParameter(processor, "low_mid_freq", 420.0f);
    setParameter(processor, "mid_freq", 1800.0f);
    setParameter(processor, "high_mid_freq", 6200.0f);
    setParameter(processor, "high_freq", 12000.0f);
    setParameter(processor, "hpf_enabled", 1.0f);
    setParameter(processor, "hpf_freq", 45.0f);
    setParameter(processor, "hpf_slope", 24.0f);
    setParameter(processor, "lpf_enabled", 1.0f);
    setParameter(processor, "lpf_freq", 17500.0f);
    setParameter(processor, "lpf_slope", 24.0f);
    setParameter(processor, "mix", 100.0f);
    setParameter(processor, "output", -1.0f);
    setParameter(processor, "bypass", 0.0f);
    setParameter(processor, "mono", 0.0f);
}

GoldenAudioSignature renderGoldenAudioSignature()
{
    GoldenAudioSignature signature;
    signature.tapIndices = { 0, 1, 2, 3, 4, 7, 16, 31, 64, 127, 255, 511, 1023, 2047, 3071, 4095 };

    MusiqueEQProcessor processor;
    configureGoldenParameters(processor);
    prepare(processor, signature.sampleRate, signature.blockSize);

    auto buffer = makeBroadbandProbe(signature.frames, signature.sampleRate, 0.014f);
    processInBlocks(processor, buffer, signature.blockSize);

    signature.leftTaps.reserve(signature.tapIndices.size());
    signature.rightTaps.reserve(signature.tapIndices.size());
    for (int index : signature.tapIndices)
    {
        signature.leftTaps.push_back(buffer.getSample(0, index));
        signature.rightTaps.push_back(buffer.getSample(1, index));
    }

    signature.rmsLeft = channelRms(buffer, 0);
    signature.rmsRight = channelRms(buffer, 1);
    signature.peak = maxAbs(buffer);
    signature.trimDb = processor.getCurrentInternalTrimDb();
    return signature;
}

template <typename Value>
juce::var vectorToJsonArray(const std::vector<Value>& values)
{
    juce::Array<juce::var> array;
    for (const auto value : values)
        array.add(juce::var(value));

    return juce::var(array);
}

juce::var goldenToJson(const GoldenAudioSignature& signature)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", signature.version);
    root->setProperty("sampleRate", signature.sampleRate);
    root->setProperty("blockSize", signature.blockSize);
    root->setProperty("frames", signature.frames);
    root->setProperty("tapIndices", vectorToJsonArray(signature.tapIndices));
    root->setProperty("tapsL", vectorToJsonArray(signature.leftTaps));
    root->setProperty("tapsR", vectorToJsonArray(signature.rightTaps));
    root->setProperty("rmsL", signature.rmsLeft);
    root->setProperty("rmsR", signature.rmsRight);
    root->setProperty("peak", signature.peak);
    root->setProperty("trimDb", signature.trimDb);
    return juce::var(root);
}

bool readIntVector(const juce::var& value, std::vector<int>& out)
{
    auto* array = value.getArray();
    if (array == nullptr)
        return false;

    out.clear();
    out.reserve(static_cast<size_t>(array->size()));
    for (int i = 0; i < array->size(); ++i)
        out.push_back(static_cast<int>(array->getReference(i)));

    return true;
}

bool readFloatVector(const juce::var& value, std::vector<float>& out)
{
    auto* array = value.getArray();
    if (array == nullptr)
        return false;

    out.clear();
    out.reserve(static_cast<size_t>(array->size()));
    for (int i = 0; i < array->size(); ++i)
        out.push_back(static_cast<float>(array->getReference(i)));

    return true;
}

bool loadGoldenAudioSignature(const juce::File& file, GoldenAudioSignature& signature)
{
    const auto parsed = juce::JSON::parse(file.loadFileAsString());
    auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return false;

    signature.version = static_cast<int>(object->getProperty("version"));
    signature.sampleRate = static_cast<double>(object->getProperty("sampleRate"));
    signature.blockSize = static_cast<int>(object->getProperty("blockSize"));
    signature.frames = static_cast<int>(object->getProperty("frames"));
    signature.rmsLeft = static_cast<float>(object->getProperty("rmsL"));
    signature.rmsRight = static_cast<float>(object->getProperty("rmsR"));
    signature.peak = static_cast<float>(object->getProperty("peak"));
    signature.trimDb = static_cast<float>(object->getProperty("trimDb"));

    return signature.version == 1
        && readIntVector(object->getProperty("tapIndices"), signature.tapIndices)
        && readFloatVector(object->getProperty("tapsL"), signature.leftTaps)
        && readFloatVector(object->getProperty("tapsR"), signature.rightTaps)
        && signature.tapIndices.size() == signature.leftTaps.size()
        && signature.tapIndices.size() == signature.rightTaps.size();
}

bool writeGoldenAudioReference(const juce::File& outputFile)
{
    if (! outputFile.getParentDirectory().createDirectory())
        return false;

    const auto signature = renderGoldenAudioSignature();
    return outputFile.replaceWithText(juce::JSON::toString(goldenToJson(signature)));
}

void testGraphMappingHelpers(Runner& runner)
{
    namespace graph = musique::eq::graph;

    runner.expect(std::abs(graph::normalisedToFrequency(0.0f, 48000.0) - 20.0f) <= 0.001f,
                  "graph mapping normalised 0 maps to 20 Hz");
    runner.expect(std::abs(graph::normalisedToFrequency(1.0f, 48000.0) - 20000.0f) <= 0.01f,
                  "graph mapping normalised 1 maps to 20 kHz at 48 kHz");
    runner.expect(std::abs(graph::normalisedToFrequency(1.0f, 12000.0) - 5400.0f) <= 0.01f,
                  "graph mapping clamps max frequency to 45 percent of low sample rates");

    const float normalised = 0.37f;
    const float frequency = graph::normalisedToFrequency(normalised, 48000.0);
    runner.expect(std::abs(graph::frequencyToNormalised(frequency, 48000.0) - normalised) <= 0.0001f,
                  "graph frequency normalised roundtrip is stable");

    const float x = graph::frequencyToX(1000.0f, 24.0f, 752.0f, 48000.0);
    runner.expect(std::abs(graph::xToFrequency(x, 24.0f, 752.0f, 48000.0) - 1000.0f) <= 0.01f,
                  "graph frequency to X roundtrip is stable");

    const float y = graph::gainToY(12.0f, 170.0f, 180.0f);
    runner.expect(std::abs(graph::yToGain(y, 170.0f, 180.0f) - 12.0f) <= 0.001f,
                  "graph gain to Y roundtrip is stable");
    runner.expect(graph::yToGain(-1000.0f, 170.0f, 180.0f) <= graph::maxGainDb,
                  "graph gain mapping clamps high values");
    runner.expect(graph::yToGain(1000.0f, 170.0f, 180.0f) >= graph::minGainDb,
                  "graph gain mapping clamps low values");

    runner.expect(graph::snapSlopeDbPerOct(10.0f) == 12, "graph slope snap selects 12 dB/oct below midpoint");
    runner.expect(graph::snapSlopeDbPerOct(20.0f) == 24, "graph slope snap selects 24 dB/oct near middle");
    runner.expect(graph::snapSlopeDbPerOct(40.0f) == 48, "graph slope snap selects 48 dB/oct above midpoint");
    runner.expect(graph::snapSlopeDbPerOct(std::numeric_limits<float>::quiet_NaN()) == 12,
                  "graph slope snap falls back to 12 dB/oct for NaN");
}

void testGraphUiHelpers(Runner& runner)
{
    namespace graphui = musique::eq::ui;

    const auto plot = graphui::graphPlotBounds(1024.0f);
    const auto panel = graphui::selectionPanelBounds(1024.0f);
    runner.expect(! graphui::overlaps(panel, plot), "UI selected-band panel stays outside graph plot bounds");
    runner.expect(panel.getY() < plot.getY(), "UI selected-band panel sits in preset bar above graph");

    const auto toggle = graphui::cutToggleBounds(panel);
    const auto slopes = graphui::slopeButtonBounds(panel);
    runner.expect(toggle.getX() >= panel.getX() && toggle.getRight() <= panel.getRight(),
                  "UI cut toggle bounds stay inside selected-band panel");
    runner.expect(slopes[0].getX() >= panel.getX() && slopes[2].getRight() <= panel.getRight(),
                  "UI slope segment bounds stay inside selected-band panel");

    const juce::Point<float> eqHandle { 200.0f, 200.0f };
    runner.expect(graphui::isEqHandleHit({ 210.0f, 205.0f }, eqHandle),
                  "UI EQ hit-test accepts a click near the visible point");
    runner.expect(! graphui::isEqHandleHit({ 240.0f, 200.0f }, eqHandle),
                  "UI EQ hit-test rejects a click away from the visible point");

    const juce::Point<float> cutHandle { 200.0f, 260.0f };
    runner.expect(graphui::isCutHandleHit({ 205.0f, 265.0f }, cutHandle),
                  "UI HPF/LPF hit-test accepts a click near the center handle");
    runner.expect(! graphui::isCutHandleHit({ 205.0f, 120.0f }, cutHandle),
                  "UI HPF/LPF hit-test rejects a click on the same vertical line away from the handle");

    const auto clamped = graphui::clampRectToBounds({ plot.getX() - 80.0f, plot.getY() - 40.0f, 54.0f, 12.0f }, plot);
    runner.expect(clamped.getX() >= plot.getX() && clamped.getRight() <= plot.getRight()
                    && clamped.getY() >= plot.getY() && clamped.getBottom() <= plot.getBottom(),
                  "UI graph labels clamp inside graph plot bounds");

    const auto frequencyText = graphui::formatFrequency(1200.0f);
    const auto gainText = graphui::formatGain(0.0f);
    const auto trimText = graphui::formatTrim(12.2f);
    runner.expect(frequencyText.isNotEmpty() && frequencyText.length() <= 10,
                  "UI frequency formatting is short and non-empty");
    runner.expect(gainText == "+0.0 dB", "UI gain formatting includes plus sign at unity");
    runner.expect(trimText.startsWith("TRIM -") && trimText.length() <= 8,
                  "UI trim formatting is short and non-empty");
    runner.expect(graphui::formatTrim(0.0f) == "SAFE", "UI trim formatting returns SAFE at neutral trim");
}

void testPresetMigrationHelper(Runner& runner)
{
    juce::DynamicObject::Ptr legacy = new juce::DynamicObject();
    legacy->setProperty("name", "Legacy User Tone");
    legacy->setProperty("low_gain", -2.0f);
    legacy->setProperty("low_mid_gain", -1.0f);
    legacy->setProperty("mid_gain", 1.5f);
    legacy->setProperty("high_mid_gain", 0.75f);
    legacy->setProperty("high_gain", 2.0f);
    legacy->setProperty("q", 1.2f);
    legacy->setProperty("mix", 88.0f);
    legacy->setProperty("output", -0.5f);
    legacy->setProperty("mono", false);

    const auto migrated = musique::eq::presets::migratePresetForCurrentParameters(juce::var(legacy.get()));
    auto* object = migrated.getDynamicObject();
    runner.expect(object != nullptr, "legacy preset migration returns an object preset");

    if (object == nullptr)
        return;

    for (size_t i = 0; i < frequencyParamIds.size(); ++i)
        expectObjectFloatNear(runner, object, frequencyParamIds[i], legacyFrequencyDefaults[i], 0.001f,
                              std::string("legacy preset migration fills ") + frequencyParamIds[i]);

    expectObjectFloatNear(runner, object, "hpf_enabled", 0.0f, 0.001f, "legacy preset migration fills HPF disabled");
    expectObjectFloatNear(runner, object, "hpf_freq", 30.0f, 0.001f, "legacy preset migration fills HPF frequency");
    expectObjectFloatNear(runner, object, "hpf_slope", 12.0f, 0.001f, "legacy preset migration fills HPF slope");
    expectObjectFloatNear(runner, object, "lpf_enabled", 0.0f, 0.001f, "legacy preset migration fills LPF disabled");
    expectObjectFloatNear(runner, object, "lpf_freq", 18000.0f, 0.01f, "legacy preset migration fills LPF frequency");
    expectObjectFloatNear(runner, object, "lpf_slope", 12.0f, 0.001f, "legacy preset migration fills LPF slope");
    for (auto* id : musique::eq::presets::futureBandQParamIds)
        expectObjectFloatNear(runner, object, id, 1.2f, 0.001f,
                              std::string("legacy preset migration fills future band Q ") + id);
    runner.expect(! object->hasProperty("bypass"), "legacy preset migration does not synthesize bypass");
    runner.expect(! legacy->hasProperty("low_freq"), "legacy preset migration leaves source preset unchanged");

    auto makeFutureQVariant = [](float futureQ)
    {
        juce::DynamicObject::Ptr preset = new juce::DynamicObject();
        preset->setProperty("name", "Future Q Compatibility");
        preset->setProperty("low_gain", 1.0f);
        preset->setProperty("low_mid_gain", -0.5f);
        preset->setProperty("mid_gain", 2.0f);
        preset->setProperty("high_mid_gain", -0.75f);
        preset->setProperty("high_gain", 0.8f);
        preset->setProperty("low_freq", 90.0f);
        preset->setProperty("low_mid_freq", 360.0f);
        preset->setProperty("mid_freq", 1800.0f);
        preset->setProperty("high_mid_freq", 6200.0f);
        preset->setProperty("high_freq", 12000.0f);
        preset->setProperty("q", 1.4f);
        preset->setProperty("mix", 100.0f);
        preset->setProperty("output", -1.0f);
        preset->setProperty("bypass", false);
        preset->setProperty("mono", false);
        preset->setProperty("hpf_enabled", true);
        preset->setProperty("hpf_freq", 35.0f);
        preset->setProperty("hpf_slope", 24.0f);
        preset->setProperty("lpf_enabled", true);
        preset->setProperty("lpf_freq", 17000.0f);
        preset->setProperty("lpf_slope", 12.0f);

        for (auto* id : musique::eq::presets::futureBandQParamIds)
            preset->setProperty(id, futureQ);

        return juce::var(preset.get());
    };

    MusiqueEQProcessor lowFutureQ;
    MusiqueEQProcessor highFutureQ;
    applyPresetToProcessor(lowFutureQ, makeFutureQVariant(0.3f));
    applyPresetToProcessor(highFutureQ, makeFutureQVariant(8.0f));
    prepare(lowFutureQ, 48000.0, 1024);
    prepare(highFutureQ, 48000.0, 1024);

    auto lowFutureQBuffer = makeBroadbandProbe(1024, 48000.0, 0.015f);
    auto highFutureQBuffer = copyOf(lowFutureQBuffer);
    process(lowFutureQ, lowFutureQBuffer);
    process(highFutureQ, highFutureQBuffer);
    expectNearBuffer(runner, lowFutureQBuffer, highFutureQBuffer, 1.0e-7f,
                     "future per-band Q metadata is ignored until public parameters exist");
}

void testFactoryPresetBank(Runner& runner)
{
    const auto presets = loadFactoryPresetsForTests(runner);
    runner.expect(presets.size() == 24, "factory preset bank contains exactly 24 public beta presets");

    const std::array<const char*, 24> expectedNames {{
        "Master Clean Reference",
        "Master Clean Open",
        "Master Clean Warm",
        "Master Corrective Mud Control",
        "Master Corrective Harshness Tamer",
        "Master Corrective Rumble Guard",
        "Mix Bus Smile",
        "Mix Bus Glue",
        "Mix Bus Punch",
        "Streaming Safe Bright",
        "Streaming Loudness Guard",
        "Streaming Dark Source Lift",
        "Band Limited Preview",
        "Band Limit Phone Check",
        "Band Limit Club PA",
        "Vocal Clear Lead",
        "Vocal Warm Lead",
        "Vocal Sibilance Tamer",
        "Drum Bus Punch",
        "Drum Overhead Air",
        "Low End Tight Bass",
        "Low End Sub Control",
        "Air Lift Smooth",
        "Repair Thin Source"
    }};

    const auto count = juce::jmin(presets.size(), static_cast<int>(expectedNames.size()));
    for (int i = 0; i < count; ++i)
    {
        auto* object = presets.getReference(i).getDynamicObject();
        runner.expect(object != nullptr, "factory preset " + std::to_string(i) + " is an object");

        if (object == nullptr)
            continue;

        const auto presetName = object->getProperty("name").toString();
        runner.expect(presetName == expectedNames[(size_t) i],
                      "factory preset order/name " + std::to_string(i) + " is " + expectedNames[(size_t) i]);

        for (auto* id : musique::eq::presets::requiredPresetParameterIds)
            runner.expect(object->hasProperty(id), "factory preset " + presetName.toStdString() + " includes " + id);

        const float qValue = static_cast<float>(object->getProperty("q"));
        for (auto* id : musique::eq::presets::futureBandQParamIds)
            expectObjectFloatNear(runner, object, id, qValue, 0.001f,
                                  "factory preset " + presetName.toStdString() + " future " + id + " mirrors global Q");

        runner.expect(static_cast<bool>(object->getProperty("bypass")) == false,
                      "factory preset " + presetName.toStdString() + " keeps bypass disabled");
        runner.expect(static_cast<bool>(object->getProperty("mono")) == false,
                      "factory preset " + presetName.toStdString() + " keeps mono disabled");
        runner.expect(static_cast<float>(object->getProperty("output")) <= 0.001f,
                      "factory preset " + presetName.toStdString() + " does not boost output");

        MusiqueEQProcessor processor;
        applyPresetToProcessor(processor, presets.getReference(i));
        prepare(processor, 48000.0, 1024);

        auto buffer = makeBroadbandProbe(1024, 48000.0, 0.015f);
        process(processor, buffer);

        runner.expect(allFinite(buffer), "factory preset " + presetName.toStdString() + " processing stays finite");
        runner.expect(maxAbs(buffer) < 512.0f, "factory preset " + presetName.toStdString() + " processing stays bounded");
    }
}

void testEmbeddedFactoryPresetBank(Runner& runner)
{
    runner.expect(BinaryData::factory_bank_jsonSize > 0, "embedded factory preset bank is non-empty");

    const auto jsonText = juce::String::fromUTF8(
        reinterpret_cast<const char*>(BinaryData::factory_bank_json),
        BinaryData::factory_bank_jsonSize);
    const auto parsed = juce::JSON::parse(jsonText);
    auto* bank = parsed.getDynamicObject();
    runner.expect(bank != nullptr, "embedded factory preset bank parses as JSON object");

    if (bank == nullptr)
        return;

    auto* presetArray = bank->getProperty("presets").getArray();
    runner.expect(presetArray != nullptr, "embedded factory preset bank contains presets array");
    runner.expect(presetArray != nullptr && presetArray->size() == 24,
                  "embedded factory preset bank contains exactly 24 public beta presets");
}

void testNeutrality(Runner& runner)
{
    MusiqueEQProcessor processor;
    prepare(processor, 48000.0, 512);

    auto buffer = makeStereoSignal(512, 48000.0);
    const auto expected = copyOf(buffer);

    process(processor, buffer);

    expectNearBuffer(runner, buffer, expected, 1.0e-4f, "neutral default processing is effectively dry");
    runner.expect(allFinite(buffer), "neutral default processing stays finite");
}

void testMixModes(Runner& runner)
{
    {
        MusiqueEQProcessor processor;
        setParameter(processor, "low_gain", 12.0f);
        setParameter(processor, "mix", 0.0f);
        setParameter(processor, "output", -3.0f);
        prepare(processor, 48000.0, 512);

        auto buffer = makeLowFrequencySignal(512, 48000.0);
        auto expected = copyOf(buffer);
        expected.applyGain(dbToGain(-3.0f));

        process(processor, buffer);

        expectNearBuffer(runner, buffer, expected, 1.0e-4f, "mix 0 outputs dry signal with output gain");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "low_gain", 12.0f);
        setParameter(processor, "mix", 100.0f);
        prepare(processor, 48000.0, 2048);

        auto buffer = makeLowFrequencySignal(2048, 48000.0);
        const auto input = copyOf(buffer);

        process(processor, buffer);

        runner.expect(maxAbsDiff(buffer, input) > 1.0e-3f, "mix 100 applies EQ when a band is boosted");
        runner.expect(allFinite(buffer), "mix 100 boosted processing stays finite");
    }
}

void testOutputGain(Runner& runner)
{
    for (float outputDb : { -6.0f, 0.0f, 6.0f })
    {
        MusiqueEQProcessor processor;
        setParameter(processor, "output", outputDb);
        prepare(processor, 48000.0, 512);

        auto buffer = makeStereoSignal(512, 48000.0);
        auto expected = copyOf(buffer);
        expected.applyGain(dbToGain(outputDb));

        process(processor, buffer);

        expectNearBuffer(runner, buffer, expected, 1.5e-4f,
                         "output gain " + std::to_string(outputDb) + " dB scales neutral signal");
    }
}

void testMonoMode(Runner& runner)
{
    {
        MusiqueEQProcessor processor;
        setParameter(processor, "mono", 1.0f);
        prepare(processor, 48000.0, 512);

        auto buffer = makeStereoSignal(512, 48000.0);
        process(processor, buffer);

        runner.expect(maxChannelDiff(buffer) <= 1.0e-5f, "mono mode makes left and right channels identical");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "mono", 0.0f);
        prepare(processor, 48000.0, 512);

        auto buffer = makeStereoSignal(512, 48000.0);
        process(processor, buffer);

        runner.expect(maxChannelDiff(buffer) > 1.0e-4f, "stereo mode preserves channel differences");
    }
}

void testNumericalStability(Runner& runner)
{
    const std::vector<double> sampleRates { 12000.0, 44100.0, 48000.0, 96000.0, 192000.0 };
    const std::vector<int> blockSizes { 1, 16, 64, 513, 2048 };
    const std::vector<float> qValues { 0.3f, 8.0f };

    for (double sampleRate : sampleRates)
    {
        for (int blockSize : blockSizes)
        {
            for (float q : qValues)
            {
                MusiqueEQProcessor processor;
                setParameter(processor, "low_gain", 24.0f);
                setParameter(processor, "low_mid_gain", -24.0f);
                setParameter(processor, "mid_gain", 24.0f);
                setParameter(processor, "high_mid_gain", -24.0f);
                setParameter(processor, "high_gain", 24.0f);
                setParameter(processor, "q", q);
                setParameter(processor, "hpf_enabled", 1.0f);
                setParameter(processor, "hpf_freq", 20000.0f);
                setParameter(processor, "hpf_slope", 48.0f);
                setParameter(processor, "lpf_enabled", 1.0f);
                setParameter(processor, "lpf_freq", 20.0f);
                setParameter(processor, "lpf_slope", 48.0f);
                prepare(processor, sampleRate, blockSize);

                auto buffer = makeStereoSignal(blockSize, sampleRate, 0.01f);
                process(processor, buffer);

                const std::string suffix = " sr=" + std::to_string(static_cast<int>(sampleRate))
                    + " block=" + std::to_string(blockSize)
                    + " q=" + std::to_string(q);
                runner.expect(allFinite(buffer), "extreme gain/Q processing finite" + suffix);
                runner.expect(maxAbs(buffer) < 512.0f, "extreme gain/Q processing bounded" + suffix);
            }
        }
    }
}

void testInternalTrim(Runner& runner)
{
    {
        MusiqueEQProcessor processor;
        prepare(processor, 48000.0, 512);

        auto buffer = makeStereoSignal(512, 48000.0);
        process(processor, buffer);

        runner.expect(processor.getCurrentInternalTrimDb() <= 0.01f, "SAFE/TRIM neutral state reports 0 dB trim");
    }

    MusiqueEQProcessor processor;
    setAllBandGains(processor, 18.0f);
    setParameter(processor, "q", 8.0f);
    prepare(processor, 48000.0, 512);

    auto aggressiveBuffer = makeStereoSignal(512, 48000.0, 0.005f);
    process(processor, aggressiveBuffer);

    runner.expect(processor.getCurrentInternalTrimDb() > 0.25f, "SAFE/TRIM stacked boosts report positive trim");
    runner.expect(processor.getCurrentInternalTrimDb() <= 12.0f, "SAFE/TRIM stacked boosts stay capped");
    runner.expect(allFinite(aggressiveBuffer), "SAFE/TRIM stacked boosts stay finite");

    setAllBandGains(processor, 0.0f);
    setParameter(processor, "q", 1.0f);

    for (int i = 0; i < 40; ++i)
    {
        auto settlingBuffer = makeStereoSignal(512, 48000.0, 0.005f);
        process(processor, settlingBuffer);
        if (i == 0 || i == 39)
            runner.expect(allFinite(settlingBuffer), "SAFE/TRIM return-to-neutral settling block " + std::to_string(i) + " stays finite");
    }

    runner.expect(processor.getCurrentInternalTrimDb() <= 0.05f, "SAFE/TRIM returns to 0 dB after neutral settings settle");
}

void testFrequencyDefaultsAndState(Runner& runner)
{
    {
        MusiqueEQProcessor processor;

        for (size_t i = 0; i < frequencyParamIds.size(); ++i)
            expectParameterNear(runner, processor, frequencyParamIds[i], legacyFrequencyDefaults[i], 0.001f,
                                std::string("frequency parameter default matches legacy anchor ") + frequencyParamIds[i]);

        expectParameterNear(runner, processor, "hpf_enabled", 0.0f, 0.001f, "HPF default is disabled");
        expectParameterNear(runner, processor, "hpf_freq", 30.0f, 0.001f, "HPF default frequency is 30 Hz");
        expectParameterNear(runner, processor, "hpf_slope", 12.0f, 0.001f, "HPF default slope is 12 dB/oct");
        expectParameterNear(runner, processor, "lpf_enabled", 0.0f, 0.001f, "LPF default is disabled");
        expectParameterNear(runner, processor, "lpf_freq", 18000.0f, 0.01f, "LPF default frequency is 18 kHz");
        expectParameterNear(runner, processor, "lpf_slope", 12.0f, 0.001f, "LPF default slope is 12 dB/oct");
    }

    {
        MusiqueEQProcessor implicitDefaults;
        setParameter(implicitDefaults, "low_gain", 3.0f);
        setParameter(implicitDefaults, "low_mid_gain", -2.0f);
        setParameter(implicitDefaults, "mid_gain", 5.0f);
        setParameter(implicitDefaults, "high_mid_gain", -1.0f);
        setParameter(implicitDefaults, "high_gain", 2.5f);
        setParameter(implicitDefaults, "q", 1.4f);
        prepare(implicitDefaults, 48000.0, 1024);

        MusiqueEQProcessor explicitDefaults;
        setParameter(explicitDefaults, "low_gain", 3.0f);
        setParameter(explicitDefaults, "low_mid_gain", -2.0f);
        setParameter(explicitDefaults, "mid_gain", 5.0f);
        setParameter(explicitDefaults, "high_mid_gain", -1.0f);
        setParameter(explicitDefaults, "high_gain", 2.5f);
        setParameter(explicitDefaults, "q", 1.4f);
        setDefaultBandFrequencies(explicitDefaults);
        prepare(explicitDefaults, 48000.0, 1024);

        auto implicitBuffer = makeBroadbandProbe(1024, 48000.0);
        auto explicitBuffer = copyOf(implicitBuffer);

        process(implicitDefaults, implicitBuffer);
        process(explicitDefaults, explicitBuffer);

        expectNearBuffer(runner, implicitBuffer, explicitBuffer, 1.0e-7f,
                         "legacy default frequencies reproduce the default processing path");
    }

    {
        MusiqueEQProcessor source;
        setParameter(source, "low_gain", -4.0f);
        setParameter(source, "mid_gain", 7.0f);
        setParameter(source, "q", 2.2f);
        setParameter(source, "mix", 73.0f);
        setParameter(source, "output", -3.5f);
        setParameter(source, "mono", 1.0f);
        setParameter(source, "low_freq", 180.0f);
        setParameter(source, "mid_freq", 2400.0f);

        auto legacyState = copyStateTree(source);
        for (auto* id : frequencyParamIds)
            removeParameterFromState(legacyState, id);
        for (auto* id : cutFilterParamIds)
            removeParameterFromState(legacyState, id);

        MusiqueEQProcessor restored;
        loadStateTree(restored, legacyState);

        expectParameterNear(runner, restored, "low_gain", -4.0f, 0.001f, "legacy state restores old low gain");
        expectParameterNear(runner, restored, "mid_gain", 7.0f, 0.001f, "legacy state restores old mid gain");
        expectParameterNear(runner, restored, "q", 2.2f, 0.001f, "legacy state restores old Q");
        expectParameterNear(runner, restored, "mix", 73.0f, 0.001f, "legacy state restores old mix");
        expectParameterNear(runner, restored, "output", -3.5f, 0.001f, "legacy state restores old output");
        expectParameterNear(runner, restored, "mono", 1.0f, 0.001f, "legacy state restores old mono flag");

        for (size_t i = 0; i < frequencyParamIds.size(); ++i)
            expectParameterNear(runner, restored, frequencyParamIds[i], legacyFrequencyDefaults[i], 0.001f,
                                std::string("legacy state fills missing frequency default ") + frequencyParamIds[i]);
        expectParameterNear(runner, restored, "hpf_enabled", 0.0f, 0.001f, "legacy state fills HPF disabled");
        expectParameterNear(runner, restored, "hpf_freq", 30.0f, 0.001f, "legacy state fills HPF frequency");
        expectParameterNear(runner, restored, "hpf_slope", 12.0f, 0.001f, "legacy state fills HPF slope");
        expectParameterNear(runner, restored, "lpf_enabled", 0.0f, 0.001f, "legacy state fills LPF disabled");
        expectParameterNear(runner, restored, "lpf_freq", 18000.0f, 0.01f, "legacy state fills LPF frequency");
        expectParameterNear(runner, restored, "lpf_slope", 12.0f, 0.001f, "legacy state fills LPF slope");
    }

    {
        MusiqueEQProcessor source;
        setParameter(source, "low_freq", 140.0f);
        setParameter(source, "low_mid_freq", 520.0f);
        setParameter(source, "mid_freq", 1800.0f);
        setParameter(source, "high_mid_freq", 6200.0f);
        setParameter(source, "high_freq", 15500.0f);
        setParameter(source, "hpf_enabled", 1.0f);
        setParameter(source, "hpf_freq", 45.0f);
        setParameter(source, "hpf_slope", 24.0f);
        setParameter(source, "lpf_enabled", 1.0f);
        setParameter(source, "lpf_freq", 16000.0f);
        setParameter(source, "lpf_slope", 48.0f);

        auto state = copyStateTree(source);
        MusiqueEQProcessor restored;
        loadStateTree(restored, state);

        expectParameterNear(runner, restored, "low_freq", 140.0f, 0.001f, "state roundtrip restores low frequency");
        expectParameterNear(runner, restored, "low_mid_freq", 520.0f, 0.001f, "state roundtrip restores low-mid frequency");
        expectParameterNear(runner, restored, "mid_freq", 1800.0f, 0.001f, "state roundtrip restores mid frequency");
        expectParameterNear(runner, restored, "high_mid_freq", 6200.0f, 0.001f, "state roundtrip restores high-mid frequency");
        expectParameterNear(runner, restored, "high_freq", 15500.0f, 0.001f, "state roundtrip restores high frequency");
        expectParameterNear(runner, restored, "hpf_enabled", 1.0f, 0.001f, "state roundtrip restores HPF enabled");
        expectParameterNear(runner, restored, "hpf_freq", 45.0f, 0.001f, "state roundtrip restores HPF frequency");
        expectParameterNear(runner, restored, "hpf_slope", 24.0f, 0.001f, "state roundtrip restores HPF slope");
        expectParameterNear(runner, restored, "lpf_enabled", 1.0f, 0.001f, "state roundtrip restores LPF enabled");
        expectParameterNear(runner, restored, "lpf_freq", 16000.0f, 0.001f, "state roundtrip restores LPF frequency");
        expectParameterNear(runner, restored, "lpf_slope", 48.0f, 0.001f, "state roundtrip restores LPF slope");
    }
}

void testFrequencyResponseControls(Runner& runner)
{
    {
        MusiqueEQProcessor lowMid;
        setParameter(lowMid, "mid_gain", 12.0f);
        setParameter(lowMid, "q", 5.0f);
        setParameter(lowMid, "mid_freq", 750.0f);
        prepare(lowMid, 48000.0, 2048);

        MusiqueEQProcessor highMid;
        setParameter(highMid, "mid_gain", 12.0f);
        setParameter(highMid, "q", 5.0f);
        setParameter(highMid, "mid_freq", 4200.0f);
        prepare(highMid, 48000.0, 2048);

        auto lowBuffer = makeBroadbandProbe(2048, 48000.0);
        auto highBuffer = copyOf(lowBuffer);

        process(lowMid, lowBuffer);
        process(highMid, highBuffer);

        runner.expect(maxAbsDiff(lowBuffer, highBuffer) > 1.0e-3f,
                      "changing mid_freq changes the measurable response");
        runner.expect(allFinite(lowBuffer) && allFinite(highBuffer),
                      "mid_freq response variants stay finite");
    }

    {
        MusiqueEQProcessor lowShelfA;
        setParameter(lowShelfA, "low_gain", 12.0f);
        setParameter(lowShelfA, "q", 0.9f);
        setParameter(lowShelfA, "low_freq", 80.0f);
        prepare(lowShelfA, 48000.0, 2048);

        MusiqueEQProcessor lowShelfB;
        setParameter(lowShelfB, "low_gain", 12.0f);
        setParameter(lowShelfB, "q", 0.9f);
        setParameter(lowShelfB, "low_freq", 500.0f);
        prepare(lowShelfB, 48000.0, 2048);

        auto bufferA = makeBroadbandProbe(2048, 48000.0);
        auto bufferB = copyOf(bufferA);

        process(lowShelfA, bufferA);
        process(lowShelfB, bufferB);

        runner.expect(maxAbsDiff(bufferA, bufferB) > 1.0e-3f,
                      "changing low_freq changes the measurable response");
        runner.expect(allFinite(bufferA) && allFinite(bufferB),
                      "low_freq response variants stay finite");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "low_gain", 24.0f);
        setParameter(processor, "low_mid_gain", -24.0f);
        setParameter(processor, "mid_gain", 24.0f);
        setParameter(processor, "high_mid_gain", -24.0f);
        setParameter(processor, "high_gain", 24.0f);
        setParameter(processor, "q", 8.0f);

        for (auto* id : frequencyParamIds)
            setParameter(processor, id, 20000.0f);

        prepare(processor, 12000.0, 513);

        auto buffer = makeBroadbandProbe(513, 12000.0, 0.005f);
        process(processor, buffer);

        runner.expect(allFinite(buffer), "frequency params clamp under Nyquist at 12 kHz sample rate");
        runner.expect(maxAbs(buffer) < 512.0f, "frequency params clamped under Nyquist stay bounded");
    }
}

void testCutFilters(Runner& runner)
{
    {
        MusiqueEQProcessor processor;
        setParameter(processor, "hpf_enabled", 1.0f);
        setParameter(processor, "hpf_freq", 200.0f);
        setParameter(processor, "hpf_slope", 48.0f);
        setParameter(processor, "lpf_enabled", 1.0f);
        setParameter(processor, "lpf_freq", 6000.0f);
        setParameter(processor, "lpf_slope", 48.0f);
        setParameter(processor, "mix", 0.0f);
        setParameter(processor, "output", -6.0f);
        prepare(processor, 48000.0, 2048);

        auto buffer = makeBroadbandProbe(2048, 48000.0);
        auto expected = copyOf(buffer);
        expected.applyGain(dbToGain(-6.0f));

        process(processor, buffer);

        expectNearBuffer(runner, buffer, expected, 1.0e-4f, "mix 0 bypasses HPF/LPF wet path and keeps output gain");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "hpf_enabled", 1.0f);
        setParameter(processor, "hpf_freq", 300.0f);
        setParameter(processor, "hpf_slope", 48.0f);
        prepare(processor, 48000.0, 4096);

        auto low = makeStereoSine(4096, 48000.0, 40.0);
        auto high = makeStereoSine(4096, 48000.0, 1000.0);
        const auto lowInput = copyOf(low);
        const auto highInput = copyOf(high);

        process(processor, low);
        process(processor, high);

        const float lowRatio = rmsFrom(low, 1024) / rmsFrom(lowInput, 1024);
        const float highRatio = rmsFrom(high, 1024) / rmsFrom(highInput, 1024);
        runner.expect(lowRatio < 0.08f, "HPF strongly attenuates low-frequency content");
        runner.expect(highRatio > 0.80f, "HPF preserves high-frequency content");
        runner.expect(allFinite(low) && allFinite(high), "HPF response test stays finite");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "lpf_enabled", 1.0f);
        setParameter(processor, "lpf_freq", 3000.0f);
        setParameter(processor, "lpf_slope", 48.0f);
        prepare(processor, 48000.0, 4096);

        auto low = makeStereoSine(4096, 48000.0, 500.0);
        auto high = makeStereoSine(4096, 48000.0, 12000.0);
        const auto lowInput = copyOf(low);
        const auto highInput = copyOf(high);

        process(processor, low);
        process(processor, high);

        const float lowRatio = rmsFrom(low, 1024) / rmsFrom(lowInput, 1024);
        const float highRatio = rmsFrom(high, 1024) / rmsFrom(highInput, 1024);
        runner.expect(lowRatio > 0.80f, "LPF preserves low-frequency content");
        runner.expect(highRatio < 0.12f, "LPF strongly attenuates high-frequency content");
        runner.expect(allFinite(low) && allFinite(high), "LPF response test stays finite");
    }

    {
        float ratios[3] {};
        const float slopes[3] { 12.0f, 24.0f, 48.0f };

        for (int i = 0; i < 3; ++i)
        {
            MusiqueEQProcessor processor;
            setParameter(processor, "hpf_enabled", 1.0f);
            setParameter(processor, "hpf_freq", 300.0f);
            setParameter(processor, "hpf_slope", slopes[i]);
            prepare(processor, 48000.0, 4096);

            auto buffer = makeStereoSine(4096, 48000.0, 60.0);
            const auto input = copyOf(buffer);
            process(processor, buffer);
            ratios[i] = rmsFrom(buffer, 1024) / rmsFrom(input, 1024);
            runner.expect(allFinite(buffer), "HPF slope " + std::to_string(static_cast<int>(slopes[i])) + " stays finite");
        }

        runner.expect(ratios[2] < ratios[1] && ratios[1] < ratios[0],
                      "HPF 48 dB/oct attenuates more than 24, which attenuates more than 12");
    }

    {
        float ratios[3] {};
        const float slopes[3] { 12.0f, 24.0f, 48.0f };

        for (int i = 0; i < 3; ++i)
        {
            MusiqueEQProcessor processor;
            setParameter(processor, "lpf_enabled", 1.0f);
            setParameter(processor, "lpf_freq", 3000.0f);
            setParameter(processor, "lpf_slope", slopes[i]);
            prepare(processor, 48000.0, 4096);

            auto buffer = makeStereoSine(4096, 48000.0, 12000.0);
            const auto input = copyOf(buffer);
            process(processor, buffer);
            ratios[i] = rmsFrom(buffer, 1024) / rmsFrom(input, 1024);
            runner.expect(allFinite(buffer), "LPF slope " + std::to_string(static_cast<int>(slopes[i])) + " stays finite");
        }

        runner.expect(ratios[2] < ratios[1] && ratios[1] < ratios[0],
                      "LPF 48 dB/oct attenuates more than 24, which attenuates more than 12");
    }
}

void testImpulseFftFrequencyResponse(Runner& runner)
{
    constexpr double sampleRate = 48000.0;

    {
        MusiqueEQProcessor processor;
        const auto response = renderImpulseResponse(processor, sampleRate);
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 60.0), -0.15f, 0.15f,
                      "FFT impulse neutral response is flat at 60 Hz");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 1000.0), -0.15f, 0.15f,
                      "FFT impulse neutral response is flat at 1 kHz");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 12000.0), -0.15f, 0.15f,
                      "FFT impulse neutral response is flat at 12 kHz");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "mid_gain", 6.0f);
        setParameter(processor, "mid_freq", 1200.0f);
        setParameter(processor, "q", 1.2f);
        const auto response = renderImpulseResponse(processor, sampleRate);

        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 1200.0), 5.0f, 7.5f,
                      "FFT impulse bell +6 dB near 1200 Hz reaches target range");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 120.0), -0.75f, 0.75f,
                      "FFT impulse bell +6 dB leaves low edge near unity");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 12000.0), -0.75f, 0.75f,
                      "FFT impulse bell +6 dB leaves high edge near unity");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "low_gain", 6.0f);
        setParameter(processor, "low_freq", 100.0f);
        setParameter(processor, "q", 1.0f);
        const auto response = renderImpulseResponse(processor, sampleRate);

        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 50.0), 4.5f, 7.5f,
                      "FFT impulse low shelf +6 dB boosts 50 Hz");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 2000.0), -0.75f, 0.75f,
                      "FFT impulse low shelf +6 dB leaves 2 kHz near unity");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "hpf_enabled", 1.0f);
        setParameter(processor, "hpf_freq", 300.0f);
        setParameter(processor, "hpf_slope", 48.0f);
        const auto response = renderImpulseResponse(processor, sampleRate);

        runner.expect(fftMagnitudeDbAt(response, sampleRate, 60.0) <= -40.0f,
                      "FFT impulse HPF 48 dB/oct at 300 Hz attenuates 60 Hz by at least 40 dB");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 1000.0), -1.0f, 1.0f,
                      "FFT impulse HPF 48 dB/oct at 300 Hz preserves 1 kHz near unity");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "lpf_enabled", 1.0f);
        setParameter(processor, "lpf_freq", 3000.0f);
        setParameter(processor, "lpf_slope", 48.0f);
        const auto response = renderImpulseResponse(processor, sampleRate);

        runner.expect(fftMagnitudeDbAt(response, sampleRate, 12000.0) <= -40.0f,
                      "FFT impulse LPF 48 dB/oct at 3 kHz attenuates 12 kHz by at least 40 dB");
        expectInRange(runner, fftMagnitudeDbAt(response, sampleRate, 500.0), -1.0f, 1.0f,
                      "FFT impulse LPF 48 dB/oct at 3 kHz preserves 500 Hz near unity");
    }
}

void testLatencyAndImpulsePosition(Runner& runner)
{
    {
        MusiqueEQProcessor processor;
        runner.expect(processor.getLatencySamples() == 0, "reported plugin latency is zero samples");

        const auto response = renderImpulseResponse(processor, 48000.0);
        runner.expect(firstSignificantSample(response) == 0,
                      "neutral impulse first significant sample stays at index 0");
        runner.expect(std::abs(response.getSample(0, 0) - 1.0f) <= 1.0e-5f
                        && std::abs(response.getSample(1, 0) - 1.0f) <= 1.0e-5f,
                      "neutral impulse preserves sample 0 amplitude");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "bypass", 1.0f);

        const auto response = renderImpulseResponse(processor, 48000.0);
        runner.expect(firstSignificantSample(response) == 0,
                      "bypass impulse first significant sample stays at index 0");
        runner.expect(std::abs(response.getSample(0, 0) - 1.0f) <= 1.0e-5f
                        && std::abs(response.getSample(1, 0) - 1.0f) <= 1.0e-5f,
                      "bypass impulse preserves sample 0 amplitude");
    }

    {
        MusiqueEQProcessor processor;
        setParameter(processor, "mid_gain", 6.0f);
        setParameter(processor, "mid_freq", 1200.0f);
        setParameter(processor, "q", 2.0f);
        setParameter(processor, "hpf_enabled", 1.0f);
        setParameter(processor, "hpf_freq", 300.0f);
        setParameter(processor, "hpf_slope", 24.0f);
        setParameter(processor, "lpf_enabled", 1.0f);
        setParameter(processor, "lpf_freq", 6000.0f);
        setParameter(processor, "lpf_slope", 24.0f);

        const auto response = renderImpulseResponse(processor, 48000.0);
        runner.expect(firstSignificantSample(response) == 0,
                      "EQ plus HPF/LPF impulse has no pre-delay before sample 0");
        runner.expect(allFinite(response), "EQ plus HPF/LPF impulse response stays finite");
    }
}

void testSampleRateChangeStability(Runner& runner)
{
    MusiqueEQProcessor processor;
    setParameter(processor, "low_gain", 18.0f);
    setParameter(processor, "low_mid_gain", -18.0f);
    setParameter(processor, "mid_gain", 24.0f);
    setParameter(processor, "high_mid_gain", -21.0f);
    setParameter(processor, "high_gain", 15.0f);
    setParameter(processor, "q", 7.5f);
    setParameter(processor, "low_freq", 40.0f);
    setParameter(processor, "low_mid_freq", 180.0f);
    setParameter(processor, "mid_freq", 2200.0f);
    setParameter(processor, "high_mid_freq", 9000.0f);
    setParameter(processor, "high_freq", 18000.0f);
    setParameter(processor, "hpf_enabled", 1.0f);
    setParameter(processor, "hpf_freq", 30.0f);
    setParameter(processor, "hpf_slope", 48.0f);
    setParameter(processor, "lpf_enabled", 1.0f);
    setParameter(processor, "lpf_freq", 18000.0f);
    setParameter(processor, "lpf_slope", 48.0f);

    const std::array<double, 4> sampleRates {{ 44100.0, 96000.0, 12000.0, 192000.0 }};
    const std::array<int, 4> blockSizes {{ 257, 512, 129, 1024 }};

    for (size_t i = 0; i < sampleRates.size(); ++i)
    {
        const auto sampleRate = sampleRates[i];
        const auto blockSize = blockSizes[i];
        prepare(processor, sampleRate, blockSize);

        bool finite = true;
        float peak = 0.0f;
        for (int block = 0; block < 24; ++block)
        {
            auto buffer = makeBroadbandProbe(blockSize, sampleRate, 0.006f);
            process(processor, buffer);
            finite = finite && allFinite(buffer);
            peak = juce::jmax(peak, maxAbs(buffer));
        }

        const std::string suffix = " at " + std::to_string(static_cast<int>(sampleRate)) + " Hz";
        runner.expect(std::abs(processor.getPreparedSampleRate() - sampleRate) <= 1.0e-6,
                      "sample-rate change updates prepared sample rate" + suffix);
        runner.expect(finite, "sample-rate change aggressive processing stays finite" + suffix);
        runner.expect(peak < 64.0f, "sample-rate change aggressive processing stays bounded" + suffix
                                + " (peak " + std::to_string(peak) + ")");
        runner.expect(processor.getCurrentInternalTrimDb() <= 12.01f,
                      "sample-rate change internal trim remains capped" + suffix);
    }
}

void testExtremeCoefficientMovement(Runner& runner)
{
    MusiqueEQProcessor processor;
    setParameter(processor, "mid_freq", 1200.0f);
    prepare(processor, 48000.0, 64);
    const auto neutral = processor.getTestCoefficientSnapshots();

    setParameter(processor, "mid_gain", 24.0f);
    settleSmoothing(processor);
    const auto plusGain = processor.getTestCoefficientSnapshots();

    setParameter(processor, "mid_gain", -24.0f);
    settleSmoothing(processor);
    const auto minusGain = processor.getTestCoefficientSnapshots();

    setParameter(processor, "mid_gain", 12.0f);
    setParameter(processor, "q", 0.3f);
    settleSmoothing(processor);
    const auto wideQ = processor.getTestCoefficientSnapshots();

    setParameter(processor, "q", 8.0f);
    settleSmoothing(processor);
    const auto narrowQ = processor.getTestCoefficientSnapshots();

    runner.expect(coefficientsFinite(neutral.eq[2])
                    && coefficientsFinite(plusGain.eq[2])
                    && coefficientsFinite(minusGain.eq[2])
                    && coefficientsFinite(wideQ.eq[2])
                    && coefficientsFinite(narrowQ.eq[2]),
                  "extreme gain/Q coefficient snapshots stay finite for center band");
    runner.expect(coefficientDistance(neutral.eq[2], plusGain.eq[2]) > 0.01f,
                  "center band coefficients move measurably for +24 dB gain");
    runner.expect(coefficientDistance(plusGain.eq[2], minusGain.eq[2]) > 0.05f,
                  "center band coefficients move measurably between +24 dB and -24 dB");
    runner.expect(coefficientDistance(wideQ.eq[2], narrowQ.eq[2]) > 0.01f,
                  "center band coefficients move measurably between Q 0.3 and Q 8.0");

    setParameter(processor, "hpf_enabled", 1.0f);
    setParameter(processor, "hpf_freq", 300.0f);
    setParameter(processor, "hpf_slope", 48.0f);
    setParameter(processor, "lpf_enabled", 1.0f);
    setParameter(processor, "lpf_freq", 3000.0f);
    setParameter(processor, "lpf_slope", 48.0f);
    settleSmoothing(processor);
    const auto cut = processor.getTestCoefficientSnapshots();

    runner.expect(cut.activeHpfStages == 4 && cut.activeLpfStages == 4,
                  "HPF/LPF 48 dB/oct coefficient snapshots expose four active stages each");
    bool cutFinite = true;
    for (int stage = 0; stage < 4; ++stage)
        cutFinite = cutFinite && coefficientsFinite(cut.hpf[(size_t) stage]) && coefficientsFinite(cut.lpf[(size_t) stage]);

    runner.expect(cutFinite, "HPF/LPF extreme coefficient snapshots stay finite");
}

void testGoldenAudioReference(Runner& runner)
{
    const auto goldenFile = findGoldenReferenceForTests();
    runner.expect(goldenFile.existsAsFile(), "golden audio JSON is discoverable");
    if (! goldenFile.existsAsFile())
        return;

    GoldenAudioSignature expected;
    runner.expect(loadGoldenAudioSignature(goldenFile, expected), "golden audio JSON parses with version 1 signature");
    if (expected.version != 1 || expected.tapIndices.empty())
        return;

    const auto actual = renderGoldenAudioSignature();
    runner.expect(expected.sampleRate == actual.sampleRate
                    && expected.blockSize == actual.blockSize
                    && expected.frames == actual.frames,
                  "golden audio render dimensions match reference metadata");
    runner.expect(expected.tapIndices == actual.tapIndices,
                  "golden audio tap index list matches reference metadata");

    const auto tapCount = juce::jmin(expected.leftTaps.size(), actual.leftTaps.size());
    runner.expect(expected.leftTaps.size() == actual.leftTaps.size()
                    && expected.rightTaps.size() == actual.rightTaps.size(),
                  "golden audio tap counts match reference");

    for (size_t i = 0; i < tapCount; ++i)
    {
        runner.expect(std::abs(expected.leftTaps[i] - actual.leftTaps[i]) <= 2.0e-4f,
                      "golden audio left tap " + std::to_string(i) + " matches reference");
        runner.expect(std::abs(expected.rightTaps[i] - actual.rightTaps[i]) <= 2.0e-4f,
                      "golden audio right tap " + std::to_string(i) + " matches reference");
    }

    runner.expect(std::abs(expected.rmsLeft - actual.rmsLeft) <= 1.0e-4f,
                  "golden audio left RMS matches reference");
    runner.expect(std::abs(expected.rmsRight - actual.rmsRight) <= 1.0e-4f,
                  "golden audio right RMS matches reference");
    runner.expect(std::abs(expected.peak - actual.peak) <= 1.0e-4f,
                  "golden audio peak matches reference");
    runner.expect(std::abs(expected.trimDb - actual.trimDb) <= 0.05f,
                  "golden audio trim dB matches reference");
}

void testRapidAutomation(Runner& runner)
{
    MusiqueEQProcessor processor;
    prepare(processor, 48000.0, 2048);

    const int blockSizes[] = { 1, 16, 64, 257, 513, 1024, 2048 };

    for (int block = 0; block < 96; ++block)
    {
        const float direction = (block % 2 == 0) ? 1.0f : -1.0f;
        setParameter(processor, "low_gain", direction * (block % 25));
        setParameter(processor, "low_mid_gain", -direction * ((block * 3) % 25));
        setParameter(processor, "mid_gain", direction * ((block * 5) % 25));
        setParameter(processor, "high_mid_gain", -direction * ((block * 7) % 25));
        setParameter(processor, "high_gain", direction * ((block * 11) % 25));
        setParameter(processor, "q", 0.3f + static_cast<float>(block % 78) * 0.1f);
        setParameter(processor, "low_freq", 60.0f + static_cast<float>((block * 17) % 440));
        setParameter(processor, "mid_freq", 250.0f + static_cast<float>((block * 131) % 5000));
        setParameter(processor, "high_freq", 4000.0f + static_cast<float>((block * 277) % 12000));
        setParameter(processor, "hpf_enabled", (block % 5 == 0) ? 1.0f : 0.0f);
        setParameter(processor, "hpf_freq", 20.0f + static_cast<float>((block * 19) % 800));
        setParameter(processor, "hpf_slope", (block % 3 == 0) ? 12.0f : (block % 3 == 1 ? 24.0f : 48.0f));
        setParameter(processor, "lpf_enabled", (block % 7 == 0) ? 1.0f : 0.0f);
        setParameter(processor, "lpf_freq", 2000.0f + static_cast<float>((block * 431) % 18000));
        setParameter(processor, "lpf_slope", (block % 3 == 0) ? 48.0f : (block % 3 == 1 ? 24.0f : 12.0f));
        setParameter(processor, "mix", static_cast<float>((block * 13) % 101));
        setParameter(processor, "output", -12.0f + static_cast<float>((block * 5) % 25));
        setParameter(processor, "bypass", (block % 17 == 0) ? 1.0f : 0.0f);

        const int blockSize = blockSizes[block % std::size(blockSizes)];
        auto buffer = makeStereoSignal(blockSize, 48000.0, 0.015f);
        process(processor, buffer);

        runner.expect(allFinite(buffer), "rapid automation block " + std::to_string(block) + " stays finite");
        runner.expect(maxAbs(buffer) < 512.0f, "rapid automation block " + std::to_string(block) + " stays bounded");
    }
}

void testAutomationTransient(Runner& runner)
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 64;
    constexpr int numBlocks = 160;

    MusiqueEQProcessor processor;
    setParameter(processor, "mix", 100.0f);
    setParameter(processor, "output", -9.0f);
    setParameter(processor, "bypass", 0.0f);
    prepare(processor, sampleRate, blockSize);

    bool finite = true;
    float peak = 0.0f;
    float maxIntraBlockJump = 0.0f;
    float maxBoundaryJump = 0.0f;
    float previousLastLeft = 0.0f;
    float previousLastRight = 0.0f;
    bool hasPreviousBlock = false;
    int64_t frameOffset = 0;

    for (int block = 0; block < numBlocks; ++block)
    {
        const bool highState = (block % 2) == 0;
        const float gainSign = highState ? 1.0f : -1.0f;
        setParameter(processor, "low_gain", gainSign * 18.0f);
        setParameter(processor, "low_mid_gain", -gainSign * 14.0f);
        setParameter(processor, "mid_gain", gainSign * 20.0f);
        setParameter(processor, "high_mid_gain", -gainSign * 16.0f);
        setParameter(processor, "high_gain", gainSign * 12.0f);
        setParameter(processor, "q", highState ? 7.5f : 0.35f);
        setParameter(processor, "low_freq", highState ? 55.0f : 650.0f);
        setParameter(processor, "low_mid_freq", highState ? 180.0f : 1400.0f);
        setParameter(processor, "mid_freq", highState ? 450.0f : 7000.0f);
        setParameter(processor, "high_mid_freq", highState ? 1500.0f : 12000.0f);
        setParameter(processor, "high_freq", highState ? 5000.0f : 18000.0f);
        setParameter(processor, "hpf_enabled", 1.0f);
        setParameter(processor, "hpf_freq", highState ? 25.0f : 600.0f);
        setParameter(processor, "hpf_slope", block % 3 == 0 ? 12.0f : (block % 3 == 1 ? 24.0f : 48.0f));
        setParameter(processor, "lpf_enabled", 1.0f);
        setParameter(processor, "lpf_freq", highState ? 18000.0f : 4000.0f);
        setParameter(processor, "lpf_slope", block % 3 == 0 ? 48.0f : (block % 3 == 1 ? 24.0f : 12.0f));

        juce::AudioBuffer<float> buffer(2, blockSize);
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const double t = static_cast<double>(frameOffset + sample) / sampleRate;
            const auto left = static_cast<float>(0.012
                * (std::sin(2.0 * pi * 110.0 * t)
                   + 0.6 * std::sin(2.0 * pi * 1730.0 * t)
                   + 0.25 * std::sin(2.0 * pi * 9100.0 * t)));
            const auto right = static_cast<float>(0.012
                * (std::sin(2.0 * pi * 220.0 * t + 0.2)
                   + 0.5 * std::sin(2.0 * pi * 5300.0 * t)
                   + 0.20 * std::sin(2.0 * pi * 13000.0 * t)));
            buffer.setSample(0, sample, left);
            buffer.setSample(1, sample, right);
        }
        frameOffset += blockSize;

        process(processor, buffer);

        finite = finite && allFinite(buffer);
        peak = juce::jmax(peak, maxAbs(buffer));
        maxIntraBlockJump = juce::jmax(maxIntraBlockJump, maxAdjacentDiff(buffer));
        if (hasPreviousBlock)
        {
            maxBoundaryJump = juce::jmax(maxBoundaryJump, std::abs(buffer.getSample(0, 0) - previousLastLeft));
            maxBoundaryJump = juce::jmax(maxBoundaryJump, std::abs(buffer.getSample(1, 0) - previousLastRight));
        }

        previousLastLeft = buffer.getSample(0, blockSize - 1);
        previousLastRight = buffer.getSample(1, blockSize - 1);
        hasPreviousBlock = true;
    }

    runner.expect(finite, "automation transient remains finite across abrupt gain/frequency/Q/HPF/LPF changes");
    runner.expect(peak < 32.0f, "automation transient peak remains bounded (peak " + std::to_string(peak) + ")");
    runner.expect(maxIntraBlockJump < 1.0f,
                  "automation transient has no abnormal intra-block jump (max " + std::to_string(maxIntraBlockJump) + ")");
    runner.expect(maxBoundaryJump < 1.0f,
                  "automation transient has no abnormal block-boundary jump (max " + std::to_string(maxBoundaryJump) + ")");
}

void runCpuBenchmark()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 256;
    constexpr int warmupBlocks = 64;
    constexpr int measuredBlocks = 768;

    MusiqueEQProcessor processor;
    setParameter(processor, "low_gain", 3.0f);
    setParameter(processor, "low_mid_gain", -2.0f);
    setParameter(processor, "mid_gain", 4.0f);
    setParameter(processor, "high_mid_gain", -1.5f);
    setParameter(processor, "high_gain", 2.0f);
    setParameter(processor, "q", 1.8f);
    setParameter(processor, "low_freq", 90.0f);
    setParameter(processor, "low_mid_freq", 420.0f);
    setParameter(processor, "mid_freq", 1800.0f);
    setParameter(processor, "high_mid_freq", 6200.0f);
    setParameter(processor, "high_freq", 12000.0f);
    setParameter(processor, "hpf_enabled", 1.0f);
    setParameter(processor, "hpf_freq", 45.0f);
    setParameter(processor, "hpf_slope", 24.0f);
    setParameter(processor, "lpf_enabled", 1.0f);
    setParameter(processor, "lpf_freq", 17500.0f);
    setParameter(processor, "lpf_slope", 24.0f);
    prepare(processor, sampleRate, blockSize);

    std::vector<juce::AudioBuffer<float>> measuredBuffers;
    measuredBuffers.reserve(static_cast<size_t>(measuredBlocks));
    for (int block = 0; block < measuredBlocks; ++block)
    {
        measuredBuffers.emplace_back(2, blockSize);
        auto& blockBuffer = measuredBuffers.back();
        const int frameOffset = block * blockSize;
        for (int sample = 0; sample < blockSize; ++sample)
        {
            const double t = static_cast<double>(frameOffset + sample) / sampleRate;
            const auto left = static_cast<float>(0.01
                * (std::sin(2.0 * pi * 120.0 * t)
                   + 0.8 * std::sin(2.0 * pi * 750.0 * t)
                   + 0.6 * std::sin(2.0 * pi * 2400.0 * t)
                   + 0.4 * std::sin(2.0 * pi * 6800.0 * t)));
            const auto right = static_cast<float>(0.01
                * (std::sin(2.0 * pi * 180.0 * t + 0.11)
                   + 0.7 * std::sin(2.0 * pi * 950.0 * t)
                   + 0.5 * std::sin(2.0 * pi * 3200.0 * t)
                   + 0.3 * std::sin(2.0 * pi * 9100.0 * t)));
            blockBuffer.setSample(0, sample, left);
            blockBuffer.setSample(1, sample, right);
        }
    }

    juce::MidiBuffer midi;

    for (int i = 0; i < warmupBlocks; ++i)
    {
        auto warmupBuffer = makeBroadbandProbe(blockSize, sampleRate, 0.01f);
        processor.processBlock(warmupBuffer, midi);
    }

    const auto start = std::chrono::steady_clock::now();
    for (auto& blockBuffer : measuredBuffers)
        processor.processBlock(blockBuffer, midi);
    const auto end = std::chrono::steady_clock::now();

    float peak = 0.0f;
    for (const auto& blockBuffer : measuredBuffers)
        peak = juce::jmax(peak, maxAbs(blockBuffer));

    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    const double nsPerSample = static_cast<double>(elapsedNs)
        / static_cast<double>(measuredBlocks * blockSize * 2);
    std::cout << "[BENCH] eq processBlock blocks=" << measuredBlocks
              << " blockSize=" << blockSize
              << " ns/sample=" << nsPerSample
              << " peak=" << peak << '\n';
}

void testBypassTransparency(Runner& runner)
{
    MusiqueEQProcessor processor;
    setParameter(processor, "bypass", 1.0f);
    setParameter(processor, "mono", 1.0f);
    setParameter(processor, "output", -6.0f);
    setParameter(processor, "hpf_enabled", 1.0f);
    setParameter(processor, "hpf_freq", 400.0f);
    setParameter(processor, "hpf_slope", 48.0f);
    setParameter(processor, "lpf_enabled", 1.0f);
    setParameter(processor, "lpf_freq", 4000.0f);
    setParameter(processor, "lpf_slope", 48.0f);
    prepare(processor, 48000.0, 512);

    auto buffer = makeStereoSignal(512, 48000.0);
    const auto input = copyOf(buffer);

    process(processor, buffer);

    expectNearBuffer(runner, buffer, input, 0.0f, "bypass is transparent even when mono/output/HPF/LPF are set");
    runner.expect(processor.getCurrentInternalTrimDb() <= 0.01f, "bypass forces internal trim to 0 dB");
}
} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInitialiser;

    if (argc == 3 && std::string(argv[1]) == "--write-eq-golden")
    {
        const juce::File outputFile(argv[2]);
        if (! writeGoldenAudioReference(outputFile))
        {
            std::cerr << "Failed to write EQ golden reference: " << outputFile.getFullPathName() << '\n';
            return 2;
        }

        std::cout << "[GOLDEN] wrote " << outputFile.getFullPathName() << '\n';
        return 0;
    }

    if (argc != 1)
    {
        std::cerr << "Usage: MusiqueEQDSPTests [--write-eq-golden <path>]\n";
        return 2;
    }

    Runner runner;

    testGraphMappingHelpers(runner);
    testGraphUiHelpers(runner);
    testPresetMigrationHelper(runner);
    testFactoryPresetBank(runner);
    testEmbeddedFactoryPresetBank(runner);
    testNeutrality(runner);
    testMixModes(runner);
    testOutputGain(runner);
    testMonoMode(runner);
    testNumericalStability(runner);
    testInternalTrim(runner);
    testFrequencyDefaultsAndState(runner);
    testFrequencyResponseControls(runner);
    testCutFilters(runner);
    testImpulseFftFrequencyResponse(runner);
    testLatencyAndImpulsePosition(runner);
    testSampleRateChangeStability(runner);
    testExtremeCoefficientMovement(runner);
    testGoldenAudioReference(runner);
    testRapidAutomation(runner);
    testAutomationTransient(runner);
    testBypassTransparency(runner);
    runCpuBenchmark();

    std::cout << "[SUMMARY] checks=" << runner.checks << " failures=" << runner.failures << '\n';
    return runner.failures == 0 ? 0 : 1;
}
