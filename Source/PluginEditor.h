#pragma once
#include <JuceHeader.h>
#include <array>
#include <limits>
#include "PluginProcessor.h"
#include "EQGraphMapping.h"
#include "EQGraphUI.h"
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
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;
    using SliderAttach = APVTS::SliderAttachment;
    using ButtonAttach = APVTS::ButtonAttachment;

    enum class SelectedBand
    {
        hpf,
        low,
        lowMid,
        mid,
        highMid,
        high,
        lpf
    };

    struct HandleHit
    {
        SelectedBand band = SelectedBand::mid;
        float distanceSquared = std::numeric_limits<float>::max();
        bool valid = false;
    };

    struct GraphCurveCache
    {
        bool valid = false;
        float width = 0.0f;
        float height = 0.0f;
        float plotX = 0.0f;
        float plotW = 0.0f;
        float midY = 0.0f;
        float q = 0.0f;
        double sampleRate = 0.0;
        std::array<float, 5> gains {};
        std::array<float, 5> frequencies {};
        bool hpfEnabled = false;
        bool lpfEnabled = false;
        float hpfFrequency = 0.0f;
        float lpfFrequency = 0.0f;
        int hpfSlope = 0;
        int lpfSlope = 0;
        juce::Path curvePath;
        juce::Path fillPath;
    };

    void timerCallback() override;
    void paintVisualization(juce::Graphics&, juce::Rectangle<int> area);
    void paintSelectionPanel(juce::Graphics&, juce::Rectangle<float> area);
    void updateDragForBand(SelectedBand band, juce::Point<float> position);
    void handlePanelClick(juce::Point<float> position);
    bool isPanelInteractiveAt(juce::Point<float> position) const;
    HandleHit findClosestHandle(juce::Point<float> position) const;
    juce::Rectangle<int> getVisualizationBounds() const;
    juce::Rectangle<float> getGraphPlotBounds() const;
    juce::Rectangle<float> getSelectionPanelBounds() const;
    juce::Rectangle<float> getToggleHitBounds() const;
    std::array<juce::Rectangle<float>, 3> getSlopeHitBounds() const;
    double getGraphSampleRate() const noexcept;
    float readParameterValue(const char* id, float fallback) const noexcept;
    bool readParameterBool(const char* id) const noexcept;
    void setParameterValue(const char* id, float value);
    bool isEqBand(SelectedBand band) const noexcept;
    const char* getBandName(SelectedBand band) const noexcept;
    const char* getFrequencyParamId(SelectedBand band) const noexcept;
    const char* getGainParamId(SelectedBand band) const noexcept;
    float getBandFrequency(SelectedBand band) const noexcept;
    float getBandGain(SelectedBand band) const noexcept;
    bool isCutBandEnabled(SelectedBand band) const noexcept;
    int getCutBandSlope(SelectedBand band) const noexcept;

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
    SelectedBand selectedBand = SelectedBand::mid;
    bool isDraggingGraphHandle = false;
    bool isHoveringGraphHandle = false;
    GraphCurveCache graphCurveCache;

    // Attachments
    std::unique_ptr<SliderAttach> lowAtt, lmAtt, midAtt, hmAtt, highAtt, qAtt, mixAtt, outAtt;
    std::unique_ptr<ButtonAttach> bypassAtt, monoAtt;

    std::shared_ptr<juce::Array<juce::var>> presets;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MusiqueEQEditor)
};
