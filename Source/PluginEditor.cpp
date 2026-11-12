#include "PluginEditor.h"

namespace
{
constexpr double kFMin = 10.0, kFMax = 30000.0, kDbRange = 18.0;
// Dune palette: warm white and sand
const juce::Colour kBg (0xfff6f1e7), kPanel (0xffe9e0cf), kGrid (0xffdcd1bc), kText (0xff7a705e), kInk (0xff2f2a20),
                   kAccent (0xffb5742a), kMid (0xff2f9e84), kSide (0xff7a5cd6), kRight (0xff2f7fc4);

juce::Colour channelColour (int ch)
{
    switch (ch) { case 3: return kMid; case 4: return kSide; case 2: return kRight; default: return kAccent; }
}

bool bandHasGain (int type) { return type == 0 || type == 1 || type == 2; }   // bell, shelves
bool bandIsPass  (int type) { return type == 3 || type == 4; }

void fillCombo (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, juce::ComboBox& box)
{
    if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        box.addItemList (c->choices, 1);
}
} // namespace

// ===========================================================================
EqDisplay::EqDisplay (MasteringEQAudioProcessor& p) : proc (p)
{
    spectrum.fill (-120.0f);
    startTimerHz (30);
}

float EqDisplay::freqToX (double f) const
{
    const double n = std::log (std::max (f, kFMin) / kFMin) / std::log (kFMax / kFMin);
    return plot.getX() + (float) n * plot.getWidth();
}

double EqDisplay::xToFreq (float x) const
{
    const double n = juce::jlimit (0.0, 1.0, (double) ((x - plot.getX()) / plot.getWidth()));
    return kFMin * std::pow (kFMax / kFMin, n);
}

float EqDisplay::dbToY (double db) const
{
    return plot.getY() + (float) ((kDbRange - db) / (2.0 * kDbRange)) * plot.getHeight();
}

double EqDisplay::yToDb (float y) const
{
    return kDbRange - (double) ((y - plot.getY()) / plot.getHeight()) * 2.0 * kDbRange;
}

