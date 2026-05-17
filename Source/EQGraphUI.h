#pragma once

#include <JuceHeader.h>
#include "FXTokens.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace musique::eq::ui
{
constexpr float graphPlotPad = 24.0f;
constexpr float selectedPanelX = 714.0f;
constexpr float selectedPanelRightPad = 18.0f;
constexpr float selectedPanelMinWidth = 260.0f;
constexpr float eqHandleHitRadius = 22.0f;
constexpr float cutHandleHitRadius = 18.0f;

inline juce::Rectangle<float> visualBounds(float editorWidth) noexcept
{
    return { 0.0f, static_cast<float>(fx::dim::headerH + fx::dim::presetBarH),
             editorWidth, static_cast<float>(fx::dim::visualH) };
}

inline juce::Rectangle<float> graphPlotBounds(float editorWidth) noexcept
{
    const auto area = visualBounds(editorWidth);
    return { area.getX() + graphPlotPad, area.getY(),
             area.getWidth() - 2.0f * graphPlotPad, area.getHeight() };
}

inline juce::Rectangle<float> selectionPanelBounds(float editorWidth) noexcept
{
    const float width = std::max(selectedPanelMinWidth, editorWidth - selectedPanelX - selectedPanelRightPad);
    return { selectedPanelX, static_cast<float>(fx::dim::headerH + 11),
             width, static_cast<float>(fx::dim::btnH) };
}

inline juce::Rectangle<float> cutToggleBounds(juce::Rectangle<float> panel) noexcept
{
    return { panel.getX() + 136.0f, panel.getY() + 4.0f, 43.0f, 20.0f };
}

inline std::array<juce::Rectangle<float>, 3> slopeButtonBounds(juce::Rectangle<float> panel) noexcept
{
    const float x = panel.getX() + 186.0f;
    const float y = panel.getY() + 5.0f;

    return {{
        { x, y, 30.0f, 18.0f },
        { x + 34.0f, y, 30.0f, 18.0f },
        { x + 68.0f, y, 30.0f, 18.0f }
    }};
}

inline bool overlaps(juce::Rectangle<float> a, juce::Rectangle<float> b) noexcept
{
    return a.intersects(b);
}

inline float distanceSquared(juce::Point<float> a, juce::Point<float> b) noexcept
{
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return dx * dx + dy * dy;
}

inline bool isEqHandleHit(juce::Point<float> pointer, juce::Point<float> handle) noexcept
{
    return distanceSquared(pointer, handle) <= eqHandleHitRadius * eqHandleHitRadius;
}

inline bool isCutHandleHit(juce::Point<float> pointer, juce::Point<float> handle) noexcept
{
    return distanceSquared(pointer, handle) <= cutHandleHitRadius * cutHandleHitRadius;
}

inline juce::Rectangle<float> clampRectToBounds(juce::Rectangle<float> rect, juce::Rectangle<float> bounds) noexcept
{
    if (rect.getWidth() >= bounds.getWidth())
        rect.setX(bounds.getX());
    else
        rect.setX(std::clamp(rect.getX(), bounds.getX(), bounds.getRight() - rect.getWidth()));

    if (rect.getHeight() >= bounds.getHeight())
        rect.setY(bounds.getY());
    else
        rect.setY(std::clamp(rect.getY(), bounds.getY(), bounds.getBottom() - rect.getHeight()));

    return rect;
}

inline juce::String formatFrequency(float frequencyHz)
{
    if (! std::isfinite(frequencyHz))
        frequencyHz = 0.0f;

    if (frequencyHz >= 1000.0f)
    {
        const int decimals = frequencyHz >= 10000.0f ? 1 : 2;
        return juce::String(frequencyHz / 1000.0f, decimals) + " kHz";
    }

    const int decimals = frequencyHz < 100.0f ? 1 : 0;
    return juce::String(frequencyHz, decimals) + " Hz";
}

inline juce::String formatGain(float gainDb)
{
    if (! std::isfinite(gainDb))
        gainDb = 0.0f;

    auto text = juce::String(gainDb, 1) + " dB";
    if (gainDb >= 0.0f)
        text = "+" + text;

    return text;
}

inline juce::String formatTrim(float internalTrimDb)
{
    if (! std::isfinite(internalTrimDb) || internalTrimDb <= 0.25f)
        return "SAFE";

    const int roundedTrim = std::clamp(static_cast<int>(std::round(internalTrimDb)), 1, 99);
    return "TRIM -" + juce::String(roundedTrim);
}
} // namespace musique::eq::ui
