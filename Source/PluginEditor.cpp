#include "PluginEditor.h"
#include "BinaryData.h"
#include "EQPresetMigration.h"
#include "EQGraphUI.h"
#include <array>
#include <cmath>

namespace
{
    namespace graph = musique::eq::graph;
    namespace graphui = musique::eq::ui;

    constexpr std::array<float, 5> kLegacyFrequencyDefaults {{
        100.0f, 350.0f, 1200.0f, 4500.0f, 10000.0f
    }};

    struct CutFilterGraphState
    {
        bool hpfEnabled = false;
        float hpfFrequency = 30.0f;
        int hpfSlopeDbPerOct = 12;
        bool lpfEnabled = false;
        float lpfFrequency = 18000.0f;
        int lpfSlopeDbPerOct = 12;
    };

    int sanitizeSlopeDbPerOct(float slopeDbPerOct)
    {
        return graph::snapSlopeDbPerOct(slopeDbPerOct);
    }

    int slopeDbPerOctToOrder(int slopeDbPerOct)
    {
        return juce::jlimit(2, 8, sanitizeSlopeDbPerOct(static_cast<float>(slopeDbPerOct)) / 6);
    }

    void applyEQPreset(MusiqueEQProcessor& proc, const juce::var& preset)
    {
        fx::preset::applyToAPVTS(proc.getAPVTS(), musique::eq::presets::migratePresetForCurrentParameters(preset));
    }

    juce::Array<juce::var> loadEmbeddedFactoryPresets()
    {
        juce::Array<juce::var> out;
        const auto jsonText = juce::String::fromUTF8(
            reinterpret_cast<const char*>(BinaryData::factory_bank_json),
            BinaryData::factory_bank_jsonSize);

        const auto parsed = juce::JSON::parse(jsonText);
        if (auto* bank = parsed.getDynamicObject())
            if (auto* presetArray = bank->getProperty("presets").getArray())
                out.addArray(*presetArray);

        return out;
    }

    juce::Array<juce::var> loadEQPresets()
    {
        auto out = loadEmbeddedFactoryPresets();
        out.addArray(fx::preset::loadUserPresets("fx-eq"));
        return out;
    }

    float readGraphFrequency(juce::AudioProcessorValueTreeState& apvts,
                             const char* id,
                             float fallback,
                             double sampleRate)
    {
        float frequency = fallback;

        if (auto* raw = apvts.getRawParameterValue(id))
            frequency = raw->load();

        if (! std::isfinite(frequency))
            frequency = fallback;

        return juce::jlimit(graph::minFrequencyHz, graph::getMaxGraphFrequency(sampleRate), frequency);
    }

    bool readBoolParameter(juce::AudioProcessorValueTreeState& apvts, const char* id)
    {
        if (auto* raw = apvts.getRawParameterValue(id))
            return raw->load() > 0.5f;

        return false;
    }

    float readFloatParameter(juce::AudioProcessorValueTreeState& apvts, const char* id, float fallback)
    {
        if (auto* raw = apvts.getRawParameterValue(id))
        {
            const float value = raw->load();
            if (std::isfinite(value))
                return value;
        }

        return fallback;
    }

    CutFilterGraphState readCutFilterGraphState(juce::AudioProcessorValueTreeState& apvts, double sampleRate)
    {
        CutFilterGraphState state {};
        state.hpfEnabled = readBoolParameter(apvts, "hpf_enabled");
        state.hpfFrequency = readGraphFrequency(apvts, "hpf_freq", 30.0f, sampleRate);
        state.hpfSlopeDbPerOct = sanitizeSlopeDbPerOct(readFloatParameter(apvts, "hpf_slope", 12.0f));
        state.lpfEnabled = readBoolParameter(apvts, "lpf_enabled");
        state.lpfFrequency = readGraphFrequency(apvts, "lpf_freq", 18000.0f, sampleRate);
        state.lpfSlopeDbPerOct = sanitizeSlopeDbPerOct(readFloatParameter(apvts, "lpf_slope", 12.0f));
        return state;
    }

    double getCutMagnitude(const juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>>& filters,
                           double frequency,
                           double sampleRate)
    {
        double magnitude = 1.0;
        for (auto* coefficients : filters)
            if (coefficients != nullptr)
                magnitude *= coefficients->getMagnitudeForFrequency(frequency, sampleRate);

        return std::isfinite(magnitude) ? magnitude : 1.0;
    }

}