void EqDisplay::setParam (int band, const char* name, float value)
{
    if (auto* p = proc.apvts.getParameter (MasteringEQAudioProcessor::pid (band, name)))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void EqDisplay::updateAnalyzer()
{
    const int pos = proc.anaPos.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
        fftData[(size_t) i] = proc.anaBuf[(size_t) ((pos - fftSize + i + 4 * MasteringEQAudioProcessor::anaSize)
                                                    % MasteringEQAudioProcessor::anaSize)];
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
    window.multiplyWithWindowingTable (fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    for (int k = 0; k < fftSize / 2; ++k)
    {
        const float db = juce::Decibels::gainToDecibels (fftData[(size_t) k] / (0.25f * (float) fftSize), -120.0f);
        spectrum[(size_t) k] = 0.7f * spectrum[(size_t) k] + 0.3f * db;
    }
}

void EqDisplay::timerCallback()
{
    updateAnalyzer();
    repaint();
}

void EqDisplay::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setFont (juce::FontOptions (11.0f));

    // grid
    for (double f : { 20.0, 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0, 20000.0 })
    {
        const float x = freqToX (f);
        g.setColour (kGrid);
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
        g.setColour (kText);
        g.drawText (f >= 1000 ? juce::String (f / 1000.0, f >= 10000 ? 0 : (f == 1000 || f == 2000 || f == 5000 ? 0 : 1)) + "k"
                              : juce::String ((int) f),
                    (int) x - 20, (int) plot.getBottom() + 2, 40, 14, juce::Justification::centred);
    }
    for (int db = -18; db <= 18; db += 6)
    {
        const float y = dbToY (db);
        g.setColour (db == 0 ? kGrid.darker (0.35f) : kGrid);
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (kText);
        g.drawText ((db > 0 ? "+" : "") + juce::String (db), 2, (int) y - 7, 34, 14, juce::Justification::centredRight);
    }

    // analyzer
    {
        const double fs = std::max (proc.getSampleRate(), 8000.0);
        juce::Path path;
        bool started = false;
        float lastX = plot.getX();
        for (int k = 2; k < fftSize / 2; ++k)
        {
            const double f = fs * (double) k / (double) fftSize;
            if (f < kFMin || f > kFMax) continue;
            const float x = freqToX (f);
            if (started && x - lastX < 1.0f) continue;
            const float y = juce::jmap (spectrum[(size_t) k], -110.0f, -10.0f, plot.getBottom(), plot.getY());
            const float yc = juce::jlimit (plot.getY(), plot.getBottom(), y);
            if (! started) { path.startNewSubPath (x, plot.getBottom()); path.lineTo (x, yc); started = true; }
            else path.lineTo (x, yc);
            lastX = x;
        }
        if (started)
        {
            path.lineTo (lastX, plot.getBottom());
            path.closeSubPath();
            g.setGradientFill (juce::ColourGradient (juce::Colour (0x66c9a46a), 0.0f, plot.getY(),
                                                     juce::Colour (0x10c9a46a), 0.0f, plot.getBottom(), false));
            g.fillPath (path);
        }
    }

    // band responses
    eqd::Band bands[eqd::kMaxBands];
    bool anyLR = false, anyMS = false;
    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        bands[i] = proc.readBand (i);
        if (bands[i].on && (bands[i].ch == eqd::Channel::Left || bands[i].ch == eqd::Channel::Right)) anyLR = true;
        if (bands[i].on && (bands[i].ch == eqd::Channel::Mid || bands[i].ch == eqd::Channel::Side)) anyMS = true;
    }

    auto curve = [&] (const eqd::Channel group, juce::Colour c, float thick)
    {
        juce::Path p;
        for (float x = plot.getX(); x <= plot.getRight(); x += 2.0f)
        {
            const double db = juce::jlimit (-kDbRange * 2, kDbRange * 2,
                                            eqd::totalMagnitudeDb (bands, eqd::kMaxBands, group, xToFreq (x)));
            const float y = dbToY (db);
            if (x == plot.getX()) p.startNewSubPath (x, y); else p.lineTo (x, y);
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (thick));
    };

    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (plot.toNearestInt());
        curve (eqd::Channel::Left, kAccent, 2.2f);
        if (anyLR) curve (eqd::Channel::Right, kRight, 1.6f);
        if (anyMS) { curve (eqd::Channel::Mid, kMid, 1.6f); curve (eqd::Channel::Side, kSide, 1.6f); }
    }

    // nodes
    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        if (! bands[i].on) continue;
        const float x = freqToX (bands[i].freq);
        const float y = dbToY (bandHasGain ((int) bands[i].type) ? bands[i].gainDb : 0.0);
        const auto c = channelColour ((int) bands[i].ch);
        g.setColour (c.withAlpha (i == selected ? 1.0f : 0.75f));
        g.fillEllipse (x - 8, y - 8, 16, 16);
        if (i == selected)
        {
            g.setColour (kInk);
            g.drawEllipse (x - 10, y - 10, 20, 20, 1.5f);
        }
        g.setColour (juce::Colours::white);
        g.drawText (juce::String (i + 1), (int) x - 8, (int) y - 8, 16, 16, juce::Justification::centred);
    }
}

