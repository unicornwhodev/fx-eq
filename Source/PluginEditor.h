#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "FXTokens.h"
#include "FXLookAndFeel.h"
#include "FXComponents.h"

class MusiqueEQEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MusiqueEQEditor(MusiqueEQProcessor&);
    ~MusiqueEQEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;

    void timerCallback() override;
    void paintVisualization(juce::Graphics&, juce::Rectangle<int> area);

    MusiqueEQProcessor& proc;
    fx::FXLookAndFeel lnf { fx::accent::eq };

    // Header
    juce::Label titleLabel;
    juce::Image pluginIcon, logoImg;
    juce::TextButton bypassBtn{"Bypass"}, monoBtn{"STEREO IN"}, headroomBtn{"SAFE"}, settingsBtn{juce::CharPointer_UTF8("\xe2\x9a\x99")};

    // Preset bar
    juce::TextButton prevBtn{"<"}, nextBtn{">"}, saveBtn{"Save"}, abBtn{"A/B"};
    juce::ComboBox presetBox;

    // 6 knobs: Low, Lo-Mid, Mid, Hi-Mid, High, Q
    juce::Slider knobs[6];
    juce::Label knobLabels[6];

    // Footer
    fx::MeterComponent inMeter, outMeter;
    juce::Slider mixSlider, outputSlider;
    juce::Label versionLabel;
    fx::LEDComponent clipLED;

    // Visualization state
    float phase = 0.0f;

    // Attachments
    std::unique_ptr<SliderAttach> lowAtt, lmAtt, midAtt, hmAtt, highAtt, qAtt, mixAtt, outAtt;
    std::unique_ptr<ButtonAttach> bypassAtt, monoAtt;

    std::shared_ptr<juce::Array<juce::var>> presets;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueEQEditor)
};