MusiqueEQEditor::MusiqueEQEditor(MusiqueEQProcessor& p)
    : AudioProcessorEditor(&p), proc(p)
{
    setLookAndFeel(&lnf);
    setSize(fx::dim::appW, fx::dim::appH);

    // Header
    titleLabel.setText("EQUALIZER", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::header).withStyle("Bold")));
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    titleLabel.setColour(juce::Label::textColourId, fx::col::textPrimary);
    addAndMakeVisible(titleLabel);

    pluginIcon = juce::ImageCache::getFromMemory(BinaryData::icon_small_png, BinaryData::icon_small_pngSize);
    logoImg = juce::ImageCache::getFromMemory(BinaryData::logo_png, BinaryData::logo_pngSize);

    auto setupHdrBtn = [&](juce::TextButton& b, bool toggle = false) {
        b.setColour(juce::TextButton::buttonColourId, fx::col::surfSecondary);
        b.setColour(juce::TextButton::textColourOffId, fx::col::textPrimary);
        if (toggle) b.setClickingTogglesState(true);
        addAndMakeVisible(b);
    };
    setupHdrBtn(bypassBtn, true);
    setupHdrBtn(monoBtn, true);
    setupHdrBtn(headroomBtn);
    headroomBtn.setTooltip("Shows the internal safety trim applied before the EQ stages when boosts and Q become aggressive");
    headroomBtn.onClick = [] {};

    // Preset bar
    setupHdrBtn(prevBtn); setupHdrBtn(nextBtn); setupHdrBtn(saveBtn);
    addAndMakeVisible(presetBox);
    presetBox.setTextWhenNothingSelected("Manual State");
    presetBox.setTextWhenNoChoicesAvailable("Manual State");

    presets = std::make_shared<juce::Array<juce::var>>(loadEQPresets());
    if (! presets->isEmpty())
    {
        int id = 1;
        for (auto& pv : *presets)
            if (auto* o = pv.getDynamicObject())
                presetBox.addItem(o->getProperty("name").toString(), id++);
    }
    presetBox.onChange = [this] {
        int i = presetBox.getSelectedItemIndex();
        if (i >= 0 && i < presets->size()) applyEQPreset(proc, presets->getReference(i));
    };
    prevBtn.onClick = [this] {
        const int count = presetBox.getNumItems();
        if (count <= 0) return;
        const int i = presetBox.getSelectedItemIndex();
        presetBox.setSelectedItemIndex(i > 0 ? i - 1 : 0);
    };
    nextBtn.onClick = [this] {
        const int count = presetBox.getNumItems();
        if (count <= 0) return;
        const int i = presetBox.getSelectedItemIndex();
        presetBox.setSelectedItemIndex(i >= 0 && i < count - 1 ? i + 1 : 0);
    };
    saveBtn.onClick = [this] {
        auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        juce::StringArray ids {"low_gain","low_mid_gain","mid_gain","high_mid_gain","high_gain","q","mix","output","bypass","mono",
                               "low_freq","low_mid_freq","mid_freq","high_mid_freq","high_freq",
                               "hpf_enabled","hpf_freq","hpf_slope","lpf_enabled","lpf_freq","lpf_slope"};
        if (fx::preset::saveUserPreset("fx-eq", name, ids, proc.getAPVTS()))
        {
            *presets = loadEQPresets();
            presetBox.clear();
            presetBox.setTextWhenNothingSelected("Manual State");
            presetBox.setTextWhenNoChoicesAvailable("Manual State");
            int id = 1;
            for (auto& pv : *presets)
                if (auto* o = pv.getDynamicObject()) presetBox.addItem(o->getProperty("name").toString(), id++);
            presetBox.setSelectedItemIndex(presetBox.getNumItems() - 1);
        }
    };

    // Knobs
    const char* labels[6] = {"LOW", "LO-MID", "MID", "HI-MID", "HIGH", "Q"};
    for (int i = 0; i < 6; ++i)
    {
        knobs[i].setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knobs[i].setTextBoxStyle(juce::Slider::TextBoxBelow, false, 62, 16);
        addAndMakeVisible(knobs[i]);
        knobLabels[i].setText(labels[i], juce::dontSendNotification);
        knobLabels[i].setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::label).withStyle("Bold")));
        knobLabels[i].setJustificationType(juce::Justification::centred);
        knobLabels[i].setColour(juce::Label::textColourId, fx::col::textMuted);
        addAndMakeVisible(knobLabels[i]);
    }

    // Footer
    addAndMakeVisible(inMeter);
    addAndMakeVisible(outMeter);
    mixSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(mixSlider);
    outputSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    addAndMakeVisible(outputSlider);
    clipLED.setAccent(fx::accent::eq);
    addAndMakeVisible(clipLED);
    versionLabel.setText("Musique EQ v1.0", juce::dontSendNotification);
    versionLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(fx::font::footer)));
    versionLabel.setColour(juce::Label::textColourId, fx::col::textMuted);
    versionLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(versionLabel);

    // Attachments
    lowAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "low_gain",      knobs[0]);
    lmAtt   = std::make_unique<SliderAttach>(proc.getAPVTS(), "low_mid_gain",  knobs[1]);
    midAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "mid_gain",      knobs[2]);
    hmAtt   = std::make_unique<SliderAttach>(proc.getAPVTS(), "high_mid_gain", knobs[3]);
    highAtt = std::make_unique<SliderAttach>(proc.getAPVTS(), "high_gain",     knobs[4]);
    qAtt    = std::make_unique<SliderAttach>(proc.getAPVTS(), "q",             knobs[5]);
    mixAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "mix",           mixSlider);
    outAtt  = std::make_unique<SliderAttach>(proc.getAPVTS(), "output",        outputSlider);
    bypassAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "bypass", bypassBtn);
    monoAtt = std::make_unique<ButtonAttach>(proc.getAPVTS(), "mono", monoBtn);

    startTimerHz(fx::anim::fftRefreshHz);
}