int EqDisplay::hitTest (juce::Point<float> p) const
{
    int best = -1;
    float bestD = 14.0f;
    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        const auto b = proc.readBand (i);
        if (! b.on) continue;
        const juce::Point<float> c (freqToX (b.freq), dbToY (bandHasGain ((int) b.type) ? b.gainDb : 0.0));
        const float d = c.getDistanceFrom (p);
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTest (e.position);
    if (dragging >= 0)
    {
        selected = dragging;
        if (onSelect) onSelect (selected);
        for (auto* n : { "freq", "gain" })
            if (auto* p = proc.apvts.getParameter (MasteringEQAudioProcessor::pid (dragging, n))) p->beginChangeGesture();
    }
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    setParam (dragging, "freq", (float) juce::jlimit (kFMin, kFMax, xToFreq (e.position.x)));
    const auto b = proc.readBand (dragging);
    if (bandHasGain ((int) b.type))
        setParam (dragging, "gain", (float) juce::jlimit (-24.0, 24.0, yToDb (e.position.y)));
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto* n : { "freq", "gain" })
            if (auto* p = proc.apvts.getParameter (MasteringEQAudioProcessor::pid (dragging, n))) p->endChangeGesture();
    dragging = -1;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = hitTest (e.position);
    if (hit >= 0)
    {
        setParam (hit, "on", 0.0f);
        return;
    }
    if (! plot.contains (e.position)) return;
    for (int i = 0; i < eqd::kMaxBands; ++i)
        if (! proc.readBand (i).on)
        {
            setParam (i, "freq", (float) xToFreq (e.position.x));
            const auto b = proc.readBand (i);
            setParam (i, "gain", bandHasGain ((int) b.type) ? (float) juce::jlimit (-24.0, 24.0, yToDb (e.position.y)) : 0.0f);
            setParam (i, "on", 1.0f);
            selected = i;
            if (onSelect) onSelect (i);
            return;
        }
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int hit = hitTest (e.position);
    if (hit < 0) return;
    const auto b = proc.readBand (hit);
    if (bandIsPass ((int) b.type))
    {
        if (auto* p = proc.apvts.getParameter (MasteringEQAudioProcessor::pid (hit, "slope")))
        {
            const int idx = juce::jlimit (0, 7, (int) std::lround (p->convertFrom0to1 (p->getValue())) + (w.deltaY > 0 ? 1 : -1));
            p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
        }
    }
    else
    {
        setParam (hit, "q", (float) juce::jlimit (0.1, 20.0, b.q * std::pow (1.25, (double) w.deltaY * 4.0)));
    }
}

// ===========================================================================
MasteringEQAudioProcessorEditor::MasteringEQAudioProcessorEditor (MasteringEQAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), display (p)
{
    if (auto* v4 = dynamic_cast<juce::LookAndFeel_V4*> (&getLookAndFeel()))
        v4->setColourScheme (juce::LookAndFeel_V4::getLightColourScheme());

    titleLabel.setText ("DUNE", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, kAccent);
    addAndMakeVisible (titleLabel);

    fillCombo (proc.apvts, "mode", modeBox);
    fillCombo (proc.apvts, "quality", qualityBox);
    addAndMakeVisible (modeBox);
    addAndMakeVisible (qualityBox);
    modeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "mode", modeBox);
    qualityAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, "quality", qualityBox);

    outSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    addAndMakeVisible (outSlider);
    outAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "out", outSlider);

    addAndMakeVisible (bypassButton);
    bypassAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "bypass", bypassButton);

    latencyLabel.setColour (juce::Label::textColourId, kText);
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        bandButtons[(size_t) i].setButtonText (juce::String (i + 1));
        bandButtons[(size_t) i].onClick = [this, i] { selectBand (i); display.setSelected (i); };
        addAndMakeVisible (bandButtons[(size_t) i]);
    }

    fillCombo (proc.apvts, "b1_type", typeBox);
    fillCombo (proc.apvts, "b1_ch", chanBox);
    fillCombo (proc.apvts, "b1_slope", slopeBox);
    for (auto* c : { &typeBox, &chanBox, &slopeBox }) addAndMakeVisible (c);
    addAndMakeVisible (onButton);

    for (auto* s : { &freqSlider, &gainSlider, &qSlider })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 90, 18);
        addAndMakeVisible (s);
    }
    struct L { juce::Label* l; const char* t; };
    for (auto e : { L { &freqLabel, "Frequency" }, L { &gainLabel, "Gain" }, L { &qLabel, "Q" } })
    {
        e.l->setText (e.t, juce::dontSendNotification);
        e.l->setJustificationType (juce::Justification::centred);
        e.l->setColour (juce::Label::textColourId, kText);
        addAndMakeVisible (e.l);
    }

    addAndMakeVisible (display);
    display.onSelect = [this] (int i) { selectBand (i); };

    selectBand (0);
    setSize (1000, 680);
    startTimerHz (10);
}

