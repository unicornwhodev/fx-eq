#pragma once

#include <JuceHeader.h>
#include <array>

namespace musique::eq::presets
{
inline constexpr std::array<const char*, 26> requiredPresetParameterIds {{
    "low_gain", "low_mid_gain", "mid_gain", "high_mid_gain", "high_gain",
    "low_freq", "low_mid_freq", "mid_freq", "high_mid_freq", "high_freq",
    "q", "mix", "output", "bypass", "mono",
    "hpf_enabled", "hpf_freq", "hpf_slope",
    "lpf_enabled", "lpf_freq", "lpf_slope",
    "low_q", "low_mid_q", "mid_q", "high_mid_q", "high_q"
}};

inline constexpr std::array<const char*, 5> legacyFrequencyParamIds {{
    "low_freq", "low_mid_freq", "mid_freq", "high_mid_freq", "high_freq"
}};

inline constexpr std::array<float, 5> legacyFrequencyDefaults {{
    100.0f, 350.0f, 1200.0f, 4500.0f, 10000.0f
}};

inline constexpr std::array<const char*, 5> futureBandQParamIds {{
    "low_q", "low_mid_q", "mid_q", "high_mid_q", "high_q"
}};

inline void ensureProperty(juce::DynamicObject& object, const char* propertyId, const juce::var& defaultValue)
{
    if (! object.hasProperty(propertyId))
        object.setProperty(propertyId, defaultValue);
}

inline juce::var migratePresetForCurrentParameters(const juce::var& preset)
{
    auto* source = preset.getDynamicObject();
    if (source == nullptr)
        return preset;

    juce::DynamicObject::Ptr migrated = new juce::DynamicObject();
    const auto& properties = source->getProperties();

    for (int i = 0; i < properties.size(); ++i)
    {
        const auto name = properties.getName(i);
        migrated->setProperty(name, source->getProperty(name));
    }

    for (size_t i = 0; i < legacyFrequencyParamIds.size(); ++i)
        ensureProperty(*migrated, legacyFrequencyParamIds[i], legacyFrequencyDefaults[i]);

    ensureProperty(*migrated, "hpf_enabled", false);
    ensureProperty(*migrated, "hpf_freq", 30.0f);
    ensureProperty(*migrated, "hpf_slope", 12.0f);
    ensureProperty(*migrated, "lpf_enabled", false);
    ensureProperty(*migrated, "lpf_freq", 18000.0f);
    ensureProperty(*migrated, "lpf_slope", 12.0f);

    const auto qValue = migrated->hasProperty("q") ? migrated->getProperty("q") : juce::var(1.0f);
    for (auto* id : futureBandQParamIds)
        ensureProperty(*migrated, id, qValue);

    return juce::var(migrated.get());
}
} // namespace musique::eq::presets