MusiqueEQEditor::~MusiqueEQEditor() { setLookAndFeel(nullptr); }

juce::Rectangle<int> MusiqueEQEditor::getVisualizationBounds() const
{
    return graphui::visualBounds(static_cast<float>(getWidth())).toNearestInt();
}

juce::Rectangle<float> MusiqueEQEditor::getGraphPlotBounds() const
{
    return graphui::graphPlotBounds(static_cast<float>(getWidth()));
}

juce::Rectangle<float> MusiqueEQEditor::getSelectionPanelBounds() const
{
    return graphui::selectionPanelBounds(static_cast<float>(getWidth()));
}

juce::Rectangle<float> MusiqueEQEditor::getToggleHitBounds() const
{
    return graphui::cutToggleBounds(getSelectionPanelBounds());
}

std::array<juce::Rectangle<float>, 3> MusiqueEQEditor::getSlopeHitBounds() const
{
    return graphui::slopeButtonBounds(getSelectionPanelBounds());
}

double MusiqueEQEditor::getGraphSampleRate() const noexcept
{
    return proc.getPreparedSampleRate() > 1000.0 ? proc.getPreparedSampleRate() : 44100.0;
}

float MusiqueEQEditor::readParameterValue(const char* id, float fallback) const noexcept
{
    if (auto* raw = proc.getAPVTS().getRawParameterValue(id))
    {
        const float value = raw->load();
        if (std::isfinite(value))
            return value;
    }

    return fallback;
}

bool MusiqueEQEditor::readParameterBool(const char* id) const noexcept
{
    return readParameterValue(id, 0.0f) > 0.5f;
}

void MusiqueEQEditor::setParameterValue(const char* id, float value)
{
    auto& apvts = proc.getAPVTS();
    if (auto* param = apvts.getParameter(id))
        param->setValueNotifyingHost(param->convertTo0to1(value));
}

bool MusiqueEQEditor::isEqBand(SelectedBand band) const noexcept
{
    return band == SelectedBand::low
        || band == SelectedBand::lowMid
        || band == SelectedBand::mid
        || band == SelectedBand::highMid
        || band == SelectedBand::high;
}

const char* MusiqueEQEditor::getBandName(SelectedBand band) const noexcept
{
    switch (band)
    {
        case SelectedBand::hpf:     return "HPF";
        case SelectedBand::low:     return "LOW";
        case SelectedBand::lowMid:  return "LO-MID";
        case SelectedBand::mid:     return "MID";
        case SelectedBand::highMid: return "HI-MID";
        case SelectedBand::high:    return "HIGH";
        case SelectedBand::lpf:     return "LPF";
    }

    return "MID";
}

const char* MusiqueEQEditor::getFrequencyParamId(SelectedBand band) const noexcept
{
    switch (band)
    {
        case SelectedBand::hpf:     return "hpf_freq";
        case SelectedBand::low:     return "low_freq";
        case SelectedBand::lowMid:  return "low_mid_freq";
        case SelectedBand::mid:     return "mid_freq";
        case SelectedBand::highMid: return "high_mid_freq";
        case SelectedBand::high:    return "high_freq";
        case SelectedBand::lpf:     return "lpf_freq";
    }

    return "mid_freq";
}

const char* MusiqueEQEditor::getGainParamId(SelectedBand band) const noexcept
{
    switch (band)
    {
        case SelectedBand::low:     return "low_gain";
        case SelectedBand::lowMid:  return "low_mid_gain";
        case SelectedBand::mid:     return "mid_gain";
        case SelectedBand::highMid: return "high_mid_gain";
        case SelectedBand::high:    return "high_gain";
        default:                    return "";
    }
}

float MusiqueEQEditor::getBandFrequency(SelectedBand band) const noexcept
{
    const float fallback = band == SelectedBand::hpf ? 30.0f
        : band == SelectedBand::lpf ? 18000.0f
        : kLegacyFrequencyDefaults[(size_t) juce::jlimit(0, 4, static_cast<int>(band) - 1)];

    return readParameterValue(getFrequencyParamId(band), fallback);
}

float MusiqueEQEditor::getBandGain(SelectedBand band) const noexcept
{
    return isEqBand(band) ? readParameterValue(getGainParamId(band), 0.0f) : 0.0f;
}

bool MusiqueEQEditor::isCutBandEnabled(SelectedBand band) const noexcept
{
    if (band == SelectedBand::hpf)
        return readParameterBool("hpf_enabled");
    if (band == SelectedBand::lpf)
        return readParameterBool("lpf_enabled");

    return true;
}

int MusiqueEQEditor::getCutBandSlope(SelectedBand band) const noexcept
{
    if (band == SelectedBand::hpf)
        return graph::snapSlopeDbPerOct(readParameterValue("hpf_slope", 12.0f));
    if (band == SelectedBand::lpf)
        return graph::snapSlopeDbPerOct(readParameterValue("lpf_slope", 12.0f));

    return 12;
}

