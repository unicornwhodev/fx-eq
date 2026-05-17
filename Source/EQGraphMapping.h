#pragma once

#include <algorithm>
#include <cmath>

namespace musique::eq::graph
{
constexpr float minFrequencyHz = 20.0f;
constexpr float maxFrequencyHz = 20000.0f;
constexpr float minGainDb = -24.0f;
constexpr float maxGainDb = 24.0f;
constexpr float gainScale = 0.42f;

inline float clampFinite(float value, float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

inline float getMaxGraphFrequency(double sampleRate) noexcept
{
    const auto safeSampleRate = std::isfinite(sampleRate) ? static_cast<float>(sampleRate) : 44100.0f;
    return std::clamp(safeSampleRate * 0.45f, minFrequencyHz, maxFrequencyHz);
}

inline float frequencyToNormalised(float frequency, double sampleRate) noexcept
{
    const float maxFrequency = getMaxGraphFrequency(sampleRate);
    const float safeFrequency = std::clamp(clampFinite(frequency, minFrequencyHz), minFrequencyHz, maxFrequency);
    const float denominator = std::log10(maxFrequency / minFrequencyHz);

    if (denominator <= 0.0f)
        return 0.0f;

    return std::clamp(std::log10(safeFrequency / minFrequencyHz) / denominator, 0.0f, 1.0f);
}

inline float normalisedToFrequency(float normalised, double sampleRate) noexcept
{
    const float maxFrequency = getMaxGraphFrequency(sampleRate);
    const float safeNormalised = std::clamp(clampFinite(normalised, 0.0f), 0.0f, 1.0f);
    return minFrequencyHz * std::pow(maxFrequency / minFrequencyHz, safeNormalised);
}

inline float frequencyToX(float frequency, float left, float width, double sampleRate) noexcept
{
    return left + frequencyToNormalised(frequency, sampleRate) * width;
}

inline float xToFrequency(float x, float left, float width, double sampleRate) noexcept
{
    if (width <= 0.0f)
        return minFrequencyHz;

    const float normalised = (x - left) / width;
    return normalisedToFrequency(normalised, sampleRate);
}

inline float gainToY(float gainDb, float midY, float height) noexcept
{
    const float safeGain = std::clamp(clampFinite(gainDb, 0.0f), minGainDb, maxGainDb);
    return midY - safeGain / maxGainDb * (height * gainScale);
}

inline float yToGain(float y, float midY, float height) noexcept
{
    if (height <= 0.0f)
        return 0.0f;

    const float gain = (midY - y) / (height * gainScale) * maxGainDb;
    return std::clamp(clampFinite(gain, 0.0f), minGainDb, maxGainDb);
}

inline int snapSlopeDbPerOct(float slopeDbPerOct) noexcept
{
    if (! std::isfinite(slopeDbPerOct))
        return 12;

    if (slopeDbPerOct < 18.0f)
        return 12;

    if (slopeDbPerOct < 36.0f)
        return 24;

    return 48;
}
} // namespace musique::eq::graph
