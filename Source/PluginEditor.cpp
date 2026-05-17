#include "PluginEditor.h"
#include "BinaryData.h"
#include <cmath>

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
    setupHdrBtn(settingsBtn);
    fx::ui::markUnsupportedControl(settingsBtn);
    headroomBtn.setTooltip("Shows the internal safety trim applied before the EQ stages when boosts and Q become aggressive");
    headroomBtn.onClick = [] {};

    // Preset bar
    setupHdrBtn(prevBtn); setupHdrBtn(nextBtn); setupHdrBtn(saveBtn); setupHdrBtn(abBtn);
    addAndMakeVisible(presetBox);

    presets = std::make_shared<juce::Array<juce::var>>(fx::preset::loadAllPresets("fx-eq"));
    if (presets->isEmpty()) { presetBox.addItem("Init", 1); presetBox.setSelectedId(1); }
    else
    {
        int id = 1;
        for (auto& pv : *presets)
            if (auto* o = pv.getDynamicObject())
                presetBox.addItem(o->getProperty("name").toString(), id++);
        presetBox.setSelectedItemIndex(0, juce::dontSendNotification);
        fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(0));
    }
    presetBox.onChange = [this] {
        int i = presetBox.getSelectedItemIndex();
        if (i >= 0 && i < presets->size()) fx::preset::applyToAPVTS(proc.getAPVTS(), presets->getReference(i));
    };
    prevBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i > 0) presetBox.setSelectedItemIndex(i - 1); };
    nextBtn.onClick = [this] { int i = presetBox.getSelectedItemIndex(); if (i < presetBox.getNumItems() - 1) presetBox.setSelectedItemIndex(i + 1); };
    saveBtn.onClick = [this] {
        auto name = juce::String("User_") + juce::Time::getCurrentTime().formatted("%H%M%S");
        juce::StringArray ids {"low_gain","low_mid_gain","mid_gain","high_mid_gain","high_gain","q","mix","output","bypass","mono"};
        if (fx::preset::saveUserPreset("fx-eq", name, ids, proc.getAPVTS()))
        {
            *presets = fx::preset::loadAllPresets("fx-eq");
            presetBox.clear();
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
    headroomBtn.setButtonText(internalTrimDb > 0.25f ? ("TRIM " + juce::String(-internalTrimDb, 1) + "dB") : "SAFE");
    headroomBtn.setColour(juce::TextButton::buttonColourId,
        internalTrimDb > 0.25f ? fx::col::meterMid.withAlpha(0.18f) : fx::col::surfSecondary);
    headroomBtn.setColour(juce::TextButton::textColourOffId,
        internalTrimDb > 0.25f ? fx::col::meterMid.brighter(0.2f) : fx::col::textPrimary);

    phase += 0.03f;
    if (phase > juce::MathConstants<float>::twoPi) phase -= juce::MathConstants<float>::twoPi;
    repaint(0, fx::dim::headerH + fx::dim::presetBarH, getWidth(), fx::dim::visualH);
}

void MusiqueEQEditor::paintVisualization(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Read band gains in dB and Q
    float gains[5] = {0,0,0,0,0};
    const char* ids[5] = {"low_gain","low_mid_gain","mid_gain","high_mid_gain","high_gain"};
    for (int i = 0; i < 5; ++i)
        if (auto* p = proc.getAPVTS().getRawParameterValue(ids[i])) gains[i] = p->load();

    float qVal = 1.0f;
    if (auto* p = proc.getAPVTS().getRawParameterValue("q")) qVal = p->load();
    const bool mono = proc.getAPVTS().getRawParameterValue("mono")->load() > 0.5f;
    const float internalTrimDb = proc.getCurrentInternalTrimDb();
    const float maxPositiveGain = juce::jmax(juce::jmax(gains[0], gains[1]), juce::jmax(juce::jmax(gains[2], gains[3]), gains[4]));
    const float bandwidthOctaves = (float) (2.0 * std::asinh(1.0 / (2.0 * juce::jmax(0.3f, qVal))) / std::log(2.0));

    const float w = (float)area.getWidth();
    const float h = (float)area.getHeight();
    const float cx = (float)area.getX();
    const float cy = (float)area.getY();
    const float midY = cy + h * 0.5f;
    const float pad = 24.0f;

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
    const float freqs[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    const char* freqLabels[] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};
    for (int i = 0; i < 10; ++i)
    {
        float norm = std::log10(freqs[i] / 20.0f) / std::log10(20000.0f / 20.0f);
        float xPos = cx + pad + norm * (w - 2.0f * pad);
        g.setColour(fx::col::gridMinor);
        g.drawVerticalLine((int)xPos, cy + 8.0f, cy + h - 16.0f);
        g.setColour(fx::col::textMuted);
        g.drawText(freqLabels[i], (int)(xPos - 14), (int)(cy + h - 16), 28, 12, juce::Justification::centred);
    }

    const double sampleRate = proc.getPreparedSampleRate() > 1000.0 ? proc.getPreparedSampleRate() : 44100.0;
    const float maxFreq = (float) juce::jmax(200.0, sampleRate * 0.45);
    const float bandFreqs[5] = {
        juce::jmin(100.0f, maxFreq),
        juce::jmin(350.0f, maxFreq),
        juce::jmin(1200.0f, maxFreq),
        juce::jmin(4500.0f, maxFreq),
        juce::jmin(10000.0f, maxFreq)
    };
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    const auto low = Coeffs::makeLowShelf(sampleRate, bandFreqs[0], qVal, juce::Decibels::decibelsToGain(gains[0]));
    const auto lowMid = Coeffs::makePeakFilter(sampleRate, bandFreqs[1], qVal, juce::Decibels::decibelsToGain(gains[1]));
    const auto mid = Coeffs::makePeakFilter(sampleRate, bandFreqs[2], qVal, juce::Decibels::decibelsToGain(gains[2]));
    const auto highMid = Coeffs::makePeakFilter(sampleRate, bandFreqs[3], qVal, juce::Decibels::decibelsToGain(gains[3]));
    const auto high = Coeffs::makeHighShelf(sampleRate, bandFreqs[4], qVal, juce::Decibels::decibelsToGain(gains[4]));

    juce::Path curvePath;
    bool started = false;
    const int steps = 256;

    for (int s = 0; s <= steps; ++s)
    {
        float norm = (float)s / (float)steps;
        float freq = 20.0f * std::pow(1000.0f, norm); // 20 Hz to 20 kHz
        float xPos = cx + pad + norm * (w - 2.0f * pad);

        const double totalMag = low->getMagnitudeForFrequency(freq, sampleRate)
            * lowMid->getMagnitudeForFrequency(freq, sampleRate)
            * mid->getMagnitudeForFrequency(freq, sampleRate)
            * highMid->getMagnitudeForFrequency(freq, sampleRate)
            * high->getMagnitudeForFrequency(freq, sampleRate);
        const float totalGainDb = juce::Decibels::gainToDecibels((float) totalMag, -24.0f);

        float yPos = midY - totalGainDb / 24.0f * (h * 0.42f);
        if (!started) { curvePath.startNewSubPath(xPos, yPos); started = true; }
        else curvePath.lineTo(xPos, yPos);
    }

    // Fill under curve
    {
        juce::Path fillPath(curvePath);
        fillPath.lineTo(cx + w - pad, midY);
        fillPath.lineTo(cx + pad, midY);
        fillPath.closeSubPath();
        g.setColour(fx::accent::eq.withAlpha(0.08f));
        g.fillPath(fillPath);
    }

    // Stroke curve
    g.setColour(fx::accent::eq.withAlpha(0.9f));
    g.strokePath(curvePath, juce::PathStrokeType(2.5f));

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
    drawBadge({ cx + w - 88.0f, cy + 14.0f, 66.0f, 22.0f }, internalTrimDb > 0.25f ? "HEADROOM" : "SAFE", internalTrimDb > 0.25f ? fx::col::meterMid : fx::col::textSecondary);

    g.setColour(fx::col::textMuted);
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    const juce::String riskText = internalTrimDb > 0.25f
        ? ("internal trim " + juce::String(internalTrimDb, 1) + " dB for stacked boosts")
        : (maxPositiveGain > 9.0f || qVal > 4.5f ? "high boost or high Q can still ring on transients" : "moderate boost profile");
    g.drawText(riskText, (int)cx + 28, (int)(cy + 16), 240, 18, juce::Justification::centredLeft);
    g.drawText("bandwidth " + juce::String(bandwidthOctaves, 2) + " oct at current Q", (int)cx + 28, (int)(cy + 32), 220, 18, juce::Justification::centredLeft);

    // Band point indicators
    for (int b = 0; b < 5; ++b)
    {
        float norm = std::log10(bandFreqs[b] / 20.0f) / std::log10(20000.0f / 20.0f);
        float xPos = cx + pad + norm * (w - 2.0f * pad);
        const float bandMag = juce::Decibels::gainToDecibels((float)
            (b == 0 ? low->getMagnitudeForFrequency(bandFreqs[b], sampleRate)
             : b == 1 ? lowMid->getMagnitudeForFrequency(bandFreqs[b], sampleRate)
             : b == 2 ? mid->getMagnitudeForFrequency(bandFreqs[b], sampleRate)
             : b == 3 ? highMid->getMagnitudeForFrequency(bandFreqs[b], sampleRate)
                      : high->getMagnitudeForFrequency(bandFreqs[b], sampleRate)), -24.0f);
        float yPos = midY - bandMag / 24.0f * (h * 0.42f);

        // Q width visualization (horizontal extent)
        float qWidth = (w - 2.0f * pad) * (1.0f / juce::jmax(qVal, 0.3f)) * 0.08f;
        g.setColour(fx::accent::eq.withAlpha(0.12f));
        g.fillRoundedRectangle(xPos - qWidth, juce::jmin(yPos, midY), qWidth * 2.0f, std::abs(yPos - midY), 3.0f);

        // Band dot
        float pulse = 0.7f + 0.3f * std::sin(phase + (float)b * 1.2f);
        g.setColour(fx::accent::eq.withAlpha(0.2f * pulse));
        g.fillEllipse(xPos - 12.0f, yPos - 12.0f, 24.0f, 24.0f);
        g.setColour(fx::accent::eq);
        g.fillEllipse(xPos - 5.0f, yPos - 5.0f, 10.0f, 10.0f);
        g.setColour(fx::col::bg);
        g.fillEllipse(xPos - 2.5f, yPos - 2.5f, 5.0f, 5.0f);

        // dB value at point
        g.setColour(fx::col::textSecondary);
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        g.drawText(juce::String(gains[b], 1) + " dB",
                   (int)(xPos - 20), (int)(yPos - 18), 40, 12, juce::Justification::centred);
    }
}

void MusiqueEQEditor::paint(juce::Graphics& g)
{
    g.fillAll(fx::col::bg);
    fx::paint::header(g, getWidth(), fx::accent::eq);
    if (pluginIcon.isValid())
        g.drawImage(pluginIcon, juce::Rectangle<float>(12, 10, 40, 40), juce::RectanglePlacement::centred);
    fx::paint::presetBar(g, getWidth());
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
    bypassBtn.setBounds(getWidth() - 310, 16, 64, fx::dim::btnH);
    monoBtn.setBounds(getWidth() - 254, 16, 96, fx::dim::btnH);
    headroomBtn.setBounds(getWidth() - 166, 16, 82, fx::dim::btnH);
    settingsBtn.setBounds(getWidth() - 64, 16, 42, fx::dim::btnH);

    // Preset bar
    const int py = fx::dim::headerH + 11;
    prevBtn.setBounds(260, py, 30, fx::dim::btnH);
    presetBox.setBounds(294, py, 250, fx::dim::btnH);
    nextBtn.setBounds(548, py, 30, fx::dim::btnH);
    saveBtn.setBounds(590, py, 56, fx::dim::btnH);
    abBtn.setBounds(652, py, 48, fx::dim::btnH);

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