MusiqueEQEditor::HandleHit MusiqueEQEditor::findClosestHandle(juce::Point<float> position) const
{
    const auto plot = getGraphPlotBounds();
    const float left = plot.getX();
    const float width = plot.getWidth();
    const float visualY = plot.getY();
    const float visualH = plot.getHeight();
    const float midY = visualY + visualH * 0.5f;
    const double sampleRate = getGraphSampleRate();

    const std::array<SelectedBand, 7> bands {{
        SelectedBand::hpf, SelectedBand::low, SelectedBand::lowMid, SelectedBand::mid,
        SelectedBand::highMid, SelectedBand::high, SelectedBand::lpf
    }};

    HandleHit best {};
    for (auto band : bands)
    {
        const float x = graph::frequencyToX(getBandFrequency(band), left, width, sampleRate);
        const float y = isEqBand(band) ? graph::gainToY(getBandGain(band), midY, visualH) : midY;
        const auto handlePoint = juce::Point<float>(x, y);
        const bool hit = isEqBand(band)
            ? graphui::isEqHandleHit(position, handlePoint)
            : graphui::isCutHandleHit(position, handlePoint);

        if (! hit)
            continue;

        const float distanceSquared = graphui::distanceSquared(position, handlePoint);
        if (distanceSquared < best.distanceSquared)
        {
            best.band = band;
            best.distanceSquared = distanceSquared;
            best.valid = true;
        }
    }

    return best;
}

void MusiqueEQEditor::updateDragForBand(SelectedBand band, juce::Point<float> position)
{
    const auto plot = getGraphPlotBounds();
    const float frequency = graph::xToFrequency(position.x, plot.getX(), plot.getWidth(), getGraphSampleRate());
    setParameterValue(getFrequencyParamId(band), frequency);

    if (isEqBand(band))
    {
        const float midY = plot.getY() + plot.getHeight() * 0.5f;
        setParameterValue(getGainParamId(band), graph::yToGain(position.y, midY, plot.getHeight()));
    }
}

void MusiqueEQEditor::handlePanelClick(juce::Point<float> position)
{
    if (selectedBand == SelectedBand::hpf || selectedBand == SelectedBand::lpf)
    {
        if (getToggleHitBounds().contains(position))
        {
            const char* id = selectedBand == SelectedBand::hpf ? "hpf_enabled" : "lpf_enabled";
            setParameterValue(id, readParameterBool(id) ? 0.0f : 1.0f);
            repaint();
            return;
        }

        const auto slopeHitBounds = getSlopeHitBounds();
        constexpr int slopes[3] { 12, 24, 48 };
        for (size_t i = 0; i < slopeHitBounds.size(); ++i)
        {
            if (slopeHitBounds[i].contains(position))
            {
                setParameterValue(selectedBand == SelectedBand::hpf ? "hpf_slope" : "lpf_slope", static_cast<float>(slopes[i]));
                repaint();
                return;
            }
        }
    }
}

bool MusiqueEQEditor::isPanelInteractiveAt(juce::Point<float> position) const
{
    if (! getSelectionPanelBounds().contains(position))
        return false;

    if (selectedBand != SelectedBand::hpf && selectedBand != SelectedBand::lpf)
        return false;

    if (getToggleHitBounds().contains(position))
        return true;

    const auto slopeHitBounds = getSlopeHitBounds();
    for (const auto& bounds : slopeHitBounds)
        if (bounds.contains(position))
            return true;

    return false;
}

void MusiqueEQEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto position = event.position;

    if (getSelectionPanelBounds().contains(position))
    {
        handlePanelClick(position);
        return;
    }

    if (! getVisualizationBounds().toFloat().contains(position))
        return;

    const auto hit = findClosestHandle(position);
    if (hit.valid)
    {
        selectedBand = hit.band;
        isDraggingGraphHandle = true;
        repaint();
    }
}

void MusiqueEQEditor::mouseDrag(const juce::MouseEvent& event)
{
    if (! isDraggingGraphHandle)
        return;

    updateDragForBand(selectedBand, event.position);
    repaint();
}

void MusiqueEQEditor::mouseUp(const juce::MouseEvent&)
{
    isDraggingGraphHandle = false;
}