void MasteringEQAudioProcessorEditor::selectBand (int i)
{
    currentBand = i;
    onAtt.reset(); typeAtt.reset(); chanAtt.reset(); slopeAtt.reset();
    freqAtt.reset(); gainAtt.reset(); qAtt.reset();
    using A = juce::AudioProcessorValueTreeState;
    auto id = [i] (const char* n) { return MasteringEQAudioProcessor::pid (i, n); };
    onAtt    = std::make_unique<A::ButtonAttachment>   (proc.apvts, id ("on"),    onButton);
    typeAtt  = std::make_unique<A::ComboBoxAttachment> (proc.apvts, id ("type"),  typeBox);
    chanAtt  = std::make_unique<A::ComboBoxAttachment> (proc.apvts, id ("ch"),    chanBox);
    slopeAtt = std::make_unique<A::ComboBoxAttachment> (proc.apvts, id ("slope"), slopeBox);
    freqAtt  = std::make_unique<A::SliderAttachment>   (proc.apvts, id ("freq"),  freqSlider);
    gainAtt  = std::make_unique<A::SliderAttachment>   (proc.apvts, id ("gain"),  gainSlider);
    qAtt     = std::make_unique<A::SliderAttachment>   (proc.apvts, id ("q"),     qSlider);
    display.setSelected (i);
    updateControlVisibility();
    updateBandButtons();
}

void MasteringEQAudioProcessorEditor::updateControlVisibility()
{
    const int type = (int) typeBox.getSelectedItemIndex();
    const bool pass = bandIsPass (type);
    gainSlider.setVisible (bandHasGain (type));
    gainLabel.setVisible (bandHasGain (type));
    qSlider.setVisible (! pass);
    qLabel.setVisible (! pass);
    slopeBox.setVisible (pass);
}

void MasteringEQAudioProcessorEditor::updateBandButtons()
{
    for (int i = 0; i < eqd::kMaxBands; ++i)
    {
        const auto b = proc.readBand (i);
        auto& btn = bandButtons[(size_t) i];
        btn.setColour (juce::TextButton::buttonColourId,
                       i == currentBand ? kAccent.withAlpha (0.55f) : juce::Colour (0xffded3bf));
        btn.setColour (juce::TextButton::textColourOffId, b.on ? kInk : kText);
    }
}

void MasteringEQAudioProcessorEditor::timerCallback()
{
    updateControlVisibility();
    updateBandButtons();
    const int lat = proc.getLatencyForGui();
    if (lat != lastLatency)
    {
        lastLatency = lat;
        const double fs = std::max (proc.getSampleRate(), 1.0);
        latencyLabel.setText ("Latency: " + juce::String (lat) + " smp (" + juce::String (1000.0 * lat / fs, 1) + " ms)",
                              juce::dontSendNotification);
    }
}

void MasteringEQAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kPanel);
}

void MasteringEQAudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced (10, 8);

    auto top = r.removeFromTop (30);
    titleLabel.setBounds (top.removeFromLeft (150));
    modeBox.setBounds (top.removeFromLeft (170).reduced (2));
    qualityBox.setBounds (top.removeFromLeft (110).reduced (2));
    bypassButton.setBounds (top.removeFromRight (80));
    outSlider.setBounds (top.removeFromRight (230));
    latencyLabel.setBounds (top);

    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (170);
    display.setBounds (r);

    auto row1 = bottom.removeFromTop (34).reduced (0, 3);
    for (auto& b : bandButtons) b.setBounds (row1.removeFromLeft (46).reduced (2));
    row1.removeFromLeft (14);
    onButton.setBounds (row1.removeFromLeft (60));
    typeBox.setBounds (row1.removeFromLeft (130).reduced (2));
    chanBox.setBounds (row1.removeFromLeft (110).reduced (2));
    slopeBox.setBounds (row1.removeFromLeft (120).reduced (2));

    auto row2 = bottom.reduced (0, 2);
    const int w = 130;
    auto place = [&] (juce::Slider& s, juce::Label& l)
    {
        auto cell = row2.removeFromLeft (w);
        l.setBounds (cell.removeFromTop (16));
        s.setBounds (cell);
    };
    place (freqSlider, freqLabel);
    place (gainSlider, gainLabel);
    place (qSlider, qLabel);
}