void MusiqueEQEditor::mouseMove(const juce::MouseEvent& event)
{
    const bool hovering = isPanelInteractiveAt(event.position)
        || (getVisualizationBounds().toFloat().contains(event.position) && findClosestHandle(event.position).valid);

    if (hovering != isHoveringGraphHandle)
    {
        isHoveringGraphHandle = hovering;
        setMouseCursor(hovering ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
}

void MusiqueEQEditor::mouseExit(const juce::MouseEvent&)
{
    isHoveringGraphHandle = false;
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void MusiqueEQEditor::paintSelectionPanel(juce::Graphics& g, juce::Rectangle<float> area)
{
    const bool selectedCut = selectedBand == SelectedBand::hpf || selectedBand == SelectedBand::lpf;
    const bool enabled = selectedCut ? isCutBandEnabled(selectedBand) : true;

    g.setColour(fx::col::surfSecondary.withAlpha(0.96f));
    g.fillRoundedRectangle(area, 6.0f);
    g.setColour((enabled ? fx::accent::eq : fx::col::disabled).withAlpha(selectedCut ? 0.62f : 0.48f));
    g.drawRoundedRectangle(area.reduced(0.5f), 6.0f, 1.0f);

    auto content = area.reduced(8.0f, 4.0f);
    auto nameArea = content.removeFromLeft(selectedCut ? 36.0f : 62.0f);

    g.setColour(enabled ? fx::col::textPrimary : fx::col::textSecondary);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
    g.drawText(getBandName(selectedBand), nameArea.toNearestInt(), juce::Justification::centredLeft);

    auto freqArea = content.removeFromLeft(selectedCut ? 84.0f : 94.0f);
    g.setColour(fx::col::textSecondary);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText(graphui::formatFrequency(getBandFrequency(selectedBand)),
               freqArea.toNearestInt(),
               juce::Justification::centredLeft);

    if (isEqBand(selectedBand))
    {
        g.setColour(fx::accent::eq);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(graphui::formatGain(getBandGain(selectedBand)),
                   content.toNearestInt(),
                   juce::Justification::centredLeft);
        return;
    }

    const auto toggle = getToggleHitBounds();
    g.setColour(enabled ? fx::accent::eq.withAlpha(0.24f) : fx::col::surfTertiary.withAlpha(0.88f));
    g.fillRoundedRectangle(toggle, 5.0f);
    g.setColour((enabled ? fx::accent::eq : fx::col::textMuted).withAlpha(0.65f));
    g.drawRoundedRectangle(toggle, 5.0f, 1.0f);
    g.setColour(enabled ? fx::col::textPrimary : fx::col::textSecondary);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
    g.drawText(enabled ? "ON" : "OFF", toggle.toNearestInt(), juce::Justification::centred);

    const auto slopeBounds = getSlopeHitBounds();
    const int slopes[3] { 12, 24, 48 };
    const int currentSlope = getCutBandSlope(selectedBand);

    for (size_t i = 0; i < slopeBounds.size(); ++i)
    {
        const bool selected = slopes[i] == currentSlope;
        g.setColour(selected ? fx::accent::eq.withAlpha(0.24f) : fx::col::surfTertiary.withAlpha(0.72f));
        g.fillRoundedRectangle(slopeBounds[i], 4.0f);
        g.setColour((selected ? fx::accent::eq : fx::col::textMuted).withAlpha(0.62f));
        g.drawRoundedRectangle(slopeBounds[i], 4.0f, 1.0f);
        g.setColour(selected ? fx::col::textPrimary : fx::col::textSecondary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f).withStyle("Bold")));
        g.drawText(juce::String(slopes[i]), slopeBounds[i].toNearestInt(), juce::Justification::centred);
    }
}

void MusiqueEQEditor::timerCallback()
{
    const auto inputLevels = proc.getVisualState().getInputLevels();
    const auto outputLevels = proc.getVisualState().getOutputLevels();
    inMeter.setLevel(inputLevels.left, inputLevels.right);
    outMeter.setLevel(outputLevels.left, outputLevels.right);
    clipLED.setOn(juce::jmax(outputLevels.left, outputLevels.right) > 0.98f);
    const bool mono = proc.getAPVTS().getRawParameterValue("mono")->load() > 0.5f;
    const float internalTrimDb = proc.getCurrentInternalTrimDb();
    monoBtn.setButtonText(mono ? "MONO IN" : "STEREO IN");
    headroomBtn.setButtonText(graphui::formatTrim(internalTrimDb));
    headroomBtn.setColour(juce::TextButton::buttonColourId,
        internalTrimDb > 0.25f ? fx::col::meterMid.withAlpha(0.18f) : fx::col::surfSecondary);
    headroomBtn.setColour(juce::TextButton::textColourOffId,
        internalTrimDb > 0.25f ? fx::col::meterMid.brighter(0.2f) : fx::col::textPrimary);

    phase += 0.03f;
    if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
    repaint(getSelectionPanelBounds().getSmallestIntegerContainer());
}

void MusiqueEQEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    auto& apvts = proc.getAPVTS();

    // Read band gains in dB and Q
    std::array<float, 5> gains {};
    const char* ids[5] = {"low_gain","low_mid_gain","mid_gain","high_mid_gain","high_gain"};
    for (int i = 0; i < 5; ++i)
        if (auto* p = apvts.getRawParameterValue(ids[i])) gains[(size_t) i] = p->load();

    float qVal = 1.0f;
    if (auto* p = apvts.getRawParameterValue("q")) qVal = p->load();
    qVal = juce::jlimit(0.3f, 8.0f, qVal);
    const bool mono = apvts.getRawParameterValue("mono")->load() > 0.5f;
    const float internalTrimDb = proc.getCurrentInternalTrimDb();

    const float w = (float)area.getWidth();
    const float h = (float)area.getHeight();
    const float cx = (float)area.getX();
    const float cy = (float)area.getY();
    const float midY = cy + h * 0.5f;
    const float pad = 24.0f;
    const float plotX = cx + pad;
    const float plotW = w - 2.0f * pad;

    // dB scale on left
    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    for (int db = -24; db <= 24; db += 6)
    {
        float yPos = midY - (float)db / 24.0f * (h * 0.42f);
        g.drawText(juce::String(db), (int)cx + 2, (int)(yPos - 5), 26, 10, juce::Justification::centredRight);
        g.setColour(fx::col::gridMinor);
        g.drawHorizontalLine((int)yPos, cx + pad + 4, cx + w - pad);
        g.setColour(fx::col::textMuted);
    }

    // 0 dB reference line
    g.setColour(fx::col::gridMajor);
    g.drawHorizontalLine((int)midY, cx + pad, cx + w - pad);

    // Frequency labels
    const float gridFreqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    const char* freqLabels[] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};
    for (int i = 0; i < 10; ++i)
    {
        const double sampleRateForGrid = getGraphSampleRate();
        if (gridFreqs[i] > graph::getMaxGraphFrequency(sampleRateForGrid))
            continue;

        float xPos = graph::frequencyToX(gridFreqs[i], plotX, plotW, sampleRateForGrid);
        g.setColour(fx::col::gridMinor);
        g.drawVerticalLine((int)xPos, cy + 8.0f, cy + h - 16.0f);
        g.setColour(fx::col::textMuted);
        g.drawText(freqLabels[i], (int)(xPos - 14), (int)(cy + h - 16), 28, 12, juce::Justification::centred);
    }

    const double sampleRate = getGraphSampleRate();
    const std::array<float, 5> bandFreqs {{
        readGraphFrequency(apvts, "low_freq", kLegacyFrequencyDefaults[0], sampleRate),
        readGraphFrequency(apvts, "low_mid_freq", kLegacyFrequencyDefaults[1], sampleRate),
        readGraphFrequency(apvts, "mid_freq", kLegacyFrequencyDefaults[2], sampleRate),
        readGraphFrequency(apvts, "high_mid_freq", kLegacyFrequencyDefaults[3], sampleRate),
        readGraphFrequency(apvts, "high_freq", kLegacyFrequencyDefaults[4], sampleRate)
    }};

    const auto cutState = readCutFilterGraphState(apvts, sampleRate);

    const bool cacheValid = graphCurveCache.valid
        && graphCurveCache.width == w
        && graphCurveCache.height == h
        && graphCurveCache.plotX == plotX
        && graphCurveCache.plotW == plotW
        && graphCurveCache.midY == midY
        && graphCurveCache.q == qVal
        && graphCurveCache.sampleRate == sampleRate
        && graphCurveCache.gains == gains
        && graphCurveCache.frequencies == bandFreqs
        && graphCurveCache.hpfEnabled == cutState.hpfEnabled
        && graphCurveCache.lpfEnabled == cutState.lpfEnabled
        && graphCurveCache.hpfFrequency == cutState.hpfFrequency
        && graphCurveCache.lpfFrequency == cutState.lpfFrequency
        && graphCurveCache.hpfSlope == cutState.hpfSlopeDbPerOct
        && graphCurveCache.lpfSlope == cutState.lpfSlopeDbPerOct;

    if (! cacheValid)
    {
        using Coeffs = juce::dsp::IIR::Coefficients<float>;
        const auto low = Coeffs::makeLowShelf(sampleRate, bandFreqs[0], qVal, juce::Decibels::decibelsToGain(gains[0]));
        const auto lowMid = Coeffs::makePeakFilter(sampleRate, bandFreqs[1], qVal, juce::Decibels::decibelsToGain(gains[1]));
        const auto mid = Coeffs::makePeakFilter(sampleRate, bandFreqs[2], qVal, juce::Decibels::decibelsToGain(gains[2]));
        const auto highMid = Coeffs::makePeakFilter(sampleRate, bandFreqs[3], qVal, juce::Decibels::decibelsToGain(gains[3]));
        const auto high = Coeffs::makeHighShelf(sampleRate, bandFreqs[4], qVal, juce::Decibels::decibelsToGain(gains[4]));

        juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>> hpfCoefficients;
        juce::ReferenceCountedArray<juce::dsp::IIR::Coefficients<float>> lpfCoefficients;

        if (cutState.hpfEnabled)
            hpfCoefficients = juce::dsp::FilterDesign<float>::designIIRHighpassHighOrderButterworthMethod(
                cutState.hpfFrequency,
                sampleRate,
                slopeDbPerOctToOrder(cutState.hpfSlopeDbPerOct));

        if (cutState.lpfEnabled)
            lpfCoefficients = juce::dsp::FilterDesign<float>::designIIRLowpassHighOrderButterworthMethod(
                cutState.lpfFrequency,
                sampleRate,
                slopeDbPerOctToOrder(cutState.lpfSlopeDbPerOct));

        graphCurveCache.curvePath.clear();
        bool started = false;
        constexpr int steps = 256;

        for (int s = 0; s <= steps; ++s)
        {
            float norm = (float)s / (float)steps;
            float freq = graph::normalisedToFrequency(norm, sampleRate);
            float xPos = plotX + norm * plotW;

            const double totalMag = low->getMagnitudeForFrequency(freq, sampleRate)
                * lowMid->getMagnitudeForFrequency(freq, sampleRate)
                * mid->getMagnitudeForFrequency(freq, sampleRate)
                * highMid->getMagnitudeForFrequency(freq, sampleRate)
                * high->getMagnitudeForFrequency(freq, sampleRate)
                * getCutMagnitude(hpfCoefficients, freq, sampleRate)
                * getCutMagnitude(lpfCoefficients, freq, sampleRate);
            const float totalGainDb = juce::Decibels::gainToDecibels((float) totalMag, -24.0f);

            float yPos = midY - totalGainDb / 24.0f * (h * 0.42f);
            if (! started)
            {
                graphCurveCache.curvePath.startNewSubPath(xPos, yPos);
                started = true;
            }
            else
            {
                graphCurveCache.curvePath.lineTo(xPos, yPos);
            }
        }

        graphCurveCache.fillPath = graphCurveCache.curvePath;
        graphCurveCache.fillPath.lineTo(cx + w - pad, midY);
        graphCurveCache.fillPath.lineTo(cx + pad, midY);
        graphCurveCache.fillPath.closeSubPath();
        graphCurveCache.width = w;
        graphCurveCache.height = h;
        graphCurveCache.plotX = plotX;
        graphCurveCache.plotW = plotW;
        graphCurveCache.midY = midY;
        graphCurveCache.q = qVal;
        graphCurveCache.sampleRate = sampleRate;
        graphCurveCache.gains = gains;
        graphCurveCache.frequencies = bandFreqs;
        graphCurveCache.hpfEnabled = cutState.hpfEnabled;
        graphCurveCache.lpfEnabled = cutState.lpfEnabled;
        graphCurveCache.hpfFrequency = cutState.hpfFrequency;
        graphCurveCache.lpfFrequency = cutState.lpfFrequency;
        graphCurveCache.hpfSlope = cutState.hpfSlopeDbPerOct;
        graphCurveCache.lpfSlope = cutState.lpfSlopeDbPerOct;
        graphCurveCache.valid = true;
    }

    // Fill under curve
    g.setColour(fx::accent::eq.withAlpha(0.08f));
    g.fillPath(graphCurveCache.fillPath);

    // Stroke curve
    g.setColour(fx::accent::eq.withAlpha(0.9f));
    g.strokePath(graphCurveCache.curvePath, juce::PathStrokeType(2.5f));

    auto drawBadge = [&](juce::Rectangle<float> rect, const juce::String& text, juce::Colour colour)
    {
        g.setColour(colour.withAlpha(0.16f));
        g.fillRoundedRectangle(rect, 8.0f);
        g.setColour(colour.withAlpha(0.6f));
        g.drawRoundedRectangle(rect, 8.0f, 1.0f);
        g.setColour(fx::col::textPrimary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f).withStyle("Bold")));
        g.drawText(text, rect.toNearestInt(), juce::Justification::centred);
    };

    drawBadge({ cx + w - 274.0f, cy + 14.0f, 88.0f, 22.0f }, mono ? "INPUT MONO" : "INPUT STEREO", fx::col::textSecondary);
    drawBadge({ cx + w - 178.0f, cy + 14.0f, 82.0f, 22.0f }, "Q " + juce::String(qVal, 2), fx::accent::eq);
    drawBadge({ cx + w - 88.0f, cy + 14.0f, 66.0f, 22.0f }, internalTrimDb > 0.25f ? "TRIM" : "SAFE", internalTrimDb > 0.25f ? fx::col::meterMid : fx::col::textSecondary);

    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));

    auto drawCutMarker = [&](SelectedBand band, float frequency, const juce::String& label, bool enabled)
    {
        const float xPos = graph::frequencyToX(frequency, plotX, plotW, sampleRate);
        const bool selected = selectedBand == band;
        const auto colour = enabled ? fx::accent::eq : fx::col::textMuted;
        g.setColour(colour.withAlpha(selected ? 0.72f : (enabled ? 0.38f : 0.20f)));
        g.drawVerticalLine((int)xPos, cy + 8.0f, cy + h - 16.0f);
        g.setColour(colour.withAlpha(selected ? 0.28f : 0.12f));
        g.fillEllipse(xPos - 8.0f, midY - 8.0f, 16.0f, 16.0f);
        g.setColour(selected ? fx::accent::eq : colour);
        g.fillEllipse(xPos - 4.5f, midY - 4.5f, 9.0f, 9.0f);
        g.setColour(fx::col::textSecondary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f).withStyle("Bold")));
        const auto labelBounds = graphui::clampRectToBounds({ xPos - 36.0f, cy + h - 48.0f, 72.0f, 12.0f },
                                                            { cx + pad, cy + 10.0f, w - 2.0f * pad, h - 36.0f });
        g.drawText(label, labelBounds.toNearestInt(), juce::Justification::centred);
    };

    drawCutMarker(SelectedBand::hpf, cutState.hpfFrequency,
                  cutState.hpfEnabled ? "HPF" : "HPF OFF",
                  cutState.hpfEnabled);
    drawCutMarker(SelectedBand::lpf, cutState.lpfFrequency,
                  cutState.lpfEnabled ? "LPF" : "LPF OFF",
                  cutState.lpfEnabled);

    // Band point indicators
    for (int b = 0; b < 5; ++b)
    {
        const auto band = static_cast<SelectedBand>(b + 1);
        const bool selected = selectedBand == band;
        float xPos = graph::frequencyToX(bandFreqs[b], plotX, plotW, sampleRate);
        float yPos = graph::gainToY(gains[b], midY, h);

        // Q width visualization (horizontal extent)
        float qWidth = (w - 2.0f * pad) * (1.0f / juce::jmax(qVal, 0.3f)) * 0.08f;
        g.setColour(fx::accent::eq.withAlpha(selected ? 0.20f : 0.12f));
        g.fillRoundedRectangle(xPos - qWidth, juce::jmin(yPos, midY), qWidth * 2.0f, std::abs(yPos - midY), 3.0f);

        // Band dot
        float pulse = 0.7f + 0.3f * std::sin(phase + (float)b * 1.2f);
        const float outer = selected ? 30.0f : 24.0f;
        g.setColour(fx::accent::eq.withAlpha((selected ? 0.32f : 0.2f) * pulse));
        g.fillEllipse(xPos - outer * 0.5f, yPos - outer * 0.5f, outer, outer);
        g.setColour(fx::accent::eq);
        g.fillEllipse(xPos - (selected ? 6.5f : 5.0f), yPos - (selected ? 6.5f : 5.0f), selected ? 13.0f : 10.0f, selected ? 13.0f : 10.0f);
        g.setColour(fx::col::bg);
        g.fillEllipse(xPos - 2.5f, yPos - 2.5f, 5.0f, 5.0f);

        // dB value at point
        g.setColour(fx::col::textSecondary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        const auto labelBounds = graphui::clampRectToBounds({ xPos - 29.0f, yPos - 20.0f, 58.0f, 12.0f },
                                                            { cx + pad, cy + 8.0f, w - 2.0f * pad, h - 28.0f });
        g.drawText(graphui::formatGain(gains[b]), labelBounds.toNearestInt(), juce::Justification::centred);
    }
}

void MusiqueEQEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::eq);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12, 10, 40, 40), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
    paintSelectionPanel(g, getSelectionPanelBounds());
    fx::paint::graphArea(g, getWidth());
    fx::paint::graphGrid(g, getWidth());
    paintVisualization(g, juce::Rectangle<int>(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH));
    fx::paint::controls(g, getWidth(), 6);
    fx::paint::footer(g, getWidth());
    if (logoImg.isValid())
    {
        const int fy = fx::dim::appH - fx::dim::footerH;
        g.drawImage(logoImg, juce::Rectangle<float>((float)getWidth() - 52.0f, (float)fy + 4.0f, 32.0f, 32.0f), juce::RectanglePlacement::centred);
    }
    fx::paint::footerLabel(g, "MIX", 80, 120);
    fx::paint::footerLabel(g, "OUT", 210, 120);
    fx::paint::outline(g, getLocalBounds());
}

void MusiqueEQEditor::resized()
{
    // Header
    titleLabel.setBounds(56, 10, 160, 40);
    bypassBtn.setBounds(getWidth() - 280, 16, 64, fx::dim::btnH);
    monoBtn.setBounds(getWidth() - 208, 16, 96, fx::dim::btnH);
    headroomBtn.setBounds(getWidth() - 104, 16, 82, fx::dim::btnH);

    // Preset bar
    const int py = fx::dim::headerH + 11;
    prevBtn.setBounds(260, py, 30, fx::dim::btnH);
    presetBox.setBounds(294, py, 300, fx::dim::btnH);
    nextBtn.setBounds(598, py, 30, fx::dim::btnH);
    saveBtn.setBounds(640, py, 56, fx::dim::btnH);

    // Knobs
    const int ctrlTop = fx::dim::headerH + fx::dim::presetBarH + fx::dim::visualH;
    const int numKnobs = 6;
    const int kW = getWidth() / numKnobs;
    const int kY = ctrlTop + 14;
    for (int i = 0; i < numKnobs; ++i)
    {
        int x = i * kW;
        knobs[i].setBounds(x + (kW - 92) / 2, kY, 92, 90);
        knobLabels[i].setBounds(x + (kW - 120) / 2, kY + 92, 120, 16);
    }

    // Footer
    const int fy = fx::dim::appH - fx::dim::footerH;
    inMeter.setBounds(16, fy + 6, 20, fx::dim::footerH - 12);
    outMeter.setBounds(42, fy + 6, 20, fx::dim::footerH - 12);
    mixSlider.setBounds(80, fy + 8, 120, 24);
    outputSlider.setBounds(210, fy + 8, 120, 24);
    clipLED.setBounds(350, fy + 14, 12, 12);
    versionLabel.setBounds(getWidth() - 220, fy + 8, 160, 24);
}
